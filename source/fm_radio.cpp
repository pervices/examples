#include <boost/format.hpp>
#include <boost/program_options.hpp>
#include <chrono>
#include <thread>
#include <uhd/exception.hpp>
#include <uhd/types/tune_request.hpp>
#include <uhd/usrp/multi_usrp.hpp>
#include <uhd/utils/safe_main.hpp>
#include <uhd/utils/thread.hpp>

const int msec_per_sec = 1e3;
const int audio_samp_rate = 48e3;

namespace po = boost::program_options;

void print_usage()
{
    std::cout << "Usage: fm_radio [OPTIONS] ..." << std::endl
              << "Receives frequency modulated signals from the specified station." << std::endl
              << std::endl
              << "Options:" << std::endl // TODO
              << "\t -a (device args)" << std::endl
              << "\t -f (frequency in Hz)" << std::endl
              << "\t -r (sample rate in Hz)" << std::endl
              << "\t -g (gain)" << std::endl
              << "\t -n (number of samples to receive)" << std::endl
              << "\t -h (print this help message)" << std::endl
              << std::endl
              << "Examples:" << std::endl
              << "\t fm_radio --help" << std::endl // TODO
              << std::endl;
}

int set_program_args(int argc, char *argv[])
{
    std::string args, ant, ref_src;
    double rate, freq, gain, setup_time;
    size_t channel;

    po::options_description desc("Allowed options");
    // clang-format off
    desc.add_options()
      ("help", "help message")
      ("args", po::value<std::string>(&args)->default_value(""), "multi uhd device address args")
      ("channel", po::value<size_t>(&channel)->default_value(0), "which channel to use")
      ("freq", po::value<double>(&freq)->default_value(0.0), "RF center frequency in Hz")
      ("gain", po::value<double>(&gain)->default_value(1.0), "gain for the RF chain")
      ("rate", po::value<double>(&rate)->default_value(1e6), "rate of incoming samples")
      ("ref_src", po::value<std::string>(&ref_src)->default_value("internal"),
                  "reference source (internal, external, mimo)")
      ("setup", po::value<double>(&setup_time)->default_value(1.0), "seconds of setup time")
    ;
    // clang-format on
    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    std::cout << std::endl;

    if (vm.count("help") || argc <= 1) {
        print_usage();
        return ~0;
    }

    // === Instantiate crimson device ===
    std::cout << boost::format("Instantiating the usrp crimson device with: %s...") % args
              << std::endl;
    uhd::usrp::multi_usrp::sptr crimson = uhd::usrp::multi_usrp::make(args);
    crimson->set_clock_source(ref_src); // lock mboard clocks

    // === Set the sample rate ===
    double actual_rate;
    if (rate <= 0.0) {
        std::cerr << boost::format("%s is not a valid sample rate.")
                  << "The sample rate needs to be >= 0.0"
                  << "Please specify a valid sample rate." << std::endl;
        return ~0;
    }
    std::cout << boost::format("Setting RX Rate: %f Msps...") % (rate / 1e6) << std::endl;
    crimson->set_rx_rate(rate, channel);
    actual_rate = crimson->get_rx_rate(channel) / 1e6;
    std::cout << boost::format("Actual RX Rate: %f Msps...") % actual_rate << std::endl;

    // === Set the rf gain ===
    double actual_gain;
    if (vm.count("gain")) {
        std::cout << boost::format("Setting RX Gain: %f dB...") % gain << std::endl;
        crimson->set_rx_gain(gain, channel);
        actual_gain = crimson->get_rx_gain(channel);
        std::cout << boost::format("Actual RX Gain: %f dB...") % actual_gain << std::endl;
    }

    // sleep a bit while the slave locks its time to the master
    std::this_thread::sleep_for(std::chrono::milliseconds(int64_t(setup_time * msec_per_sec)));

    return 0;
}

int UHD_SAFE_MAIN(int argc, char *argv[])
{
    uhd::set_thread_priority_safe();

    if (set_program_args(argc, argv) != 0) {
        return ~0;
    }

    std::cout << std::endl << "Done!" << std::endl;
    return EXIT_SUCCESS;
}
