#include <boost/format.hpp>
#include <boost/program_options.hpp>
#include <chrono>
#include <csignal>
#include <gnuradio/audio/sink.h>
#include <gnuradio/blocks/complex_to_float.h>
#include <gnuradio/top_block.h>
#include <gnuradio/uhd/usrp_sink.h>
#include <gnuradio/uhd/usrp_source.h>
#include <thread>
#include <uhd/exception.hpp>
#include <uhd/types/tune_request.hpp>
#include <uhd/usrp/multi_usrp.hpp>
#include <uhd/utils/safe_main.hpp>
#include <uhd/utils/thread.hpp>

/**
 * copy files over with:
 *   rsync -av --exclude=".git" ../fm_receiver jade@summers:~/vikram/cpp
 */

//--------------------------------------------------------------------------------------------------
//-- Constants
//--------------------------------------------------------------------------------------------------
const std::string PROGRAM_NAME = "program_name";

const int AUDIO_CARD_SAMP_RATE = 48e3;

const int LEFT_CHANNEL = 0;
const int RIGHT_CHANNEL = 1;

//--------------------------------------------------------------------------------------------------
//-- Global
//--------------------------------------------------------------------------------------------------
namespace po = boost::program_options;

class UserArgs
{
  public:
    std::string device_addr;
    std::string ref_src;
    double sample_rate;
    double center_frequency;
    double gain;
    double setup_time;
    size_t channel;
};

//--------------------------------------------------------------------------------------------------
//-- Signal Handlers
//--------------------------------------------------------------------------------------------------
static bool stop_signal_called = false;
void sig_int_handler(int) { stop_signal_called = true; }

//--------------------------------------------------------------------------------------------------
//-- Program
//--------------------------------------------------------------------------------------------------
void print_usage(po::options_description desc)
{
    std::cout << "Usage: fm_radio [OPTIONS] ..." << std::endl
              << "Receives frequency modulated signals from the specified station." << std::endl
              << std::endl
              << desc << std::endl
              << std::endl
              << "Examples:" << std::endl
              << "    fm_radio --help" << std::endl // TODO
              << std::endl;
}

po::options_description set_program_args(std::shared_ptr<UserArgs> user_args)
{
    po::options_description desc("Allowed options");
    // clang-format off
    desc.add_options()("help", "help message")
      ("device",       po::value<std::string>(&(user_args->device_addr))->default_value(""),
                       "multi uhd device address args")
      ("channel",      po::value<size_t>(&(user_args->channel))->default_value(0),
                       "which channel to use")
      ("center_freq",  po::value<double>(&(user_args->center_frequency))->default_value(99.9e6),
                       "RF center frequency in Hz")
      ("gain",         po::value<double>(&(user_args->gain))->default_value(1.0),
                       "gain for the RF chain")
      ("samp_rate",    po::value<double>(&(user_args->sample_rate))->default_value(1e6),
                       "rate of incoming samples")
      ("ref_src",      po::value<std::string>(&(user_args->ref_src))->default_value("internal"),
                       "reference source (internal, external, mimo)")
      ("setup",        po::value<double>(&(user_args->setup_time))->default_value(1.0),
                       "seconds of setup time");
    // clang-format on
    return desc;
}

int setup_usrp_device(uhd::usrp::multi_usrp::sptr usrp_device, std::shared_ptr<UserArgs> user_args,
                      po::variables_map vm)
{
    //---------------------------------------------------------------------------------------------
    //-- Instantiate Crimson Device
    //---------------------------------------------------------------------------------------------
    if (not((user_args->ref_src.compare("internal")) == 0 or
            (user_args->ref_src.compare("external")) == 0 or
            (user_args->ref_src.compare("mimo")) == 0)) {
        std::cerr << "Invalid reference source. Reference source should be one of (internal, "
                     "external, mimo)"
                  << std::endl;
        return ~0;
    }
    std::cout << boost::format("Instantiating the usrp usrp_device device with address: %s...") %
                     user_args->device_addr
              << std::endl;
    usrp_device->set_clock_source(user_args->ref_src); // lock mboard clocks

    //---------------------------------------------------------------------------------------------
    //-- Set the Sample Rate
    //---------------------------------------------------------------------------------------------
    double actual_sample_rate;
    if (user_args->sample_rate <= 0.0) {
        std::cerr << boost::format("%s is not a valid sample rate.") % user_args->sample_rate
                  << "The sample rate needs to be a positive float"
                  << "Please specify a valid sample rate." << std::endl;
        return ~0;
    }
    std::cout << boost::format("Setting RX Rate: %f Msps...") % (user_args->sample_rate / 1e6)
              << std::endl;
    usrp_device->set_rx_rate(user_args->sample_rate, user_args->channel);
    actual_sample_rate = usrp_device->get_rx_rate(user_args->channel) / 1e6;
    std::cout << boost::format("Actual RX Rate: %f Msps...") % actual_sample_rate << std::endl;

    //---------------------------------------------------------------------------------------------
    //-- Set the RF Gain
    //---------------------------------------------------------------------------------------------
    double actual_gain;
    if (vm.count("gain")) {
        std::cout << boost::format("Setting RX Gain: %f dB...") % user_args->gain << std::endl;
        usrp_device->set_rx_gain(user_args->gain, user_args->channel);
        actual_gain = usrp_device->get_rx_gain(user_args->channel);
        std::cout << boost::format("Actual RX Gain: %f dB...") % actual_gain << std::endl;
    }

    //---------------------------------------------------------------------------------------------
    //-- Sleep a bit while the slave locks its time to the master
    //---------------------------------------------------------------------------------------------
    std::this_thread::sleep_for(std::chrono::seconds(int64_t(user_args->setup_time)));

    return 0;
}

int UHD_SAFE_MAIN(int argc, char *argv[])
{
    uhd::set_thread_priority_safe();

    // set program args
    po::variables_map vm;
    std::shared_ptr<UserArgs> user_args(new UserArgs());
    po::options_description desc = set_program_args(user_args);
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);
    std::cout << std::endl;

    if (vm.count("help") or argc <= 1) {
        print_usage(desc);
        return ~0;
    }

    uhd::usrp::multi_usrp::sptr usrp_device = uhd::usrp::multi_usrp::make(user_args->device_addr);
    if (setup_usrp_device(usrp_device, user_args, vm) != 0) {
        return ~0;
    };

    // create a receive streamer
    std::vector<size_t> channel_nums;
    channel_nums.push_back(0);
    uhd::stream_args_t stream_args("fc32");
    stream_args.channels = channel_nums;
    uhd::rx_streamer::sptr rx_stream = usrp_device->get_rx_stream(stream_args);

    //---------------------------------------------------------------------------------------------
    //-- setup streaming
    //---------------------------------------------------------------------------------------------
    /* int total_num_samples = 1000; */
    /* double seconds_in_future = 1.5; */
    /* double delta = 1.0; */

    /* std::cout << std::endl */
    /*           << boost::format("Begin streaming %u samples, %5.2f seconds in the future...") % */
    /*                  total_num_samples % seconds_in_future */
    /*           << std::endl; */

    /* uhd::stream_cmd_t stream_cmd(uhd::stream_cmd_t::STREAM_MODE_NUM_SAMPS_AND_DONE); */
    /* stream_cmd.num_samps = total_num_samples; */
    /* stream_cmd.stream_now = false; */
    /* stream_cmd.time_spec = uhd::time_spec_t(seconds_in_future); */
    /* rx_stream->issue_stream_cmd(stream_cmd); */

    /* uhd::rx_metadata_t metadata; */
    /* const size_t samples_per_buff = rx_stream->get_max_num_samps(); */
    /* std::vector<std::vector<std::complex<float>>> buffs( */
    /*     1, std::vector<std::complex<float>>(samples_per_buff)); */

    /* std::complex<float> *buff_ptr; */

    /* // the first call to recv() will block this many seconds before receiving */
    /* double timeout = seconds_in_future + delta; */
    /* size_t num_acc_samples = 0; */

    /* while (num_acc_samples < total_num_samples) { */

    /*     // receive a single packet */
    /*     size_t num_rx_samples = rx_stream->recv(buff_ptr, samples_per_buff, metadata, timeout);
     */

    /*     timeout = delta; // use a smaller timeout for subsequent packets */

    /*     if (metadata.error_code == uhd::rx_metadata_t::ERROR_CODE_TIMEOUT) { */
    /*         break; */
    /*     } */

    /*     if (metadata.error_code != uhd::rx_metadata_t::ERROR_CODE_NONE) { */
    /*         throw std::runtime_error(str(boost::format("Receiver error %s") %
     * metadata.strerror())); */
    /*     } */

    /*     num_acc_samples += num_rx_samples; */
    /* } */

    /* if (num_acc_samples < total_num_samples) { */
    /*     std::cerr << "Receive timeout before all samples received..." << std::endl; */
    /* } */

    gr::top_block_sptr tb = gr::make_top_block(PROGRAM_NAME);
    gr::uhd::usrp_source::sptr usrp_source =
        gr::uhd::usrp_source::make(user_args->device_addr, stream_args);
    usrp_source->set_samp_rate(user_args->sample_rate);
    usrp_source->set_center_freq(user_args->center_frequency);

    gr::audio::sink::sptr audio_sink = gr::audio::sink::make(AUDIO_CARD_SAMP_RATE);
    tb->connect(usrp_source, 0, audio_sink, LEFT_CHANNEL);
    tb->connect(usrp_source, 0, audio_sink, RIGHT_CHANNEL);

    //---------------------------------------------------------------------------------------------
    //-- poll the exit signal while running
    //---------------------------------------------------------------------------------------------
    std::cout << "Starting flow graph" << std::endl;
    tb->start();

    std::signal(SIGINT, &sig_int_handler);
    std::cout << "Press ctrl + c to exit." << std::endl;
    while (not stop_signal_called) {
        boost::this_thread::sleep(boost::posix_time::milliseconds(100));
    }

    std::cout << std::endl << "done!" << std::endl;
    return EXIT_SUCCESS;
}

//--------------------------------------------------------------------------------------------------
