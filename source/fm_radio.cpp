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

//--------------------------------------------------------------------------------------------------
//-- Constants
//--------------------------------------------------------------------------------------------------
const int audio_samp_rate = 48e3;

namespace po = boost::program_options;

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

int UHD_SAFE_MAIN(int argc, char *argv[])
{
    uhd::set_thread_priority_safe();

    //------------------------------------------------------------------
    //-- Set program args
    //------------------------------------------------------------------
    std::string device_addr, ant, ref_src;
    double desired_sample_rate, center_frequency, desired_gain, setup_time;
    size_t channel;

    po::options_description desc("Allowed options");
    // clang-format off
    desc.add_options()
      ("help", "help message")
      ("device", po::value<std::string>(&device_addr)->default_value(""), "multi uhd device address args")
      ("channel", po::value<size_t>(&channel)->default_value(0), "which channel to use")
      ("center_freq", po::value<double>(&center_frequency)->default_value(99.9e6),
                      "RF center frequency in Hz")
      ("gain", po::value<double>(&desired_gain)->default_value(1.0), "gain for the RF chain")
      ("samp_rate", po::value<double>(&desired_sample_rate)->default_value(1e6),
                    "rate of incoming samples")
      ("ref_src", po::value<std::string>(&ref_src)->default_value("internal"),
                  "reference source (internal, external, mimo)")
      ("setup", po::value<double>(&setup_time)->default_value(1.0), "seconds of setup time")
    ;
    // clang-format on
    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    std::cout << std::endl;

    if (vm.count("help") or argc <= 1) {
        print_usage(desc);
        return ~0;
    }

    //------------------------------------------------------------------
    //-- Instantiate Crimson Device
    //------------------------------------------------------------------
    if (not((ref_src.compare("internal")) == 0 or (ref_src.compare("external")) == 0 or
            (ref_src.compare("mimo")) == 0)) {
        std::cerr << "Invalid reference source. Reference source should be one of (internal, "
                     "external, mimo)"
                  << std::endl;
        return 0;
    }
    std::cout << boost::format("Instantiating the usrp crimson device with address: %s...") %
                     device_addr
              << std::endl;
    uhd::usrp::multi_usrp::sptr crimson = uhd::usrp::multi_usrp::make(device_addr);
    crimson->set_clock_source(ref_src); // lock mboard clocks

    //------------------------------------------------------------------
    //-- Set the Sample Rate
    //------------------------------------------------------------------
    double actual_sample_rate;
    if (desired_sample_rate <= 0.0) {
        std::cerr << boost::format("%s is not a valid sample rate.") % desired_sample_rate
                  << "The sample rate needs to be a positive float"
                  << "Please specify a valid sample rate." << std::endl;
        return ~0;
    }
    std::cout << boost::format("Setting RX Rate: %f Msps...") % (desired_sample_rate / 1e6)
              << std::endl;
    crimson->set_rx_rate(desired_sample_rate, channel);
    actual_sample_rate = crimson->get_rx_rate(channel) / 1e6;
    std::cout << boost::format("Actual RX Rate: %f Msps...") % actual_sample_rate << std::endl;

    //------------------------------------------------------------------
    //-- Set the RF Gain
    //------------------------------------------------------------------
    double actual_gain;
    if (vm.count("gain")) {
        std::cout << boost::format("Setting RX Gain: %f dB...") % desired_gain << std::endl;
        crimson->set_rx_gain(desired_gain, channel);
        actual_gain = crimson->get_rx_gain(channel);
        std::cout << boost::format("Actual RX Gain: %f dB...") % actual_gain << std::endl;
    }

    //------------------------------------------------------------------
    //-- Sleep a bit while the slave locks its time to the master
    //------------------------------------------------------------------
    std::this_thread::sleep_for(std::chrono::seconds(int64_t(setup_time)));

    // create a receive streamer
    std::vector<size_t> channel_nums;
    channel_nums.push_back(0);

    uhd::stream_args_t stream_args("fc32"); // complex floats
    stream_args.channels = channel_nums;
    uhd::rx_streamer::sptr rx_stream = crimson->get_rx_stream(stream_args);

    //------------------------------------------------------------------
    //-- setup streaming
    //------------------------------------------------------------------
    int total_num_samples = 1000;
    double seconds_in_future = 1.5;
    double delta = 1.0;

    std::cout << std::endl
              << boost::format("Begin streaming %u samples, %5.2f seconds in the future...") %
                     total_num_samples % seconds_in_future
              << std::endl;

    uhd::stream_cmd_t stream_cmd(uhd::stream_cmd_t::STREAM_MODE_NUM_SAMPS_AND_DONE);
    stream_cmd.num_samps = total_num_samples;
    stream_cmd.stream_now = false;
    stream_cmd.time_spec = uhd::time_spec_t(seconds_in_future);
    rx_stream->issue_stream_cmd(stream_cmd);

    uhd::rx_metadata_t metadata;
    const size_t samples_per_buff = rx_stream->get_max_num_samps();
    std::vector<std::vector<std::complex<float>>> buffs(
        1, std::vector<std::complex<float>>(samples_per_buff));

    std::complex<float> *buff_ptr;

    // the first call to recv() will block this many seconds before receiving
    double timeout = seconds_in_future + delta;
    size_t num_acc_samples = 0;

    while (num_acc_samples < total_num_samples) {

        // receive a single packet
        size_t num_rx_samples = rx_stream->recv(buff_ptr, samples_per_buff, metadata, timeout);

        timeout = delta; // use a smaller timeout for subsequent packets

        if (metadata.error_code == uhd::rx_metadata_t::ERROR_CODE_TIMEOUT) {
            break;
        }

        if (metadata.error_code != uhd::rx_metadata_t::ERROR_CODE_NONE) {
            throw std::runtime_error(str(boost::format("Receiver error %s") % metadata.strerror()));
        }

        num_acc_samples += num_rx_samples;
    }

    if (num_acc_samples < total_num_samples) {
        std::cerr << "Receive timeout before all samples received..." << std::endl;
    }

    //  ================================================================================
    gr::top_block_sptr tb = gr::make_top_block("program_name");

    gr::uhd::usrp_source::sptr usrp_source =
        gr::uhd::usrp_source::make(device_addr, uhd::stream_args_t("fc32"));
    usrp_source->set_samp_rate(actual_sample_rate);
    usrp_source->set_center_freq(center_frequency);

    gr::uhd::usrp_sink::sptr usrp_sink =
        gr::uhd::usrp_sink::make(device_addr, uhd::stream_args_t("fc32"));
    usrp_sink->set_samp_rate(actual_sample_rate);
    usrp_sink->set_center_freq(center_frequency);
    tb->connect(usrp_source, 0, usrp_sink, 0);
    // ================================================================================

    //------------------------------------------------------------------
    //-- poll the exit signal while running
    //------------------------------------------------------------------
    std::cout << "starting flow graph" << std::endl;
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
