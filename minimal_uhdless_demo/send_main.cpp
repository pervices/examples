#include <iostream>
#include <complex>

#include "pv_iface.hpp"
#include "clock_sync.hpp"
#include "super_send_packet_handler_mmsg.hpp"

int main() {
    std::cout << "Starting" << std::endl;

    // Create an instance of the class to manage communication with the management port
    pv_iface management_iface = pv_iface("192.168.10.2", 42799);

    std::cout << "Created connection to management port" << std::endl;

    // Enable channel
    management_iface.set_int("tx/a/pwr", 1);

    // Set samples (not VRT header, just samples) to little endian to match the host
    management_iface.set_int("tx/a/link/endian_swap", 1);

    // Enable respecting VRT header
    management_iface.set_int("tx/a/link/vita_en", 1);

    // Reset tx dsp to clean up artifacts from earlier
    management_iface.set_int("tx/a/dsp/rstreq", 1);

    // TODO: set gain and frequency

    std::vector<size_t> channels = {0};
    // TODO: adjust limit for Crimson
    size_t max_samples_per_packet = /*8912 is the maximum bytes samples can take up. sc16 are 4 bytes each*/ 8912 / 4;
    int64_t buffer_size = (int64_t) management_iface.get_double("system/get_max_buffer_level");
    std::vector<std::string> dst_ips = { "10.10.10.2" };
    std::vector<int> dst_ports = { 42836 };
    // Tick rate of the device's clock. 250MHz for Cyan, 325MHz or 300MHz for Crimson
    double tick_rate = 250000000;
    // Packets must be a multiple of a certain length on Cyan depending on the FPGA version
    // You can get it dynamically on Cyan
    // Check CRIMSON_TNG_PACKET_NSAMP_MULTIPLE in https://github.com/pervices/uhd if porting this to Crimson
    size_t packet_size_multiple = static_cast<size_t>(management_iface.get_int("system/nsamps_multiple_tx"));

    // Calculated the time of the device a packet sent now would arrive at
    std::shared_ptr<clock_sync> clock_sync_manager =  clock_sync::make("10.10.10.2", 42809, tick_rate);

    send_packet_handler_mmsg send_manager = 
        send_packet_handler_mmsg(
            channels,
            max_samples_per_packet,
            /* Use string since to get since it may exceed the size of int*/ buffer_size,
            dst_ips,
            dst_ports,
            /* Target buffer level */ (int64_t) (static_cast<double>(buffer_size) * 0.5),
            packet_size_multiple,
            tick_rate,
            clock_sync_manager
        );

    
    management_iface.set_double("tx/a/dsp/rate", 10e6);
    double actual_sample_rate = management_iface.get_double("tx/a/dsp/rate");
    std::cout << "Actual sample rate: " << actual_sample_rate << std::endl;
    send_manager.set_samp_rate(actual_sample_rate);

    // Vector of samples long enough to send up to 10 packets of samples at a time
    std::vector<std::complex<short>> samples((size_t) max_samples_per_packet * 10, 0);

    // Pointers to the start of the sample buffer
    // The streamer tacks an arrays of pointers to the buffers of samples for each channel
    std::vector<const void* > sample_ptrs(1, samples.data());

    // Wait for clock (predicting the time of arrival of packets) to complete
    clock_sync_manager->wait_for_sync();

    time_spec_t send_start_time = clock_sync_manager->get_device_time() + 10.0;

    tx_metadata_t md;
    md.start_of_burst = true;
    md.end_of_burst   = false;
    md.has_time_spec  = true;
    md.time_spec = send_start_time;

    std::cout << "Starting sending samples\n";

    size_t samples_sent = 0;
    // Send 10 seconds of samples
    size_t samples_to_send = (size_t) actual_sample_rate * 10;

    while(samples_sent < samples_to_send) {
        size_t samples_this_send = std::min(samples.size(), samples_to_send - samples_sent);
        samples_sent += send_manager.send(sample_ptrs, samples_this_send, md, 5);

        md.start_of_burst = false;
        md.has_time_spec = false;
    }

    std::cout << "Sent " << samples_sent << " samples\n";

    return 0;
}