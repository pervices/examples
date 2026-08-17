//
// Copyright 2010,2014 Ettus Research LLC
// Copyright 2018 Ettus Research, a National Instruments Company
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#pragma once

#include <cstddef>
#include <memory>
#include <string>

class udp_simple
{
public:
    typedef std::shared_ptr<udp_simple> sptr;

    virtual ~udp_simple(void) = 0;

    /*!
     * Make a new connected udp transport:
     * This transport is for sending and receiving
     * between this host and a single endpoint.
     * The primary usage for this transport will be control transactions.
     * The underlying implementation is simple and portable (not fast).
     *
     * The address will be resolved, it can be a host name or ipv4.
     * The port will be resolved, it can be a port type or number.
     *
     * \param addr a string representing the destination address
     * \param port a string representing the destination port
     */
    static sptr make_connected(const std::string& addr, const std::string& port);

    /*!
     * Send a single buffer.
     * Blocks until the data is sent.
     * \param buff a pointer to the buffer containing data to send
     * \param count number of bytes to send
     * \return the number of bytes sent
     */
    virtual size_t send(const void* buff, size_t count) = 0;

    /*!
     * Receive into the provided buffer.
     * Blocks until data is received or a timeout occurs.
     * \param buff pointer to the buffer to write data in
     * \param size size of the buffer in bytes
     * \param timeout the timeout in seconds
     * \return the number of bytes received or zero on timeout
     */
    virtual size_t recv(
        void* buff, size_t size, double timeout = 0.1) = 0;

    /*!
     * Get the last IP address as seen by recv().
     * Only use this with the broadcast socket.
     */
    virtual std::string get_recv_addr(void) = 0;

    /*!
     * Get the IP address for the destination
     */
    virtual std::string get_send_addr(void) = 0;
};
