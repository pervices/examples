#include <iostream>
#include "pv_iface.hpp"

int main() {
    std::cout << "Starting" << std::endl;

    // Create an instance of the class to manage communication with the management port
    uhd::pv_iface management_iface = uhd::pv_iface("192.168.10.2", 42799);

    std::cout << "Created connection to management port" << std::endl;
    return 0;
}