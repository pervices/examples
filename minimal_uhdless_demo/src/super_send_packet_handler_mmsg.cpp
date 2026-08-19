//
// Copyright 2023-2024 Per Vices Corporation
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "../include/super_send_packet_handler_mmsg.hpp"
#include <vector>

// Smart pointers
#include <memory>

#include <cmath>

// For printing error messages
#include <iostream>
#include <stdexcept>
#include <cstring>

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <unistd.h>

send_packet_handler_mmsg::send_packet_handler_mmsg(const std::vector<size_t>& channels, size_t max_samples_per_packet, const int64_t device_buffer_size, std::vector<std::string>& dst_ips, std::vector<int>& dst_ports, int64_t device_target_nsamps, size_t device_packet_nsamp_multiple, double tick_rate, std::shared_ptr<clock_sync> clock_sync_info_owner)
    // Ensure max_samples_per_packet is a multiple of the number of samples allowed per packet
    :
    _DEVICE_TARGET_NSAMPS(device_target_nsamps),
    _DEVICE_PACKET_NSAMP_MULTIPLE(device_packet_nsamp_multiple),
    _max_samples_per_packet((max_samples_per_packet / device_packet_nsamp_multiple) * device_packet_nsamp_multiple),
    _MAX_SAMPLE_BYTES_PER_PACKET(_max_samples_per_packet * _BYTES_PER_SAMPLE),
    _TICK_RATE(tick_rate),
    _DEVICE_BUFFER_SIZE(device_buffer_size),
    _NUM_CHANNELS(channels.size()),
    _clock_sync(clock_sync_info_owner.get()),
    _clock_sync_owner(clock_sync_info_owner)
{
    ch_send_buffer_info_group = std::vector<ch_send_buffer_info>(_NUM_CHANNELS, ch_send_buffer_info(0, HEADER_SIZE, _BYTES_PER_SAMPLE * (_DEVICE_PACKET_NSAMP_MULTIPLE - 1), _DEVICE_TARGET_NSAMPS, _sample_rate));

    // Creates and binds to sockets
    for(size_t n = 0; n < _NUM_CHANNELS; n++) {
        struct sockaddr_in dst_address;
        int send_socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
        if(send_socket_fd < 0) {
            throw std::runtime_error( "Failed to create send socket. Error code:" + std::string(strerror(errno)));
        }

        dst_address.sin_family = AF_INET;
        dst_address.sin_addr.s_addr = inet_addr(dst_ips[n].c_str());
        dst_address.sin_port = htons(dst_ports[n]);

        if(connect(send_socket_fd, (struct sockaddr*)&dst_address, sizeof(dst_address)) < 0)
        {
            fprintf(stderr, "ERROR Unable to connect to IP address %s and port %i\n", dst_ips[n].c_str(), dst_ports[n]);
            if(errno == EADDRINUSE) {
                fprintf(stderr, "Address already in use. This is usually caused by attempting to run multiple UHD programs at once\n");
            } else {
                fprintf(stderr, "Connect failed with error: %s\n", strerror(errno));
            }
        }

        // Sets the send buffer size
        setsockopt(send_socket_fd, SOL_SOCKET, SO_SNDBUF, &_DEFAULT_SEND_BUFFER_SIZE, sizeof(_DEFAULT_SEND_BUFFER_SIZE));

        // The actual size of the send buffer
        int _actual_send_buffer_size = 0;

        // Checks the recv buffer size
        socklen_t opt_len = sizeof(_actual_send_buffer_size);
        getsockopt(send_socket_fd, SOL_SOCKET, SO_SNDBUF, &_actual_send_buffer_size, &opt_len);

        // NOTE: The kernel will set the actual size to be double the requested. So the expected amount is double the requested
        if(_actual_send_buffer_size < 2*_DEFAULT_SEND_BUFFER_SIZE) {
            std::cout << "[SEND_PACKET_HANDLER] ERROR: Unable to set send buffer size. Performance will be negatively affected.\n Target size: " << _DEFAULT_SEND_BUFFER_SIZE << "\nActual size: " << (_actual_send_buffer_size/2) << "\nPlease run \"sudo sysctl -w net.core.wmem_max=" << _DEFAULT_SEND_BUFFER_SIZE << "\"\n" << std::endl;
        }

        int mtu = get_mtu(send_socket_fd, dst_ips[n].c_str());
        if(mtu < MIN_MTU) {
            std::cout << "[SEND_PACKET_HANDLER] ERROR: MTU of interface associated with " << dst_ips[n] << "is to small. " << MIN_MTU << "required, the current value is " << mtu << ".\n" << std::endl;
            throw std::runtime_error("MTU size to small");
        }

        int set_priority_ret = setsockopt(send_socket_fd, SOL_SOCKET, SO_PRIORITY, &TX_SO_PRIORITY, sizeof(TX_SO_PRIORITY));
        if(set_priority_ret) {
            fprintf(stderr, "Attempting to set tx socket priority failed with error code: %s", strerror(errno));
        }

        send_sockets[n] = send_socket_fd;
    }
}

send_packet_handler_mmsg::~send_packet_handler_mmsg(void){
    for(size_t n = 0; n < _NUM_CHANNELS; n++) {
        int r = close(send_sockets[n]);
        if(r) {
            fprintf(stderr, "close failed on data send socket with: %s\nThe program may not have closed cleanly\n", strerror(errno));
        }
    }

    if(sendmmsg_errno) {
        char nsec_s[42];
        snprintf(nsec_s, sizeof(nsec_s), "%ld.%.9ld", sendmmsg_failure_time.tv_sec, sendmmsg_failure_time.tv_nsec);

        std::cout << "[SEND_PACKET_HANDLER] ERROR: A sendmmsg command failed to send packets with error code " << strerror(sendmmsg_errno) << " at time " << nsec_s << std::endl;
    }
}

void send_packet_handler_mmsg::set_samp_rate(const double rate) {
    _sample_rate = rate;
    for(auto& ch_send_buffer_info_i : ch_send_buffer_info_group) {
        ch_send_buffer_info_i.buffer_level_manager.set_sample_rate(rate);
    }

    // Drop packets if they are within the lower of 20% of a buffer of being late and 40us
    // 40e-6 was chosen since it is extremely rare for packets to be over 35e-6 seconds late based on clock sync predictions
    drop_lead = std::min(0.2 * _DEVICE_BUFFER_SIZE / _sample_rate, 40e-6);
}

send_packet_handler_mmsg::ch_send_buffer_info::ch_send_buffer_info(const size_t size, const size_t vrt_header_size, const size_t cache_size, const int64_t device_target_nsamps, const double rate)
: _vrt_header_size(vrt_header_size),
sample_cache(std::vector<int8_t>(cache_size)),
buffer_level_manager(device_target_nsamps, rate)
{
    resize_and_clear(size);
}

void send_packet_handler_mmsg::ch_send_buffer_info::resize_and_clear(size_t new_size) {
    msgs.resize(new_size);
    memset(msgs.data(), 0, sizeof(mmsghdr)*new_size);
    // 1 VRT header and 1 data section in every packet, plus the cached samples in the first packet
    iovecs.resize(2*new_size + 1);
    memset(iovecs.data(), 0, sizeof(iovec)*2*new_size);
    vrt_headers.resize(new_size);
    std::fill(vrt_headers.begin(), vrt_headers.end(), std::vector<uint32_t>(_vrt_header_size/sizeof(uint32_t), 0));
    sample_data_start_for_packet.resize(new_size, 0);
}

void send_packet_handler_mmsg::expand_send_buffer_info(size_t new_size) {
    // Resizes the per channel buffers used in the send command
    if(new_size > send_buffer_info_size) {
        send_buffer_info_size = new_size;
        packet_header_infos.resize(new_size);
        for(size_t ch_i = 0; ch_i < _NUM_CHANNELS; ch_i++) {
            ch_send_buffer_info_group[ch_i].resize_and_clear(new_size);
        }
    }
}

int send_packet_handler_mmsg::check_fc_npackets(const size_t ch_i) {
    // Get the buffer level on the unit
    time_spec_t device_time = _clock_sync->get_device_time();
    int64_t buffer_level = ch_send_buffer_info_group[ch_i].buffer_level_manager.get_buffer_level(device_time);

    int num_packets_to_send = (int) std::ceil((_DEVICE_TARGET_NSAMPS - buffer_level) / ((double)_max_samples_per_packet));

    return num_packets_to_send;
}

void send_packet_handler_mmsg::send_eob_packet(const tx_metadata_t &metadata, double timeout) {

    // How many dummy samples to send in the eob
    constexpr size_t dummy_samples_in_eob = 1;

    // Create vector of dummy samples, since the FPGA cannot handle 0 sample packets
    std::vector<std::vector<int8_t>> dummy_buffs(_NUM_CHANNELS, std::vector<int8_t>(_BYTES_PER_SAMPLE * dummy_samples_in_eob, 0));
    std::vector<const void*> dummy_buff_ptrs(_NUM_CHANNELS);
    for(size_t n = 0; n < _NUM_CHANNELS; n++) {
        dummy_buff_ptrs[n] = dummy_buffs[n].data();
    }

    // Clear cached sob flag, to handle edge case of where user sends 0 sample sob, followed by eob
    cached_sob = false;

    tx_metadata_t eob_md = metadata;
    // Clears start of burst flag
    eob_md.start_of_burst = false;
    // Sets the eof time so buffer tracking can account for time between sob and eob
    for(auto& ch_send_buffer_info_i : ch_send_buffer_info_group) {
        ch_send_buffer_info_i.buffer_level_manager.set_end_of_burst_time(next_send_time);
    }

    // Record amount of samples dropped so that user may be informed of it if they start a new stream
    // Don't print warning about dropped samples here because it often results in umimportant warnings
    dropped_nsamps_in_cache = nsamps_in_cache;

    // Drop any samples in the cache, since otherwise they would be added to the next burst
    nsamps_in_cache = 0;

    // Sends the eob packet
    send_multiple_packets(dummy_buff_ptrs, dummy_samples_in_eob, eob_md, timeout, true);
}

int send_packet_handler_mmsg::get_mtu(int socket_fd, std::string ip) {
    //Start of linked list containing interface info
    struct ifaddrs *ifaces = nullptr;

    // Gets a linked list of all interfaces
    getifaddrs(&ifaces);
    for(ifaddrs *iface = ifaces; iface != NULL; iface = iface->ifa_next) {

        // Verifies this interface has a broadcast address
        if(iface->ifa_broadaddr != nullptr) {
            // Verifies said broadcast address is IPV4
            if(iface->ifa_broadaddr->sa_family == AF_INET) {
                // Converts broadcast address to human readable format
                char broadcast_buffer[INET_ADDRSTRLEN] = {0, };
                auto ret = inet_ntop(AF_INET,  &((struct sockaddr_in*)(iface->ifa_broadaddr))->sin_addr, broadcast_buffer, INET_ADDRSTRLEN);
                if(ret == nullptr) {
                    throw std::runtime_error("error when converting ip address format");
                }

                // Converts IP address to byte array
                uint8_t interface_ip[4];
                sscanf(broadcast_buffer, "%hhu.%hhu.%hhu.%hhu", &interface_ip[0], &interface_ip[1], &interface_ip[2], &interface_ip[3]);
                uint8_t device_ip[4];
                sscanf(ip.c_str(), "%hhu.%hhu.%hhu.%hhu", &device_ip[0], &device_ip[1], &device_ip[2], &device_ip[3]);

                // Checks if the interface subnet matches the Crimson ip to be checked
                bool ip_matches = true;
                for(int n = 0; n < 4; n++) {
                    // Checks if the IPs match or the interface is 255 (which corresponds to any)
                    if(interface_ip[n] != device_ip[n] && interface_ip[n] != 255) {
                        ip_matches = false;
                        break;
                    }
                }
                if(!ip_matches) {
                    continue;
                }

                struct ifreq ifr;
                ifr.ifr_addr.sa_family = AF_INET;//address family = IPV4
                strncpy(ifr.ifr_name, iface->ifa_name, sizeof(ifr.ifr_name));//interface name of MTU to get
                // Gets MTU
                if (ioctl(socket_fd, SIOCGIFMTU, (caddr_t)&ifr) < 0) {
                    throw std::runtime_error("ioctl error when attempting to check MTU\n");
                }
                freeifaddrs(ifaces);
                return ifr.ifr_mtu;
            }
        }
    }
    freeifaddrs(ifaces);
    throw std::runtime_error("No interface with subnet matching ip found");
}
