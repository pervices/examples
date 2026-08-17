#include <iostream>
#include "pv_iface.hpp"

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

    /* NOTE:
     * certain hardware requires packets to be a multiple of a certain length
     * You can get that from system/nsamps_multiple_tx. I will hardcode it for now
    */

    return 0;
}