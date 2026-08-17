//
// Copyright 2014-2015 Per Vices Corporation
// Copyright 2022, 2025 Per Vices Corporation
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

#include <stdexcept>
#include <inttypes.h>
#include <iostream>
#include <cmath>
#include <cstring>
#include "../include/pv_iface.hpp"

#define PV_IFACE_DEBUG_NAME_C "PV_IFACE"

static uint32_t seq = 1;

/***********************************************************************
 * Structors
 **********************************************************************/
pv_iface::pv_iface(const std::string& addr, const uint16_t udp_port)
{
    memset( _buff, '\0', sizeof( _buff ) );

    // Initialize the UDP connection with the server
    udp_transport = udp_simple::make_connected(addr, std::to_string(udp_port));

    // Get the management port used to ask for a TCP connection
    int tcp_port;
    try {
        // Update the property in their respective impl.cpp files if this is changed
        tcp_port = get_int("system/tcp_management_port");
    } catch(const std::out_of_range& e) {
        // The server is from before TCP was added, skip creating the connection
        tcp_connection = nullptr;
        return;
    }

    if(tcp_port < 0 || tcp_port > 65535) {
        throw std::invalid_argument("Invalid tcp management IP: " + std::to_string(tcp_port));
    }

    tcp_connection = new tcp_simple(addr, (uint16_t) tcp_port);
}

pv_iface::~pv_iface() {
    // Close the TCP connection if it was created
    if(tcp_connection != nullptr) {
        delete tcp_connection;
    }
}

/***********************************************************************
 * Peek and Poke
 **********************************************************************/
// Never call this function by itself, always call through pv_iface::get/set()
// else it will mess up the protocol with the sequencing and will contian no error checks.
void pv_iface::poke_str(std::string data) {
    // populate the command string with sequence number
    data = data.insert(0, (std::to_string(seq++) + ","));

    // Send data using UDP if the TCP connection in uninitilized
    if(tcp_connection == nullptr) [[unlikely]] {
        udp_transport->send( data.c_str(), data.length() );
    } else {
        tcp_connection->send( data.c_str(), data.length() );
    }
    return;
}

// Never call this function by itself, always call through pv_iface::get/set(),
// else it will mess up the protocol with the sequencing and will contian no error checks.
// Format: <sequence number>,<error code>,<data>
std::string pv_iface::peek_str( float timeout_s ) {
    uint32_t iseq;
    std::vector<std::string> tokens;
    uint8_t tries = 0;
    uint8_t num_tries = 5;

    do {
        // clears the buffer and receives the message
        memset( _buff, 0, sizeof( _buff ) );

        size_t nbytes;
        // Receive data from UDP if the TCP connection in uninitilized
        if(tcp_connection == nullptr) [[unlikely]] {
            nbytes = udp_transport -> recv(_buff, MAX_MTU_SIZE, timeout_s );
        } else {
            nbytes = tcp_connection->recv(_buff, MAX_MTU_SIZE, timeout_s);
        }

        if (nbytes == 0) return "TIMEOUT";

        // parses it through tokens: seq, status, [data]
        this -> parse(tokens, _buff, MAX_MTU_SIZE, ',');

        // Malformed packet
        if (tokens.size() < 2) {
            return "ERROR";
        // Error while reading property
        } else if(tokens[1].c_str()[0] == CMD_ERROR) {
            return "GET_ERROR";
        }

        // if seq is incorrect, return an error
        sscanf(tokens[0].c_str(), "%" SCNd32, &iseq);

    } while(iseq != seq - 1 && tries++ < num_tries);

    // exits with an error if can't find a matching sequence
    if (tries == num_tries) return "INVLD_SEQ";

    // Return the message (tokens[2])
    // If the error checking passed and tokens is size 2, then the intended data is "" (since parse will not add "" if the packets ends with a ","
    if(tokens.size() < 3) {
        return "";
    } else {
        return tokens[2];
    }
}

std::string pv_iface::peek_str() {
    return peek_str( 8 );
}

// Gets a property on the device
std::string pv_iface::get_string(std::string req) {

    std::lock_guard<std::mutex> _lock( _iface_lock );

    // Send the get request
    poke_str("get," + req);

    // peek (read) back the data
    std::string ret = peek_str();

    if(ret == "GET_ERROR") {
        throw std::out_of_range("pv_iface::get_string - Unable to read property on the server: " + req + "\nPlease Verify that the server is up to date");
    }
    else if (ret == "TIMEOUT") {
        throw std::runtime_error("pv_iface::get_string - UDP resp. timed out: get: " + req);
    }
    else  if(ret == "ERROR") {
        throw std::runtime_error("pv_iface::get_string - UDP unpecified error: " + req);
    }
    else {
        return ret;
    }
}
// Sets a property on the device
void pv_iface::set_string(const std::string pre, std::string data) {

	std::lock_guard<std::mutex> _lock( _iface_lock );

	// Send the set request
	poke_str("set," + pre + "," + data);

	// peek (read) anyways for error check, since Crimson will reply back
	std::string ret = peek_str();

    if(ret == "GET_ERROR") {
        throw std::out_of_range("pv_iface::set_string - Unable to read property on the server: " + pre + "\nPlease Verify that the server is up to date");
    }
    else if (ret == "TIMEOUT") {
        throw std::runtime_error("pv_iface::set_string - UDP resp. timed out: set: " + pre + " = " + data);
    }
    else  if(ret == "ERROR") {
        throw std::runtime_error("pv_iface::set_string - UDP unpecified error: " + pre);
    }
    else {
        return;
    }
}

// wrapper for type <double> through the ASCII Crimson interface
double pv_iface::get_double(std::string req) {
    try { return std::stod( get_string(req) );
    } catch(std::exception &e) {
        std::cout << "[" << PV_IFACE_DEBUG_NAME_C << "] WARNING: Failed to get double property: " << e.what() << std::endl;
    }
    return 0;
}
void pv_iface::set_double(const std::string pre, double data){
    set_string(pre, std::to_string(data));
}

// wrapper for type <bool> through the ASCII Crimson interface
bool pv_iface::get_bool(std::string req) {
    try {
        int x = std::stoi(req);
        return x != 0;
    } catch(std::exception &e) {
        std::cout << "[" << PV_IFACE_DEBUG_NAME_C << "] WARNING: Failed to get bool property: " << e.what() << std::endl;
    }
    return 0;
}
void pv_iface::set_bool(const std::string pre, bool data){
    set_string(pre, std::to_string(data));
}

// wrapper for type <int> through the ASCII Crimson interface
int pv_iface::get_int(std::string req) {
	try { return std::stoi( get_string(req) );
    } catch(std::exception &e) {
        std::cout << "[" << PV_IFACE_DEBUG_NAME_C << "] WARNING: Failed to get int property: " << e.what() << std::endl;
    }
    return 0;
}
void pv_iface::set_int(const std::string pre, int data){
    set_string(pre, std::to_string(data));
}

// we should get back time in the form "12345.6789" from Crimson, where it is seconds elapsed relative to Crimson bootup or the time set this boot
// NOTE: use <device>_impl::get_time_now for anything that requires precision
time_spec_t pv_iface::get_time_spec(std::string req) {
    // Get time via management port
    double fracpart, intpart;
    fracpart = modf(get_double(req), &intpart);
    time_spec_t temp = time_spec_t((time_t)intpart, fracpart);
    return temp;
}

void pv_iface::set_time_spec( const std::string pre, time_spec_t value ) {
    set_double(pre, value.get_real_secs());
}

/***********************************************************************
 * Helper Functions
 **********************************************************************/
void pv_iface::parse(std::vector<std::string> &tokens, char* data, size_t const data_len, const char delim) {
    size_t i = 0;
    // Ensure the vector the result is stored in is clean
    tokens.clear();

    // Perform bounds check for if the buffer to parse is 0 length
    if(i >= data_len) {
        return;
    }

    while (data[i]) {
        std::string token = "";

        // While in quotes ignore the comma seperator
        bool in_quotes = false;

        while (data[i] && (data[i] != delim || in_quotes)) {

            if(data[i] == '"') {
                // Toggle whether or not we are in quotes (and whether or not delim should be ignored)
                in_quotes = !in_quotes;
            } else {
                // Add non quote characters to the token
                token.push_back(data[i]);
            }
            if (data[i+1] == 0 || (data[i+1] == delim && !in_quotes) || i + 1 >= data_len) {
                tokens.push_back(token);
            }
            i++;
            // Perform the bounds checking before the while loop check to avoid attempting to read past the end of the buffer in the rest of the condition
            if(i >= data_len) {
                break;
            }
        }
        i++;
        if(i >= data_len) {
            break;
        }
    }
    return;
}
