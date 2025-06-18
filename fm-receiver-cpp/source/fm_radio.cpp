#define _use_math_defines

#ifdef GNURADIO_3_7
#include <algorithm>
#include <boost/format.hpp>
#include <chrono>
#include <gnuradio/filter/fir_filter_fff.h>
#include <gnuradio/filter/rational_resampler_base_ccf.h>
#include <math.h>
#include <thread>
#include <uhd/exception.hpp>

#else
#include <gnuradio/filter/fir_filter_blk.h>
#include <gnuradio/filter/rational_resampler.h>
#include <gnuradio/fft/window.h>
#endif

#include <boost/program_options.hpp>
#include <csignal>
#include <gnuradio/analog/quadrature_demod_cf.h>
#include <gnuradio/audio/sink.h>
#include <gnuradio/blocks/complex_to_float.h>
#include <gnuradio/filter/firdes.h>
#include <gnuradio/top_block.h>
#include <gnuradio/uhd/usrp_sink.h>
#include <gnuradio/uhd/usrp_source.h>
#include <iostream>
#include <uhd/types/tune_request.hpp>
#include <uhd/usrp/multi_usrp.hpp>
#include <uhd/utils/safe_main.hpp>
#include <uhd/utils/thread.hpp>

//--------------------------------------------------------------------------------------------------
//-- Debugging Tool
//--------------------------------------------------------------------------------------------------
#define DEBUG 1

#ifdef DEBUG
#define DEBUG_PRINT(fmt, args...)                                                                  \
    fprintf(stderr, "DEBUG: %d:%s(): " fmt "\n", __LINE__, __func__, ##args)
#else
#define DEBUG_PRINT(fmt, args...)                                                                  \
    do {                                                                                           \
    } while (0)
#endif

//--------------------------------------------------------------------------------------------------
//-- Global Constants
//--------------------------------------------------------------------------------------------------
const std::string STREAM_ARGS = "fc32";

const int INTERPOL_FACTOR = 1;
const int DECIMATE_FACTOR_RR = 5;
const int DECIMATE_FACTOR_DEMOD = 4;

const int LEFT_CHANNEL = 0;
const int RIGHT_CHANNEL = 1;

//--------------------------------------------------------------------------------------------------
//-- Global
//--------------------------------------------------------------------------------------------------
namespace po = boost::program_options;

class UserArgs
{
  public:
    std::string program_name;
    std::string device_addr;
    std::string ref_src;
    double sample_rate;
    double output_rate;
    double station;
    double volume;
    double setup_time;
    double deviation;
    size_t channel;
};

//--------------------------------------------------------------------------------------------------
//-- Signal Handlers
//--------------------------------------------------------------------------------------------------
static bool stop_signal_called = false;
void sig_int_handler(int) { stop_signal_called = true; }

//--------------------------------------------------------------------------------------------------
//-- Filters
//--------------------------------------------------------------------------------------------------

std::vector<float> design_filter(int interpolation, int decimation, float fractional_bw = 0.4)
{
    if (fractional_bw >= 0.5 or fractional_bw <= 0) {
        std::cerr << "Invalid fractional_bandwidth, must be in (0, 0.5)";
    }

    const double halfband = 0.5;
    const double beta = 7.0;
    double rate = double(interpolation) / double(decimation);
    double trans_width, mid_transition_band;

    if (rate >= 1.0) {
        trans_width = halfband - fractional_bw;
        mid_transition_band = halfband - trans_width / 2.0;
    } else {
        trans_width = rate * (halfband - fractional_bw);
        mid_transition_band = rate * halfband - trans_width / 2.0;
    }

    std::vector<float> taps;

#ifdef GNURADIO_3_7
    taps = gr::filter::firdes::low_pass(interpolation, interpolation, mid_transition_band,
                                        trans_width, gr::filter::firdes::WIN_KAISER, beta);
#else
    taps = gr::filter::firdes::low_pass(interpolation, interpolation, mid_transition_band,	
		    			trans_width, gr::fft:window::WIN_KAISER, beta);
#endif
    return taps;
}

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
              << "    fm_radio --help" << std::endl  // TODO
              << std::endl;
}

po::options_description set_program_args(std::shared_ptr<UserArgs> user_args)
{
    po::options_description desc("Allowed options");
    // clang-format off
    desc.add_options()
      ("help",         "help message")
      ("name",         po::value<std::string>(&(user_args->device_addr))
       		             ->default_value("fm_broadcast_receiver"),
                       "a descriptive name for the program")
      ("station",      po::value<double>(&(user_args->station))->default_value(99.9),
                       "the fm station you wish to tune into")
      ("volume",       po::value<double>(&(user_args->volume))->default_value(5.0),
                       "should be an integer between 0 and 10")
      ("sample-rate",  po::value<double>(&(user_args->sample_rate))->default_value(1e6),
                       "rate of incoming samples")
      ("output-rate",  po::value<double>(&(user_args->output_rate))->default_value(43e3),
                       "rate at which samples will be fed to the computer's audio card")
      ("deviation",    po::value<double>(&(user_args->deviation))->default_value(75e3),
                       "fm broadcast deviation")
      ("device",       po::value<std::string>(&(user_args->device_addr))->default_value(""),
                       "multi uhd device address args")
      ("channel",      po::value<size_t>(&(user_args->channel))->default_value(0),
                       "which channel to use")
      ("ref-src",      po::value<std::string>(&(user_args->ref_src))->default_value("internal"),
                       "reference source (internal, external, mimo)")
      ("setup",        po::value<double>(&(user_args->setup_time))->default_value(1.0),
                       "seconds of setup time");
    // clang-format on

    return desc;
}

int setup_usrp_device(uhd::usrp::multi_usrp::sptr usrp_device, std::shared_ptr<UserArgs> user_args,
                      double *actual_sample_rate, double *actual_gain, po::variables_map vm)
{
    // Instantiate Crimson Device
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
    usrp_device->set_clock_source(user_args->ref_src);  // lock mboard clocks

    // Set the Sample Rate
    double desired_sample_rate = user_args->sample_rate;
    if (user_args->sample_rate <= 0.0) {
        std::cerr << boost::format("%s is not a valid sample rate.") % desired_sample_rate
                  << "The sample rate needs to be a positive float"
                  << "Please specify a valid sample rate." << std::endl;
        return ~0;
    }
    std::cout << boost::format("Setting RX Rate: %5.2f Msps...") % (desired_sample_rate / 1e6)
              << std::endl;
    usrp_device->set_rx_rate(desired_sample_rate, user_args->channel);
    *actual_sample_rate = usrp_device->get_rx_rate(user_args->channel);
    std::cout << boost::format("Actual  RX Rate: %f Msps...") % (*actual_sample_rate / 1e6)
              << std::endl;

    // Set the RF Gain
    double desired_gain = user_args->volume * 1e-1;
    std::cout << boost::format("Setting RX Gain: %f dB...") % desired_gain << std::endl;
    usrp_device->set_rx_gain(desired_gain, user_args->channel);
    *actual_gain = usrp_device->get_rx_gain(user_args->channel);
    std::cout << boost::format("Actual  RX Gain: %f dB...") % *actual_gain << std::endl;

    // Sleep a bit while the slave locks its time to the master
    std::this_thread::sleep_for(std::chrono::seconds(int64_t(user_args->setup_time)));

    return 0;
}

int uhd_safe_main(int argc, char *argv[])
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

    double center_frequency = user_args->station * 1e6;
    double sample_rate, gain;

    uhd::usrp::multi_usrp::sptr usrp_device = uhd::usrp::multi_usrp::make(user_args->device_addr);
    if (setup_usrp_device(usrp_device, user_args, &sample_rate, &gain, vm) != 0) {
        return ~0;
    };

    // create a receive streamer
    std::vector<size_t> channel_nums;
    channel_nums.push_back(0);
    uhd::stream_args_t stream_args(STREAM_ARGS);
    stream_args.channels = channel_nums;
    uhd::rx_streamer::sptr rx_stream = usrp_device->get_rx_stream(stream_args);

    // GNU Radio blocks
    //---------------------------------------------------------------------------------------------

    // Create top block
    gr::top_block_sptr tb = gr::make_top_block(user_args->program_name);
    gr::uhd::usrp_source::sptr usrp_source;
    usrp_source = gr::uhd::usrp_source::make(user_args->device_addr, stream_args);
    usrp_source->set_samp_rate(sample_rate);
    usrp_source->set_center_freq(center_frequency);

    // Resample source
    std::vector<float> resampler_taps = design_filter(INTERPOL_FACTOR, DECIMATE_FACTOR_RR);

#ifdef GNURADIO_3_7
    gr::filter::rational_resampler_base_ccf::sptr resampler =
        gr::filter::rational_resampler_base_ccf::make(INTERPOL_FACTOR, DECIMATE_FACTOR_RR,
                                                      resampler_taps);
#else
    gr::filter::rational_resampler_ccf::sptr resampler =
	gr::filter::rational_resampler_ccf::make(INTERPOL_FACTOR, DECIMATE_FACTOR_RR,
			    			     resampler_taps);
#endif

    // Demodulate quadrature
    float channel_rate = sample_rate / DECIMATE_FACTOR_DEMOD;
    float k = channel_rate / (2 * M_PI * user_args->deviation);
    gr::analog::quadrature_demod_cf::sptr quad_demod = gr::analog::quadrature_demod_cf::make(k);

    // fir_filter_fff
    std::vector<float> audio_taps;
    gr::filter::fir_filter_fff::sptr fir_filter;
    double transition_width = 1e3;
    audio_taps = gr::filter::firdes::low_pass(gain, user_args->output_rate, 15e3, transition_width);
    fir_filter = gr::filter::fir_filter_fff::make(DECIMATE_FACTOR_DEMOD, audio_taps);

    // audio sink
    gr::audio::sink::sptr audio_sink = gr::audio::sink::make(user_args->output_rate);

    // create flow graph
    tb->connect(usrp_source, 0, resampler, 0);
    tb->connect(resampler, 0, quad_demod, 0);
    tb->connect(quad_demod, 0, fir_filter, 0);
    tb->connect(fir_filter, 0, audio_sink, LEFT_CHANNEL);
    tb->connect(fir_filter, 0, audio_sink, RIGHT_CHANNEL);

    // poll the exit signal while running
    std::cout << "Starting flow graph" << std::endl;
    tb->start();

    if (DEBUG) {
        tb->dump();
    }

    std::signal(SIGINT, &sig_int_handler);
    std::cout << "Press Ctrl + C to exit." << std::endl;
    while (not stop_signal_called) {
        boost::this_thread::sleep(boost::posix_time::milliseconds(1000));
    }
    tb->stop();
    tb->wait();

    std::cout << std::endl << "done!" << std::endl;
    return EXIT_SUCCESS;
}

//--------------------------------------------------------------------------------------------------
