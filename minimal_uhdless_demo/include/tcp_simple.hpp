//
// Copyright 2026 Per Vices Corporation
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#pragma once

#include <string>
#include <stdint.h>

/**
 * A simple manager for using a tcp socket
 */
class tcp_simple {

private:

    // File descriptor of the tcp socket
    int tcp_socket_fd = -1;

public:

    /**
     * Make a new TCP connection
     *
     * @param addr The IP address to connect to
     * @param port The port to connect to
     *
     * @throws std::system_error getaddrinfo, socket, or connect failed
     */
    tcp_simple(const std::string& addr, const uint16_t port);

    /**
     * Close the TCP connection
     */
    ~tcp_simple();

    /**
     * Sends a TCP packet
     *
     * @param buff A buffer contianing the payload to send
     * @param size The number of bytes to send
     *
     * @throws std::system_error An error occured during send(2)
     * @throws std::runtime_error Fewer bytes were send during send than intended but it reported success. This is defined as an exception so that callers cannot get undefined behaviour from assuming the program worked
     */
    void send(const void* buff, size_t size);

    /**
     * Receives a TCP packet
     *
     * @param buff The buffer to receive into
     * @param size The size of the buffer
     * @param timeout How long to wait before timing out in seconds
     *
     * @return The number of bytes received, or 0 in the event of a timeout
     *
     * @throws std::system_error Error during ppoll (waiting for packet), or recv(2)
     * @throw std::runtime_error The host closed the connection, or size was 0. This is an error because this class assumes that only it (the client) will close the connection
     */
    size_t recv(void* buff, size_t size, double timeout = 0.1);
};
