#include <boost/format.hpp>
#include <boost/program_options.hpp>
#include <chrono>
/* #include <complex> */
/* #include <csignal> */
/* #include <fstream> */
/* #include <iostream> */
#include <thread>
#include <uhd/exception.hpp>
#include <uhd/types/tune_request.hpp>
#include <uhd/usrp/multi_usrp.hpp>
#include <uhd/utils/safe_main.hpp>
#include <uhd/utils/thread.hpp>

const int msec_per_sec = 1e3;

namespace po = boost::program_options;

int UHD_SAFE_MAIN(int argc, char *argv[])
{
    uhd::set_thread_priority_safe();

    // variables to be set by po
    std::string args, ant, ref;
    double rate, freq, gain, setup_time;
    size_t channel;

    // === setup the program options
    po::options_description desc("Allowed options");
    // clang-format off
    desc.add_options()
      ("help", "help message")
      ("args", po::value<std::string>(&args)->default_value(""), "multi uhd device address args")
      ("channel", po::value<size_t>(&channel)->default_value(0), "which channel to use")
      ("freq", po::value<double>(&freq)->default_value(0.0), "RF center frequency in Hz")
      ("gain", po::value<double>(&gain)->default_value(1.0), "gain for the RF chain")
      ("rate", po::value<double>(&rate)->default_value(1e6), "rate of incoming samples")
      ("ref", po::value<std::string>(&ref)->default_value("internal"), "reference source (internal, external, mimo)")
      ("setup", po::value<double>(&setup_time)->default_value(1.0), "seconds of setup time")
    ;
    // clang-format on
    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    // === print the help message
    if (vm.count("help")) {
        std::cout << boost::format("This is a help message %s") % args << std::endl;
        return ~0;
    }

    // === create a usrp device
    std::cout << std::endl;
    std::cout << boost::format("Creating the usrp device with: %s...") % args << std::endl;
    uhd::usrp::multi_usrp::sptr usrp = uhd::usrp::multi_usrp::make(args);

    // Lock mboard clocks
    usrp->set_clock_source(ref);

    double actual_gain, actual_rate;

    // set the sample rate
    if (rate <= 0.0) {
        std::cerr << "Please specify a valid sample rate" << std::endl;
        return ~0;
    }
    std::cout << boost::format("Setting RX Rate: %f Msps...") % (rate / 1e6) << std::endl;
    usrp->set_rx_rate(rate, channel);
    actual_rate = usrp->get_rx_rate(channel) / 1e6;
    std::cout << boost::format("Actual RX Rate: %f Msps...") % actual_rate << std::endl;

    // set the rf gain
    if (vm.count("gain")) {
        std::cout << boost::format("Setting RX Gain: %f dB...") % gain << std::endl;
        usrp->set_rx_gain(gain, channel);
        actual_gain = usrp->get_rx_gain(channel);
        std::cout << boost::format("Actual RX Gain: %f dB...") % actual_gain << std::endl;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(int64_t(setup_time * msec_per_sec)));

    std::cout << std::endl << "Done!" << std::endl;
    return EXIT_SUCCESS;
}
