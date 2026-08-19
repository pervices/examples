//
// Copyright 2024,2026 Per Vices Corporation
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

// Header
#include "../include/tcp_simple.hpp"

// Internal UHD includes
// Messages for the user
#include <iostream>
// Util for getting the time
#include "../include/system_time.hpp"

// Standard library
#include <cstring>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <poll.h>

tcp_simple::tcp_simple(const std::string& addr, const uint16_t port) {

    // Struct containing info used to generate
    struct addrinfo hint;
    memset(&hint, 0, sizeof(hint));

    // Let getaddrinfo determine whether ipv4 or ipv6
    hint.ai_family = AF_UNSPEC;
    // TCP
    hint.ai_socktype = SOCK_STREAM;
    // Speed up check by letting getaddrinfo know that it is provided a numeric ip
    hint.ai_flags = AI_NUMERICHOST;

    struct addrinfo* host_address = NULL;

    // Create string from port here to ensure it exists while c_str is in use
    std::string port_s = std::to_string(port);

    // Convert addr to a struct usable by connect, and get whether it is ipv4 or ipv6
    int addr_info_ret = getaddrinfo(addr.c_str(), port_s.c_str(), &hint, &host_address);

    if(addr_info_ret != 0) {
        std::cout << "[TCP_SIMPLE] ERROR: Failed to parse ip " << addr << "with error code: " << strerror(errno) << std::endl;

        throw std::system_error(errno, std::system_category(), "getaddrinfo(3)");
    }

    // Create tcp socket
    tcp_socket_fd = socket(host_address->ai_family, SOCK_STREAM, 0);

    if(tcp_socket_fd < 0) {
        int socket_errno = errno;

        // Free before throwing an error
        freeaddrinfo(host_address);

        std::cout << "[TCP_SIMPLE] ERROR: Creating TCP socket failed with error code: " << strerror(socket_errno) << std::endl;

        throw std::system_error(socket_errno, std::system_category(), "socket(2)");
    }

    int connect_r = connect(tcp_socket_fd, host_address->ai_addr, host_address->ai_addrlen);

    if(connect_r != 0) {
        int connect_errno = errno;

        // Close and free before throwing an error
        close(tcp_socket_fd);
        freeaddrinfo(host_address);

        std::cout << "[TCP_SIMPLE] ERROR: Connecting TCP socket failed with error code: " << strerror(connect_errno) << std::endl;

        throw std::system_error(connect_errno, std::system_category(), "connect(2)");
    }

    // Frees the host_addressults of getaddrinfo
    // NOTE: if you are editing this constructor make sure freeaddrinfo is called if an error is thrown/this constructor exits early
    freeaddrinfo(host_address);
}

tcp_simple::~tcp_simple() {
    close(tcp_socket_fd);
}

void tcp_simple::send(const void* buff, size_t size) {

    ssize_t bytes_sent = ::send(tcp_socket_fd, buff, size, 0);

    if(bytes_sent < 0) {
        std::cout << "[TCP_SIMPLE] ERROR: send failed: " << strerror(errno) << std::endl;
        throw std::system_error(errno, std::system_category(), "send(2)");

    } else if(bytes_sent < (ssize_t) size) {
        std::cout << "[TCP_SIMPLE] ERROR: Attempted to send " << size << " but only " << bytes_sent << " bytes were sent" << std::endl;

        throw std::runtime_error("Fewer bytes sent than requested");
    }

    return;
}

size_t tcp_simple::recv(void* buff, size_t size, double timeout) {

    struct pollfd  pfds[1];
    pfds[0].fd = tcp_socket_fd;
    // List of the event we want to listen for
    // POLLIN is the only event that should happen, all others should be considered errors
    pfds[0].events = POLLIN | POLLPRI | POLLRDHUP | POLLERR | POLLHUP;
    // Return value, initialize to 0 to prevent undefined errors
    pfds[0].revents = 0;

    int recv_ready;
    do {
        struct timespec ts_timeout;
        ts_timeout.tv_sec = (time_t) timeout;
        ts_timeout.tv_nsec = (long) ((timeout - static_cast<double>(ts_timeout.tv_sec)) * 1000000000);

        time_spec_t start = get_system_time();

        recv_ready = ppoll(pfds, 1, &ts_timeout, NULL);

        // ppoll completed succefully
        if(recv_ready >= 0) {
            break;

        // ppoll was interrupted, reduce timeout then try again
        } else if(errno == EINTR) {
            time_spec_t end = get_system_time();

            timeout = timeout - (start - end).get_real_secs();

        // An error occurred, continue on to error handling
        } else {
            break;
        }
    } while (true);


    if(recv_ready < 0) {
        std::cout << "[TCP_SIMPLE] ERROR: Error from ppoll while waiting for packet: " << strerror(errno) << std::endl;

        throw std::system_error(errno, std::generic_category(), "Error during ppoll when waiting for packet(s)");
    } else if(recv_ready == 0) {
        // No packet was ready
        return 0;
    }

    int recv_attempts = 0;
    ssize_t bytes_received;
    do {
        bytes_received = ::recv(tcp_socket_fd, buff, size, MSG_DONTWAIT);

        recv_attempts++;

    // Repeat if EINTR was encountered up to 10 times
    } while(recv_attempts < 10 && (bytes_received < 0 && errno == EINTR));

    if(bytes_received < 0) {
        // This should be impossible because ppoll, but is included just in case to ensure the program doesn't freeze
        if(errno == EAGAIN || errno == EWOULDBLOCK) {
            std::cout << "[TCP_SIMPLE] ERROR: ppoll indicated data ready but no data was found" << std::endl;
        } else {
            // All other errors shouldn't happen
            std::cout << "[TCP_SIMPLE] ERROR: recv failed with: " << strerror(errno) << std::endl;
        }

        throw std::system_error(errno, std::generic_category(), "recv(2)");

    // The SDR should never be the one to close the connection. A closed connection indicates something went very wrong
    } else if(bytes_received == 0) {
        std::cout << "[TCP_SIMPLE] ERROR: TCP connection unexpectedly closed the connection" << std::endl;
        throw std::runtime_error("connection closed");

    // recv successful
    } else {
        return static_cast<size_t>(bytes_received);
    }
}
