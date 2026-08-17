#include <iostream>
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
    size_t max_samples_per_packet = /*8912 is the maximum bytes samples can take up. sc16 are 4 bytes each*/ 8912 / 4;
    int64_t buffer_size = (int64_t) management_iface.get_double("system/get_max_buffer_level");
    std::vector<std::string> dst_ips = { "10.10.10.2" };
    std::vector<int> dst_ports = { 42836 };
    // Tick rate of the device's clock. 250MHz for Cyan, 325MHz or 300MHz for Crimson
    double tick_rate = 250000000;
    // Packets must be a multiple of a certain length on Cyan depending on the FPGA version
    // You can get it dynamically on Cyan
    // Check CRIMSON_TNG_PACKET_NSAMP_MULTIPLE in https://github.com/pervices/uhd if porting this to Crimson
    int packet_size_multiple = management_iface.get_int("system/nsamps_multiple_tx");

    // Calculated the time of the device a packet sent now would arrive at
    std::shared_ptr<clock_sync> clock_sync_manager =  clock_sync::make("10.10.10.2", 42809, tick_rate);

    send_packet_handler_mmsg send_manager = 
        send_packet_handler_mmsg(
            channels,
            max_samples_per_packet,
            /* Use string since to get since it may exceed the size of int*/ buffer_size,
            dst_ips,
            dst_ports,
            /* Target buffer level */ (int64_t) (buffer_size * 0.5),
            packet_size_multiple,
            tick_rate,
            /* Always use little endian data to match the host*/ true,
            clock_sync_manager
        );

    /* NOTE:
     * certain hardware requires packets to be a multiple of a certain length
     * You can get that from system/nsamps_multiple_tx. I will hardcode it for now
    */


    

    return 0;
}