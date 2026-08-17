//
// Copyright 2022, 2024 Per Vices Corporation
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//

#ifndef INCLUDED_PV_IFACE_HPP
#define INCLUDED_PV_IFACE_HPP

// Standard library
#include <string>
#include <mutex>
#include <vector>

// Internal UHD includes
#include "udp_simple.hpp"
#include "tcp_simple.hpp"
#include "time_spec.hpp"

// TODO: replace with enums belonging to the class
#define CMD_SUCCESS 	'0'
#define CMD_ERROR	'1'

/*!
 * Interface class: for communicating with the server on Per Vices products
 * Provides a set of functions access the state tree
 */
class pv_iface
{
public:
    static constexpr size_t MAX_MTU_SIZE = 9000;

    /**
     * Make a new pv_iface with the control transport.
     * @param addr The IP to connect to.
     * @param udp_port The UDP port the server is listening to.
     */
    pv_iface(const std::string& addr, const uint16_t udp_port);

    // Helper functions to wrap peek_str and poke_str as get and set
    //
    //
    //

    std::string get_string(std::string req);
    void set_string(const std::string pre, std::string data);

    // wrapper for type <double> through the ASCII Crimson interface
    double get_double(std::string req);
    void set_double(const std::string pre, double data);

    // wrapper for type <bool> through the ASCII Crimson interface
    bool get_bool(std::string req);
    void set_bool(const std::string pre, bool data);

    // wrapper for type <int> through the ASCII Crimson interface
    int get_int(std::string req);
    void set_int(const std::string pre, int data);

    // wrapper for type <time_spec_t> through the ASCII Crimson interface
    time_spec_t get_time_spec(std::string req);
    void set_time_spec(const std::string pre, time_spec_t data);

    ~pv_iface();

private:

    // Mutex for controlling access to the management port
    std::mutex _iface_lock;

    // Send/write a data packet (string), null terminated
    void poke_str(std::string data);

    // Recieve/read a data packet (string), null terminated
    std::string peek_str(void);

    // Recieve/read a data packet (string), null terminated
    std::string peek_str( float timeout_s );

    // UDP socket pointed at the SDR
    udp_simple::sptr udp_transport;

    // TCP connection to the server
    tcp_simple* tcp_connection = nullptr;

    // internal function for tokenizing the inputs
    void parse(std::vector<std::string> &tokens, char* data, size_t const data_len, const char delim);

    //used in send/recv
    uint32_t _ctrl_seq_num;
    uint32_t _protocol_compat;

    // buffer for in and out
    char _buff[ MAX_MTU_SIZE ];
};

#endif /* INCLUDED_PV_IFACE_HPP */
