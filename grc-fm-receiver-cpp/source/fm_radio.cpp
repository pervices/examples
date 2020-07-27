#define _USE_MATH_DEFINES

#include <algorithm>
#include <boost/format.hpp>
#include <boost/program_options.hpp>
#include <chrono>
#include <csignal>
#include <gnuradio/analog/quadrature_demod_cf.h>
#include <gnuradio/audio/sink.h>
#include <gnuradio/blocks/complex_to_float.h>
#include <gnuradio/filter/fir_filter_fff.h>
#include <gnuradio/filter/pm_remez.h>
#include <gnuradio/filter/rational_resampler_base_ccc.h>
#include <gnuradio/top_block.h>
#include <gnuradio/uhd/usrp_sink.h>
#include <gnuradio/uhd/usrp_source.h>
#include <iostream>
#include <math.h>
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
const std::string STREAM_ARGS = "fc32";

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
//-- Helper Functions
//--------------------------------------------------------------------------------------------------

std::vector<float> convert_vector_doubles_to_floats(std::vector<double> vec_doubles)
{
    std::vector<float> vec_floats;
    for (int i = 0; i < vec_doubles.size(); i++) {
        vec_floats.push_back(vec_doubles[i]);
    }

    return vec_floats;
}

double vector_max(std::vector<double> &arr)
{
    double max = INT_MIN;
    for (auto val : arr) {
        if (max < val)
            max = val;
    }
    return max;
}

float lporder(float freq1, float freq2, float delta_p, float delta_s)
{
    /**
     * FIR lowpass filter length estimator.  freq1 and freq2 are
     * normalized to the sampling frequency.  delta_p is the passband
     * deviation (ripple), delta_s is the stopband deviation (ripple).
     * Note, this works for high pass filters too (freq1 > freq2), but
     * doesn't work well if the transition is near f == 0 or f == fs/2
     * From Herrmann et al (1973), Practical design rules for optimum
     * finite impulse response filters.
     *
     * Bell System Technical J., 52, 769-99
     **/

    float df = std::abs(freq2 - freq1);
    float ddp = std::log10(delta_p);
    float dds = std::log10(delta_s);

    float a1 = 5.309e-3;
    float a2 = 7.114e-2;
    float a3 = -4.761e-1;
    float a4 = -2.66e-3;
    float a5 = -5.941e-1;
    float a6 = -4.278e-1;

    float b1 = 11.01217;
    float b2 = 0.5124401;

    float t1 = a1 * ddp * ddp;
    float t2 = a2 * ddp;
    float t3 = a4 * ddp * ddp;
    float t4 = a5 * ddp;

    float dinf = ((t1 + t2 + a3) * dds) + (t3 + t4 + a6);
    float ff = b1 + b2 * (ddp - dds);
    float n = dinf / df - ff * df + 1;

    return n;
}

std::vector<double> remezord(std::vector<double> fcuts, std::vector<double> mags,
                             std::vector<double> devs, int nextra_taps, int fsamp = 2)
{

    float nbands = mags.size();

    /**
     * fsamp defaults to 2 Hz, implying a Nyquist frequency of 1 Hz.
     * You can therefore specify band edges scaled to a particular applications.
     * sampling frequency.
     */
    for (int i = 0; i < fcuts.size(); i++) {
        fcuts[i] = fcuts[i] / fsamp;
    }

    if (mags.size() != devs.size()) {
        std::cerr << "Length of mags and devs must be equal";
    }

    if (fcuts.size() != 2 * (nbands - 1)) {
        std::cerr << "Length of f must be 2 * len (mags) - 2";
    }

    for (int i = 1; i < mags.size(); i++) {
        devs[i] = devs[i] / mags[i];
    }

    std::vector<double> f1, f2;
    for (int i = 0; i < fcuts.size(); i++) {
        if (i % 2 == 0) {
            f1.push_back(fcuts[i]);
        } else {
            f2.push_back(fcuts[i]);
        }
    }

    int n = 0;
    int min_delta = 2;

    for (int i = 0; i < f1.size(); i++) {
        if (f2[i] - f1[i] < min_delta) {
            n = i;
            min_delta = f2[i] - f1[i];
        }
    }

    float l, l1, l2;
    if (nbands == 2) {
        l = lporder(f1[n], f2[n], devs[0], devs[1]);
    } else {
        l = 0;
        for (int i = 1; i < nbands - 1; i++) {
            l1 = lporder(f1[i - 1], f2[i - 1], devs[i], devs[i - 1]);
            l2 = lporder(f1[i], f2[i], devs[i], devs[i + 1]);
            l = std::max(l, l1);
            l = std::max(l, l2);
        }
    }

    n = (int) (ceil(l) - 1);

    std::vector<double> fo;
    fo.push_back(0.0);
    for (int i = 0; i < fcuts.size(); i++) {
        fo.push_back(fcuts[i]);
    }
    fo.push_back(1.0);
    for (int i = 0; i < fo.size(); i++) {
        fo[i] *= 2;
    }

    std::vector<double> ao;
    float a;
    for (int i = 0; i < mags.size(); i++) {
        a = mags[i];
        ao.push_back(a);
        ao.push_back(a);
    }

    std::vector<double> wts;
    double max_deviations = vector_max(devs);
    for (int i = 0; i < devs.size(); i++) {
        wts[i] = max_deviations / devs[i];
    }

    std::vector<double> remez_out = gr::filter::pm_remez(n + nextra_taps, fo, ao, wts);
    return remez_out;
}

std::vector<float> low_pass(float gain, float samp_rate, float passband_end, float stopband_start,
                            float passband_ripple_db = 0.1, float stopband_atten_db = 60,
                            int nextra_taps = 2)
{
    std::vector<double> frequency_band_edges, magnitudes, max_deviations;

    float passband_dev = (std::pow(10, (passband_ripple_db / 20)) - 1) /
                         (std::pow(10, (passband_ripple_db / 20) + 1));
    float stopband_dev = std::pow(10, -stopband_atten_db / 20);

    frequency_band_edges.push_back(passband_end);
    frequency_band_edges.push_back(stopband_start);

    magnitudes.push_back(gain);
    magnitudes.push_back(0);

    max_deviations.push_back(stopband_dev);
    max_deviations.push_back(passband_dev);

    std::vector<double> remez_out =
        remezord(frequency_band_edges, magnitudes, max_deviations, nextra_taps, samp_rate);
    std::vector<float> taps = convert_vector_doubles_to_floats(remez_out);

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
    usrp_device->set_clock_source(user_args->ref_src);  // lock mboard clocks

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
    uhd::stream_args_t stream_args(STREAM_ARGS);
    stream_args.channels = channel_nums;
    uhd::rx_streamer::sptr rx_stream = usrp_device->get_rx_stream(stream_args);

    //---------------------------------------------------------------------------------------------
    //-- GNU Radio blocks
    //---------------------------------------------------------------------------------------------

    // Create top block
    gr::top_block_sptr tb = gr::make_top_block(PROGRAM_NAME);
    gr::uhd::usrp_source::sptr usrp_source =
        gr::uhd::usrp_source::make(user_args->device_addr, stream_args);
    usrp_source->set_samp_rate(user_args->sample_rate);
    usrp_source->set_center_freq(user_args->center_frequency);

    // Resample source
    std::vector<std::complex<float>> taps;  // TODO: WTF is a tap?
    taps.push_back(std::complex<float>(1, 2));
    gr::filter::rational_resampler_base_ccc::sptr resampler =
        gr::filter::rational_resampler_base_ccc::make(1, 5, taps);
    tb->connect(usrp_source, 0, resampler, 0);

    // Demodulate quadrature
    float deviation = 15e3;
    float k = user_args->channel / (2 * M_PI * deviation);
    gr::analog::quadrature_demod_cf::sptr quad_demod = gr::analog::quadrature_demod_cf::make(k);
    tb->connect(resampler, 0, quad_demod, 0);

    // Low pass filter to obtain taps
    float audio_pass = 15e3;
    float audio_stop = 16e3;
    std::vector<float> audio_taps =
        low_pass(user_args->gain, user_args->sample_rate, audio_pass, audio_stop);

    // fir_filter_fff
    gr::filter::fir_filter_fff::sptr fir_filter = gr::filter::fir_filter_fff::make(1, audio_taps);
    tb->connect(quad_demod, 0, fir_filter, 0);

    // audio sink
    gr::audio::sink::sptr audio_sink = gr::audio::sink::make(AUDIO_CARD_SAMP_RATE);
    tb->connect(fir_filter, 0, audio_sink, LEFT_CHANNEL);
    tb->connect(fir_filter, 0, audio_sink, RIGHT_CHANNEL);

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
    tb->stop();
    tb->wait();

    std::cout << std::endl << "done!" << std::endl;
    return EXIT_SUCCESS;
}

//--------------------------------------------------------------------------------------------------