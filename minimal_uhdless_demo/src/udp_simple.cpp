//
// Copyright 2010-2011,2014 Ettus Research LLC
// Copyright 2018 Ettus Research, a National Instruments Company
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "../include/udp_simple.hpp"
#include <iostream>
#include <stdexcept>
#include <poll.h>
// asio::io_context and asio::ip::udp (formerly pulled in transitively via
// uhdlib/transport/udp_common.hpp -> uhdlib/asio.hpp -> <boost/asio.hpp>)
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/udp.hpp>

using namespace uhd::transport;
namespace asio = boost::asio;

/*!
 * Wait for the socket to become ready for a receive operation.
 * (formerly uhd::transport::wait_for_recv_ready(), from uhdlib/transport/udp_common.hpp;
 * trimmed to the poll()-based path, since the select()-based UHD_PLATFORM_WIN32 path
 * will never be defined here, and to the 2-arg form, since it's the only one used)
 * \param sock_fd the open socket file descriptor
 * \param timeout_ms the timeout duration in milliseconds
 * \return true when the socket is ready for receive
 */
static bool wait_for_recv_ready(int sock_fd, int32_t timeout_ms)
{
    pollfd pfd_read;
    pfd_read.fd     = sock_fd;
    pfd_read.events = POLLIN;

    // call poll with timeout on receive socket
    // poll will return the number of socket events (i.e. data received) if successful, 0 if it timed out and -1 on error
    int poll_result = ::poll(&pfd_read, 1, (int)timeout_ms);
    return poll_result > 0;
}

/***********************************************************************
 * UDP simple implementation: connected and broadcast
 **********************************************************************/
class udp_simple_impl : public udp_simple
{
public:
    udp_simple_impl(
        const std::string& addr, const std::string& port, bool bcast, bool connect)
        : _connected(connect)
    {
        std::cout << "[UDP] TRACE: Creating udp transport for " << addr << " " << port << std::endl;

        // resolve the address
        asio::ip::udp::resolver resolver(_io_context);
        _send_endpoint = *resolver
                              .resolve(asio::ip::udp::v4(),
                                  addr,
                                  port,
                                  asio::ip::resolver_query_base::all_matching)
                              .begin();

        // create and open the socket
        socket_fd = ::socket(AF_INET, SOCK_DGRAM, 0);

        // allow broadcasting
        int broadcast_enable = bcast;
        setsockopt(socket_fd, SOL_SOCKET, SO_BROADCAST, &broadcast_enable, sizeof(broadcast_enable));

        // connect the socket
        if (connect) {
            struct sockaddr_in dst_address;
            dst_address.sin_family = AF_INET;
            std::string ipv4_addr = _send_endpoint.address().to_string();
            dst_address.sin_addr.s_addr = inet_addr(ipv4_addr.c_str());
            dst_address.sin_port = htons(_send_endpoint.port());

            int r = ::connect(socket_fd, (struct sockaddr*)&dst_address, sizeof(dst_address));

            if(r) {
                throw std::runtime_error("Failed to connect send socket for control packets. Error code:" + std::string(strerror(errno)));
            }
        }

        // Sets socket priority to minimum
        int priority = 0;
        setsockopt(socket_fd, SOL_SOCKET, SO_PRIORITY, &priority, sizeof(priority));
    }

    ~udp_simple_impl(void) {
        close(socket_fd);
    }

    size_t send(const void* buff, size_t count) override
    {
        if (_connected) {
            // MSG_CONFIRM to avoid uneccessary control packets being sent to verify the destination is where it already is
            ssize_t data_sent = ::send(socket_fd, buff, count, MSG_CONFIRM & route_good);
            if(data_sent == -1) {
                std::cout << "[UDP] ERROR: Attempt to send UDP control packet failed with: " << strerror(errno) << std::endl;
                return 0;
            } else {
                return data_sent;
            }
        }

        struct sockaddr_in dst_address;
        memset(&dst_address, 0, sizeof(dst_address));
        dst_address.sin_family = AF_INET;
        std::string ipv4_addr = _send_endpoint.address().to_string();
        dst_address.sin_addr.s_addr = inet_addr(ipv4_addr.c_str());
        dst_address.sin_port = htons(_send_endpoint.port());

        ssize_t ret = sendto(socket_fd, buff, count, MSG_CONFIRM & route_good, (struct sockaddr*)&dst_address, sizeof(dst_address));

        if(ret > 0) {
            return ret;
        } else {
            std::cout << "[UDP] ERROR: Attempt to sendto UDP control packet failed with: " << strerror(errno) << std::endl;
            // Return 0 to keep behaviour from asio
            return 0;
        }
    }

    size_t recv(void* buff, size_t size, double timeout) override
    {
        const int32_t timeout_ms = static_cast<int32_t>(timeout * 1000);

        if (not wait_for_recv_ready(socket_fd, timeout_ms)) {
            return 0;
        }
        ssize_t data_received = 0;
        if(_connected) {
            // MSG_DONTWAIT since wait_for_recv_ready will already wait for data to be ready. If this would block something has gone wrong and return to avoid blocking
            data_received = ::recv(socket_fd, buff, size, MSG_DONTWAIT);
            if(data_received == -1) {
                std::cout << "[UDP] ERROR: Attempt to recv UDP control packet failed with: " << strerror(errno) << std::endl;
                data_received = 0;
            }
        } else {
            struct sockaddr_in src_address;
            memset(&src_address, 0, sizeof(src_address));
            uint32_t addr_len = sizeof(src_address);

            data_received = ::recvfrom(socket_fd, buff, size, MSG_DONTWAIT, (struct sockaddr*)&src_address, &addr_len);
            if(data_received == -1) {
                std::cout << "[UDP] ERROR: Attempt to recvfrom UDP control packet failed with: " << strerror(errno) << std::endl;
                data_received = 0;
            }

            recv_ip = inet_ntoa(src_address.sin_addr);
        }

        // If data has been received, then we know routing is good
        if(data_received) {
            route_good = ~0;
        } else {
            route_good = 0;
        }

        return (size_t)data_received;
    }

    std::string get_recv_addr(void) override
    {
        return recv_ip;
    }

    std::string get_send_addr(void) override
    {
        return _send_endpoint.address().to_string();
    }

private:
    bool _connected;
    asio::io_context _io_context;
    asio::ip::udp::endpoint _send_endpoint;
    // IP address received packets originated from
    std::string recv_ip = "0.0.0.0";
    // Set to 0 until a packet has been received, ~0 once a packet has been received
    // If this has been confirmed send can be called with MSG_CONFIRM
    int route_good = 0;
    int socket_fd;
};

udp_simple::~udp_simple(void)
{
    /* NOP */
}

/***********************************************************************
 * UDP public make functions
 **********************************************************************/
udp_simple::sptr udp_simple::make_connected(
    const std::string& addr, const std::string& port)
{
    return sptr(new udp_simple_impl(addr, port, false, true /* no bcast, connect */));
}
