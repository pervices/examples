/*=================================================================
 * sdr_interface_pervices_v16.cpp
 *
 * Per Vices MatLab Implementation
 * Copyright (C) 2025 Per Vices Corporation
 *
 * SPDX-ID: GPL-3.0
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 * =================================================================
 *
 * This version implements the blocking-with-timeout logic
 * in 'handle_read_chunk'. This fixes the "variable chunk size"
 * problem by forcing the MEX file to wait for the requested
 * number of samples, up to the specified timeout.
 *
 * Key changes:
 * 1. handle_read_chunk: Added a std::chrono loop to wait
 * for samples or timeout.
 * 2. handle_read_chunk: The 'meta' output struct is now much
 * more useful for debugging (reports num_recd,
 * num_available_post_read, and timed_out).
 * 3. ThreadSafeCircularBuffer::read: Renamed arguments
 * for clarity.
 * 4. rx_stream_thread_func: Added explicit check for
 * UHD's ERROR_CODE_OVERFLOW.
 *
 *=================================================================
 *
 * --- Implemented MEX Commands ---
 *
 * This file provides a single MEX entry point, 'sdr', which
 * accepts a string command as its first argument.
 *
 * Implemented commands (parsed in 'mexFunction'):
 *
 * -- Connection & Info --
 * - sdr('open', device_args_string)
 * - sdr('close')
 * - sdr('get_info')
 * - sdr('get_time')
 * - sdr('get_sensor', 'name', 'rx'|'tx'|'mboard', [channel])
 * - sdr('get_gps')
 * - sdr('check_locks')
 *
 * -- Configuration --
 * - sdr('config_rx', rx_config_struct)
 * - sdr('config_tx', tx_config_struct)
 * - sdr('set_clock_source', 'source')
 *
 * -- Burst (Finite) Streaming --
 * - sdr('rx_stream', num_samples)
 * - sdr('tx_stream', tx_data_matrix)
 *
 * -- Continuous RX Streaming --
 * - sdr('start_rx_stream')
 * - sdr('read_chunk', num_samples, timeout_ms)
 * - sdr('stop_rx_stream')
 *
 * -- Continuous TX Streaming --
 * - sdr('start_tx_stream')
 * - sdr('write_chunk', tx_data_matrix)
 * - sdr('stop_tx_stream')
 *
 * -- Stubbed/Disabled Commands --
 * - sdr('set_rx_trigger')
 * - sdr('set_tx_trigger')
 *
 *
 * --- Not Implemented ---
 *
 * This interface is a high-level wrapper and does not expose
 * the full capabilities of the UHD API. Missing features include:
 *
 * - Advanced Timing: set_time_source, set_time_unknown_pps,
 * set_time_next_pps, get_time_last_pps.
 * - Advanced Triggers: Full implementation of set_rx_trigger
 * and set_tx_trigger (e.g., timed triggers, GPIO triggers).
 * - Multi-USRP Synchronization: Functions for synchronizing
 * time and phase across multiple devices.
 * - Daughterboard Controls: No access to the 'dboard'
 * property tree for fine-grained control.
 * - Antenna/Port Configuration: Only basic 'set_rx/tx_antenna'
 * is implemented.
 * - Direct RDMA/GPUDirect: This interface uses a CPU-based
 * circular buffer, not direct memory access (DMA) from the
 * NIC to the GPU (e.g., via RoCEv2/GPUDirect).
 * - Logging: No control over UHD's logging level.
 *
 *=================================================================*/

// --- MATLAB MEX API Header ---
// This is the main header required for building any MEX file.
// It defines 'mexFunction', 'mxArray', and all the 'mx...'
// functions used to interact with MATLAB.
#include "mex.h"

// --- UHD Headers ---
// These are the core headers from the UHD (USRP Hardware Driver)
// library, which provide the C++ API to control the SDR.
#include <uhd/usrp/multi_usrp.hpp> // The main device handle
#include <uhd/stream.hpp>          // For rx_streamer/tx_streamer
#include <uhd/exception.hpp>       // For UHD-specific error handling

// --- C++ Standard Library ---
// These provide fundamental C++ tools.
#include <string>    // For std::string
#include <vector>    // For std::vector (dynamic arrays)
#include <complex>   // For std::complex<float>
#include <memory>    // For std::unique_ptr, std::make_unique
#include <iostream>  // For std::cout, std::cerr (used by mexPrintf)
#include <algorithm> // For std::min, std::max
#include <stdexcept> // For std::runtime_error
#include <thread>    // For std::thread (our RX streaming thread)
#include <mutex>     // For std::mutex (to protect shared data)
#include <atomic>    // For std::atomic<bool> (thread-safe stop flag)
#include <chrono>    // For timing (e.g., handle_read_chunk timeout)
#include <queue>     // For std::queue (used in ThreadSafeErrorQueue)

// --- Type Definitions for Readability ---
// UHD streams data as 32-bit floating-point complex samples.
// MATLAB prefers 64-bit floating-point (double) complex samples.
// These typedefs make the code clearer about which format is
// being used where.
typedef std::complex<float>  fc32_t; // UHD's format
typedef std::complex<double> fc64_t; // MATLAB's format

// =================================================================
//  1. Thread-Safe Circular Buffer
// =================================================================
/**
 * @brief A thread-safe circular buffer (or ring buffer).
 *
 * This is the core of our continuous streaming design. It solves
 * the "producer-consumer" problem:
 * 1. Producer: The dedicated C++ thread ('rx_stream_thread_func')
 * receives data from the SDR and 'write's it to this buffer.
 * 2. Consumer: The main MATLAB thread calls 'sdr('read_chunk', ...)',
 * which 'read's data from this buffer.
 *
 * The 'std::mutex' ensures that the producer and consumer threads
 * can access the buffer's head, tail, and data without corrupting it.
 */
class ThreadSafeCircularBuffer {
public:
    /**
     * @brief Allocates the buffer memory.
     * @param size Max number of samples *per channel* to store.
     * @param num_channels The number of channels (e.g., 1, 2, 4).
     */
    ThreadSafeCircularBuffer(size_t size, size_t num_channels)
        : _max_size(size), _num_channels(num_channels) {
        // We de-interleave the data for simpler write/read logic.
        // The buffer is structured as [Chan0_Samps][Chan1_Samps]...
        _buffer.resize(size * num_channels);
        _head = 0;
        _tail = 0;
        _current_size = 0;
    }

    /**
     * @brief (Producer) Writes new samples from the SDR into the buffer.
     * @param data A vector of void pointers, one for each channel.
     * (e.g., data[0] -> fc32_t* for chan 0)
     * @param num_samples The number of samples *per channel* to write.
     * @return true on success, false if the buffer is full (a "Host Overrun").
     */
    bool write(const std::vector<void*>& data, size_t num_samples) {
        // Lock the buffer to prevent the 'read' function from
        // running at the same time.
        std::lock_guard<std::mutex> lock(_mutex);
        
        if (_current_size + num_samples > _max_size) {
            return false; // Overrun! Consumer is not reading fast enough.
        }

        // De-interleave the data from UHD's buffers into our
        // planar buffer layout.
        for (size_t i = 0; i < num_samples; ++i) {
            for (size_t ch = 0; ch < _num_channels; ++ch) {
                // Find the next circular write spot
                size_t write_idx = (_head + i) % _max_size;
                // Get the pointer to the raw UHD data for this channel
                fc32_t* channel_data = static_cast<fc32_t*>(data[ch]);
                // Calculate the flat index in our buffer:
                // (channel_offset + sample_index)
                _buffer[ch * _max_size + write_idx] = channel_data[i];
            }
        }
        // Advance the head pointer
        _head = (_head + num_samples) % _max_size;
        _current_size += num_samples;
        return true;
    }

    /**
     * @brief (Consumer) Reads samples from the buffer into a MATLAB matrix.
     * @param out_ptr Pointer to the MATLAB matrix (mxComplexDouble*).
     * @param matrix_row_count The number of rows in the output matrix (M).
     * @param samps_to_read The number of samples to pull from the buffer.
     * @return The number of samples *actually* read.
     */
    size_t read(mxComplexDouble* out_ptr, size_t matrix_row_count, size_t samps_to_read) {
        // Lock the buffer to prevent the 'write' function from
        // running at the same time.
        std::lock_guard<std::mutex> lock(_mutex);
        
        // Don't read more than we have, or more than was requested.
        size_t actual_samps_to_read = std::min(samps_to_read, _current_size);
        if (actual_samps_to_read == 0) {
            return 0;
        }

        // Safety check: ensure the output matrix is large enough.
        if (matrix_row_count < actual_samps_to_read) {
             actual_samps_to_read = matrix_row_count;
        }

        // Copy data from our buffer into the MATLAB matrix.
        // We must convert from fc32_t (C++) to mxComplexDouble (MATLAB)
        // and handle MATLAB's column-major memory layout.
        for (size_t i = 0; i < actual_samps_to_read; ++i) {
            for (size_t ch = 0; ch < _num_channels; ++ch) {
                // Find the next circular read spot
                size_t read_idx = (_tail + i) % _max_size;
                // Get the sample from our planar buffer
                fc32_t sample = _buffer[ch * _max_size + read_idx];
                
                // Write to MATLAB's column-major matrix
                // (channel_offset * num_rows + sample_index)
                (out_ptr + ch * matrix_row_count + i)->real = (double)sample.real();
                (out_ptr + ch * matrix_row_count + i)->imag = (double)sample.imag();
            }
        }
        // Advance the tail pointer
        _tail = (_tail + actual_samps_to_read) % _max_size;
        _current_size -= actual_samps_to_read;
        return actual_samps_to_read;
    }

    /**
     * @brief Safely gets the number of samples in the buffer.
     */
    size_t get_current_size() {
        std::lock_guard<std::mutex> lock(_mutex);
        return _current_size;
    }

    /**
     * @brief Safely clears the buffer.
     */
    void clear() {
        std::lock_guard<std::mutex> lock(_mutex);
        _head = 0;
        _tail = 0;
        _current_size = 0;
    }

private:
    std::mutex _mutex;            // The lock protecting this class
    std::vector<fc32_t> _buffer;  // The actual data storage
    size_t _max_size;             // Max samples *per channel*
    size_t _num_channels;         // Number of channels
    size_t _head;                 // Write position
    size_t _tail;                 // Read position
    size_t _current_size;         // Number of samples available
};

// =================================================================
//  1.5 Thread-Safe Error Queue
// =================================================================
/**
 * @brief A simple thread-safe queue for passing error messages.
 *
 * This solves a similar problem: The RX streaming thread
 * (Producer) might encounter an error (like an Overrun). It
 * cannot call 'mexWarnMsgIdAndTxt' directly, as that is not
 * thread-safe.
 *
 * Instead, it 'push'es an error message string into this queue.
 * The main MATLAB thread (Consumer) 'pop's these messages
 * inside 'handle_read_chunk' and displays them safely.
 */
class ThreadSafeErrorQueue {
public:
    void push(std::string msg) {
        std::lock_guard<std::mutex> lock(_mutex);
        _queue.push(msg);
    }
    std::string pop() {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_queue.empty()) {
            return "";
        }
        std::string msg = _queue.front();
        _queue.pop();
        return msg;
    }
    bool is_empty() {
        std::lock_guard<std::mutex> lock(_mutex);
        return _queue.empty();
    }
    void clear() {
        std::lock_guard<std::mutex> lock(_mutex);
        std::queue<std::string> empty; // Create a new empty queue
        std::swap(_queue, empty);      // Swap it with the old one
    }
private:
    std::queue<std::string> _queue;
    std::mutex _mutex;
};


// =================================================================
//  2. The Persistent SDR Manager Class (RAII)
// =================================================================
/**
 * @brief This class holds all the persistent UHD objects.
 *
 * MEX files are "stateless" by default. Every call to
 * 'sdr(...)' re-runs 'mexFunction'. To keep our SDR connection
 * alive between calls, we store all objects (usrp, stream,
 * buffer, thread) in a single *static* instance of this
 * class ('sdr_device').
 *
 * This class uses the RAII (Resource Acquisition Is Initialization)
 * pattern. The destructor '~SDRManager' automatically calls
 * 'release()', ensuring that even if MATLAB is closed or the
 * MEX file is cleared ('clear mex'), we properly stop the
 * thread and release the SDR hardware.
 */
class SDRManager {
public:
    // --- UHD Handles ---
    uhd::usrp::multi_usrp::sptr usrp;      // The main device object
    uhd::rx_streamer::sptr rx_stream; // The RX stream object
    uhd::tx_streamer::sptr tx_stream; // The TX stream object

    // --- Channel Info ---
    std::vector<size_t> rx_channels;  // e.g., [0, 1]
    std::vector<size_t> tx_channels;  // e.g., [0]
    
    // --- Continuous RX Threading Objects ---
    // We use unique_ptr to manage their lifetime, as they are
    // only created *after* 'config_rx' is called.
    std::unique_ptr<ThreadSafeCircularBuffer> rx_buffer;
    std::unique_ptr<ThreadSafeErrorQueue> rx_error_queue;
    std::thread _rx_thread;
    std::atomic<bool> _stop_rx_thread; // Thread-safe flag to stop the loop
    
    // Define the circular buffer size (samples per channel)
    static const size_t CIRCULAR_BUFFER_SIZE = 20000000; // ~800MB for 2ch fc32

    // Constructor: Initialize with safe default values.
    SDRManager() : usrp(nullptr), rx_stream(nullptr), tx_stream(nullptr), _stop_rx_thread(false) {}
    
    // Destructor: This is CRITICAL. It's called when the
    // MEX file is unloaded (e.g., 'clear sdr' or 'clear mex').
    ~SDRManager() {
        // Call the release function, but don't print messages
        // (verbose=false) because MATLAB might be shutting down.
        release(false);
    }

    /**
     * @brief The main loop for the dedicated SDR streaming thread (Producer).
     *
     * This function runs in the background *only* after
     * 'sdr('start_rx_stream')' is called. Its entire job is to
     * call 'rx_stream->recv()' in a loop and put the data
     * into the 'rx_buffer'.
     */
    void rx_stream_thread_func() {
        // Get the max number of samples UHD can give us in one chunk.
        // This is usually set by the transport (e.g., 10GbE).
        size_t buff_size = rx_stream->get_max_num_samps();
        
        // Pre-allocate buffers for UHD to write into.
        std::vector<std::vector<fc32_t>> channel_buffs(
            rx_channels.size(), std::vector<fc32_t>(buff_size));
        
        // Create a vector of void* pointers to these buffers,
        // which is what the 'recv' function expects.
        std::vector<void*> buff_ptrs(rx_channels.size());
        for (size_t i = 0; i < rx_channels.size(); ++i) {
            buff_ptrs[i] = channel_buffs[i].data();
        }
        
        uhd::rx_metadata_t md; // Holds metadata (timestamps, errors)

        // This is the main producer loop. It runs until
        // '_stop_rx_thread' is set to true.
        while (!_stop_rx_thread) {
            // This is a blocking call. It will wait up to 0.1s
            // (the timeout) for data to arrive from the SDR.
            size_t num_recd = rx_stream->recv(buff_ptrs, buff_size, md, 0.1);

            // --- Check for errors ---
            if (md.error_code != uhd::rx_metadata_t::ERROR_CODE_NONE) {
                if (md.error_code == uhd::rx_metadata_t::ERROR_CODE_LATE_COMMAND) {
                     // This is common when starting a stream and not fatal.
                } else if (md.error_code == uhd::rx_metadata_t::ERROR_CODE_OVERFLOW) {
                    // This is a "UHD Overrun" or "Kernel Overrun".
                    // The SDR sent data, but the PC's kernel
                    // buffer overflowed before UHD could read it.
                    // This means the C++ thread itself is not
                    // running fast enough.
                    this->rx_error_queue->push("UHD Overrun: PC not keeping up with SDR.");
                } else {
                    // Push any other UHD error string.
                    this->rx_error_queue->push(md.strerror());
                }
            }

            // --- Process good data ---
            if (num_recd > 0) {
                // Write the received data into our circular buffer.
                if (!rx_buffer->write(buff_ptrs, num_recd)) {
                    // This is a "Host Overrun".
                    // Our C++ thread *did* receive the data, but
                    // our circular buffer was full. This means
                    // MATLAB is not calling 'read_chunk'
                    // fast enough to keep up.
                    this->rx_error_queue->push("Host Overrun: MATLAB is not reading data fast enough.");
                }
            }
        }
        
        // --- Cleanup ---
        // The loop has exited (because _stop_rx_thread == true).
        // Send a "stop" command to the SDR to tell it to
        // stop sending samples.
        uhd::stream_cmd_t stream_cmd(uhd::stream_cmd_t::STREAM_MODE_STOP_CONTINUOUS);
        rx_stream->issue_stream_cmd(stream_cmd);
    }


    /**
     * @brief Finds and initializes the SDR device.
     */
    void open(const std::string& args) {
        if (usrp) {
            mexPrintf("SDRManager: Device is already open.\n");
            return;
        }
        try {
            // This 'mexPrintf' is like 'printf' but prints to
            // the MATLAB command window.
            mexPrintf("SDRManager: Initializing SDR device with args: %s\n", args.empty() ? "(default)" : args.c_str());
            
            // This is the main UHD call to find and connect
            // to the SDR hardware.
            usrp = uhd::usrp::multi_usrp::make(args);
            
            mexPrintf("SDRManager: Device found and initialized.\n");
        } catch (const uhd::exception& e) {
            // If 'make' fails (e.g., device not found),
            // 'mexErrMsgIdAndTxt' throws an error back to MATLAB.
            mexErrMsgIdAndTxt("UHD:DeviceInitFailed", e.what());
        }
    }

    /**
     * @brief Shuts down the stream thread and releases all hardware.
     */
    void release(bool verbose = true) {
        if (!usrp) return; // Already released.
        
        if (verbose) mexPrintf("SDRManager: Releasing streams and device...\n");
        
        // --- This is the most important part of shutdown ---
        if (_rx_thread.joinable()) {
            // 1. Tell the thread to stop its loop
            _stop_rx_thread = true;
            // 2. Wait for the thread to *actually* finish
            //    (exit the loop, send the STOP_CONTINUOUS command)
            _rx_thread.join(); 
        }

        // 3. Destroy the UHD objects in reverse order
        //    (smart pointers 'reset' to nullptr)
        rx_stream.reset();
        tx_stream.reset();
        usrp.reset();
        
        // 4. Destroy our custom buffer/queue
        rx_buffer.reset(); 
        rx_error_queue.reset();
        
        // 5. Reset state variables
        rx_channels.clear();
        tx_channels.clear();
        _stop_rx_thread = false;
        
        if (verbose) mexPrintf("SDRManager: All resources released.\n");
    }

    // --- Safety Check Helpers ---
    // These are called at the start of each 'handle_...'
    // function to give the user a clear error message.

    void check_open() {
        if (!usrp) {
            mexErrMsgIdAndTxt("UHD:DeviceNotOpen",
                "SDR device is not open. Call sdr('open', ...) first.");
        }
    }
    void check_rx_ready() {
        if (!rx_stream) {
            mexErrMsgIdAndTxt("UHD:RxNotConfigured",
                "RX streamer is not configured. Call sdr('config_rx', ...) first.");
        }
    }
    void check_tx_ready() {
        if (!tx_stream) {
            mexErrMsgIdAndTxt("UHD:TxNotConfigured",
                "TX streamer is not configured. Call sdr('config_tx', ...) first.");
        }
    }
};

// --- The Global Static Instance ---
// This is the single, persistent object that holds our state.
// It is constructed when the MEX file is first loaded and
// is destructed when the MEX file is unloaded.
const size_t SDRManager::CIRCULAR_BUFFER_SIZE;
static SDRManager sdr_device;


// =================================================================
//  3. Helper Functions (Parsing & Polling)
// =================================================================

// These functions are MEX "boilerplate" for converting
// MATLAB's 'mxArray*' (a generic array pointer) into
// C++ types (string, double, vector) that UHD can understand.

/**
 * @brief Parses a MATLAB string (char array) into a C++ std::string.
 */
std::string parse_string(const mxArray* ma) {
    if (!mxIsChar(ma)) {
        mexErrMsgIdAndTxt("UHD:ParseError", "Expected a string.");
    }
    // mxArrayToString allocates memory that we *must* free.
    // Using std::unique_ptr with a custom deleter (&mxFree)
    // is a safe, modern C++ way to ensure it's freed
    // even if an exception is thrown.
    std::unique_ptr<char, decltype(&mxFree)> c_str(mxArrayToString(ma), &mxFree);
    return std::string(c_str.get());
}
/**
 * @brief Gets a scalar double value from a MATLAB struct field.
 */
double get_scalar_field(const mxArray* struct_ma, const char* fieldname) {
    mxArray* field_ma = mxGetField(struct_ma, 0, fieldname);
    if (!field_ma) {
        std::string err = "Struct is missing field: " + std::string(fieldname);
        mexErrMsgIdAndTxt("UHD:ParseError", err.c_str());
    }
    if (!mxIsDouble(field_ma) || mxIsComplex(field_ma) || mxGetNumberOfElements(field_ma) != 1) {
         std::string err = "Field '" + std::string(fieldname) + "' must be a real double scalar.";
        mexErrMsgIdAndTxt("UHD:ParseError", err.c_str());
    }
    return mxGetScalar(field_ma);
}
/**
 * @brief Gets a string value from a MATLAB struct field.
 */
std::string get_string_field(const mxArray* struct_ma, const char* fieldname) {
    mxArray* field_ma = mxGetField(struct_ma, 0, fieldname);
    if (!field_ma) {
        std::string err = "Struct is missing field: " + std::string(fieldname);
        mexErrMsgIdAndTxt("UHD:ParseError", err.c_str());
    }
    return parse_string(field_ma);
}
/**
 * @brief Gets a vector of channel indices (e.g., [0, 1])
 * from a MATLAB struct field.
 */
std::vector<size_t> get_channel_vector(const mxArray* struct_ma, const char* fieldname) {
    mxArray* field_ma = mxGetField(struct_ma, 0, fieldname);
    if (!field_ma) {
         std::string err = "Struct is missing field: " + std::string(fieldname);
        mexErrMsgIdAndTxt("UHD:ParseError", err.c_str());
    }
    double* chan_ptr = mxGetDoubles(field_ma); // Get pointer to the double data
    size_t num_chans = mxGetNumberOfElements(field_ma);
    if (num_chans == 0 || num_chans > 4) { // Sanity check
         mexErrMsgIdAndTxt("UHD:ParseError", "Channel vector must have 1 to 4 elements.");
    }
    std::vector<size_t> channels(num_chans);
    for (size_t i = 0; i < num_chans; ++i) {
        // Cast from double (MATLAB's default) to size_t (C++)
        channels[i] = static_cast<size_t>(chan_ptr[i]);
    }
    return channels;
}
/**
 * @brief Polls a motherboard sensor until it returns the expected value.
 *
 * This is used to wait for things like 'ref_locked'.
 * Hardware operations are not instant, so we must poll.
 */
bool poll_mboard_sensor(uhd::usrp::multi_usrp::sptr usrp, std::string sensor_name, bool expected_val, double timeout_secs) {
    auto start_time = std::chrono::steady_clock::now();
    while (true) {
        // Get the sensor value and check it
        if (usrp->get_mboard_sensor(sensor_name, 0).to_bool() == expected_val) {
            return true; // Success!
        }
        
        // Check for timeout
        auto now = std::chrono::steady_clock::now();
        std::chrono::duration<double> elapsed = now - start_time;
        if (elapsed.count() > timeout_secs) {
            return false; // Timed out
        }
        
        // Don't spin-lock the CPU. Sleep for a bit.
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}
/**
 * @brief Polls the 'rfpll_lock' sensor for all RX channels.
 *
 * This is crucial. After setting a new frequency, we *must*
 * wait for the RF PLLs to lock before streaming data,
 * otherwise the data will be invalid.
 */
bool poll_rx_lock(uhd::usrp::multi_usrp::sptr usrp, const std::vector<size_t>& channels, double timeout_secs) {
    auto start_time = std::chrono::steady_clock::now();
    while (true) {
        bool all_locked = true;
        for (size_t ch : channels) {
            if (!usrp->get_rx_sensor("rfpll_lock", ch).to_bool()) {
                all_locked = false; // At least one channel is not locked
                break;              
            }
        }
        if (all_locked) return true; // Success!

        // Check for timeout
        auto now = std::chrono::steady_clock::now();
        std::chrono::duration<double> elapsed = now - start_time;
        if (elapsed.count() > timeout_secs) {
            return false; // Timed out
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}
/**
 * @brief Polls the 'rfpll_lock' sensor for all TX channels.
 */
bool poll_tx_lock(uhd::usrp::multi_usrp::sptr usrp, const std::vector<size_t>& channels, double timeout_secs) {
    auto start_time = std::chrono::steady_clock::now();
    while (true) {
        bool all_locked = true;
        for (size_t ch : channels) {
            if (!usrp->get_tx_sensor("rfpll_lock", ch).to_bool()) {
                all_locked = false;
                break;
            }
        }
        if (all_locked) return true; 

        auto now = std::chrono::steady_clock::now();
        std::chrono::duration<double> elapsed = now - start_time;
        if (elapsed.count() > timeout_secs) {
            return false; 
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}


// =================================================================
//  4. MEX Function Handlers (The "Business Logic")
// =================================================================
// These functions are called by the main 'mexFunction'
// dispatcher. 'prhs' is the array of *input* arguments
// from MATLAB, and 'plhs' is the array of *output* arguments
// to MATLAB.
//
// prhs[0] = 'command' (e.g., 'config_rx')
// prhs[1] = first argument (e.g., rx_config_struct)
// ...
//
// plhs[0] = first return value (e.g., data)
// plhs[1] = second return value (e.g., meta)
//

/**
 * @brief Handler for: sdr('config_rx', rx_config_struct)
 */
void handle_config_rx(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[]) {
    sdr_device.check_open(); // Ensure 'open' was called

    // If the stream is already running, warn the user.
    // Changing settings while streaming is dangerous.
    if (sdr_device._rx_thread.joinable()) {
        mexWarnMsgIdAndTxt("UHD:config_rx:StreamRunning",
            "Stream is currently running. Call sdr('stop_rx_stream') "
            "before re-configuring the device.");
        return; 
    }

    // --- 1. Argument Parsing ---
    if (nrhs != 2 || !mxIsStruct(prhs[1])) {
        mexErrMsgIdAndTxt("UHD:config_rx", "Usage: sdr('config_rx', rx_config_struct)");
    }
    
    // prhs[1] is the struct. Parse its fields.
    const mxArray* config = prhs[1];
    double rate      = get_scalar_field(config, "rate");
    double freq      = get_scalar_field(config, "freq");
    double gain      = get_scalar_field(config, "gain");
    double bw        = get_scalar_field(config, "bw");
    std::string ant  = get_string_field(config, "antenna");
    std::vector<size_t> chans = get_channel_vector(config, "channels");

    mexPrintf("Configuring RX: %zu channels, Freq=%.2f MHz, Rate=%.2f Msps...\n",
        chans.size(), freq / 1e6, rate / 1e6);

    try {
        // --- 2. Apply Settings to Hardware ---
        sdr_device.usrp->set_rx_rate(rate);
        for (size_t ch : chans) {
            sdr_device.usrp->set_rx_freq(freq, ch);
            sdr_device.usrp->set_rx_gain(gain, ch);
            sdr_device.usrp->set_rx_antenna(ant, ch);
            sdr_device.usrp->set_rx_bandwidth(bw, ch);
        }

        // --- 3. Wait for PLL Lock ---
        mexPrintf("Waiting for RX 'rfpll_lock' on channels: ");
        for(size_t ch : chans) { mexPrintf("%zu ", ch); }
        mexPrintf("... ");
        
        if (poll_rx_lock(sdr_device.usrp, chans, 2.0)) { 
            mexPrintf("OK\n");
        } else {
            // Throw an error back to MATLAB if it fails.
            mexErrMsgIdAndTxt("UHD:config_rx:LockError", 
                "RX PLL lock failed after 2.0s.");
        }
        
        // --- 4. Create Streamer & Buffers ---
        
        // Tell UHD we want 32-bit complex float format.
        uhd::stream_args_t stream_args("fc32"); 
        stream_args.channels = chans; // Specify which channels
        
        // This creates the RX streamer object.
        sdr_device.rx_stream = sdr_device.usrp->get_rx_stream(stream_args);
        sdr_device.rx_channels = chans; // Store the channels
        
        // Now that we know the channel count, create the
        // circular buffer and error queue for continuous streaming.
        sdr_device.rx_buffer = std::make_unique<ThreadSafeCircularBuffer>(
            sdr_device.CIRCULAR_BUFFER_SIZE, chans.size());
        
        sdr_device.rx_error_queue = std::make_unique<ThreadSafeErrorQueue>();
        
    } catch (const uhd::exception& e) {
        mexErrMsgIdAndTxt("UHD:config_rx:Error", e.what());
    }
}

/**
 * @brief Handler for: sdr('config_tx', tx_config_struct)
 *
 * This is identical in structure to 'handle_config_rx'.
 */
void handle_config_tx(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[]) {
    sdr_device.check_open();

    if (sdr_device._rx_thread.joinable()) {
        mexWarnMsgIdAndTxt("UHD:config_tx:StreamRunning",
            "An RX stream is currently running. Call sdr('stop_rx_stream') "
            "before re-configuring TX settings.");
        return; 
    }

    if (nrhs != 2 || !mxIsStruct(prhs[1])) {
        mexErrMsgIdAndTxt("UHD:config_tx", "Usage: sdr('config_tx', tx_config_struct)");
    }

    const mxArray* config = prhs[1];
    double rate      = get_scalar_field(config, "rate");
    double freq      = get_scalar_field(config, "freq");
    double gain      = get_scalar_field(config, "gain");
    double bw        = get_scalar_field(config, "bw");
    std::string ant  = get_string_field(config, "antenna");
    std::vector<size_t> chans = get_channel_vector(config, "channels");

     mexPrintf("Configuring TX: %zu channels, Freq=%.2f MHz, Rate=%.2f Msps...\n",
        chans.size(), freq / 1e6, rate / 1e6);

    try {
        sdr_device.usrp->set_tx_rate(rate);
        for (size_t ch : chans) {
            sdr_device.usrp->set_tx_freq(freq, ch);
            sdr_device.usrp->set_tx_gain(gain, ch);
            sdr_device.usrp->set_tx_antenna(ant, ch);
            sdr_device.usrp->set_tx_bandwidth(bw, ch);
        }

        mexPrintf("Waiting for TX 'rfpll_lock' on channels: ");
        for(size_t ch : chans) { mexPrintf("%zu ", ch); }
        mexPrintf("... ");

        if (poll_tx_lock(sdr_device.usrp, chans, 2.0)) {
            mexPrintf("OK\n");
        } else {
            mexErrMsgIdAndTxt("UHD:config_tx:LockError", 
                "TX PLL lock failed after 2.0s.");
        }
        
        uhd::stream_args_t stream_args("fc32");
        stream_args.channels = chans;
        sdr_device.tx_stream = sdr_device.usrp->get_tx_stream(stream_args);
        sdr_device.tx_channels = chans;
        
    } catch (const uhd::exception& e) {
        mexErrMsgIdAndTxt("UHD:config_tx:Error", e.what());
    }
}

/**
 * @brief Handler for: [data, meta] = sdr('rx_stream', num_samples)
 *
 * This performs a "burst" or "finite" stream. It asks the SDR
 * for *exactly* 'num_samples' and then stops. This does *not*
 * use the background thread or circular buffer.
 */
void handle_rx_stream(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[]) {
    sdr_device.check_rx_ready();
    if (nrhs != 2 || nlhs > 2 || !mxIsDouble(prhs[1])) {
         mexErrMsgIdAndTxt("UHD:rx_stream", "Usage: [data, meta] = sdr('rx_stream', num_samples)");
    }

    size_t num_samples = static_cast<size_t>(mxGetScalar(prhs[1]));
    size_t num_chans = sdr_device.rx_channels.size();
    
    // --- 1. Allocate Output Matrix ---
    // We create the MATLAB matrix *before* receiving data.
    // Format is [num_samples x num_channels].
    plhs[0] = mxCreateNumericMatrix(num_samples, num_chans, mxDOUBLE_CLASS, mxCOMPLEX);
    mxComplexDouble* out_ptr = mxGetComplexDoubles(plhs[0]); // Get C++ pointer to it

    // --- 2. Allocate C++ Receive Buffers ---
    size_t buff_size = std::min(num_samples, sdr_device.rx_stream->get_max_num_samps());
    std::vector<std::vector<fc32_t>> channel_buffs(num_chans, std::vector<fc32_t>(buff_size));
    std::vector<void*> buff_ptrs(num_chans);
    for (size_t i = 0; i < num_chans; ++i) {
        buff_ptrs[i] = channel_buffs[i].data();
    }
    
    uhd::rx_metadata_t md;
    
    try {
        // --- 3. Issue Stream Command ---
        
        // Tell the SDR we want a finite number of samples
        uhd::stream_cmd_t stream_cmd(uhd::stream_cmd_t::STREAM_MODE_NUM_SAMPS_AND_DONE);
        stream_cmd.num_samps = num_samples;
        
        // Set up a timed command:
        // stream_now = false: Don't start immediately.
        // time_spec = ...:   Start at a specific time (0.1s in
        //                   the future) to ensure all channels
        //                   start at the same moment.
        stream_cmd.stream_now = false;
        stream_cmd.time_spec = sdr_device.usrp->get_time_now() + uhd::time_spec_t(0.1);
        mexPrintf("Issuing timed RX burst for %zu samples.\n", num_samples);
        
        sdr_device.rx_stream->issue_stream_cmd(stream_cmd);

        // --- 4. Receive Loop ---
        // We may need to call 'recv' multiple times to get
        // all 'num_samples'.
        size_t total_recd = 0;
        while (total_recd < num_samples) {
            size_t num_to_recv = std::min(buff_size, num_samples - total_recd);
            
            // This blocks until 'num_to_recv' samples arrive
            // or the 3.0s timeout is hit.
            size_t num_recd = sdr_device.rx_stream->recv(buff_ptrs, num_to_recv, md, 3.0); 

            // Check for errors
            if (md.error_code != uhd::rx_metadata_t::ERROR_CODE_NONE) {
                if (md.error_code == uhd::rx_metadata_t::ERROR_CODE_LATE_COMMAND) {
                     mexWarnMsgIdAndTxt("UHD:rx_stream:Late", 
                        "Stream command was late! (Try increasing the time delta).");
                } else {
                    mexWarnMsgIdAndTxt("UHD:rx_stream:Error", md.strerror().c_str());
                }
            }
            if (num_recd == 0) continue; // Spurious wakeup

            // --- 5. Copy data to MATLAB matrix ---
            // This is the most complex part: copying from the
            // C++ 'channel_buffs' (fc32_t) to the MATLAB
            // 'out_ptr' (mxComplexDouble), respecting the
            // column-major layout.
            for (size_t i = 0; i < num_chans; ++i) { 
                // Get pointer to the *start* of the i-th column
                mxComplexDouble* col_ptr = out_ptr + (i * num_samples); 
                for (size_t j = 0; j < num_recd; ++j) { 
                    // Write at (total_recd + j) offset in that column
                    col_ptr[total_recd + j].real = (double)channel_buffs[i][j].real();
                    col_ptr[total_recd + j].imag = (double)channel_buffs[i][j].imag();
                }
            }
            total_recd += num_recd;
        }

        // --- 6. Create Metadata Output ---
        // If the user asked for a second output argument:
        // [~, meta] = sdr(...)
        if (nlhs > 1) {
            const char* field_names[] = {"error_code", "time_spec", "str_error"};
            plhs[1] = mxCreateStructMatrix(1, 1, 3, field_names);
            mxSetField(plhs[1], 0, "error_code", mxCreateDoubleScalar((double)md.error_code));
            mxSetField(plhs[1], 0, "time_spec", mxCreateDoubleScalar(md.time_spec.get_full_secs() + md.time_spec.get_frac_secs()));
            mxSetField(plhs[1], 0, "str_error", mxCreateString(md.strerror().c_str()));
        }

    } catch (const uhd::exception& e) {
        mexErrMsgIdAndTxt("UHD:rx_stream:RuntimeError", e.what());
    }
}


/**
 * @brief Handler for: sdr('tx_stream', tx_data)
 *
 * Performs a "burst" or "finite" transmission.
 */
void handle_tx_stream(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[]) {
    sdr_device.check_tx_ready();
    if (nrhs != 2 || !mxIsComplex(prhs[1])) {
        mexErrMsgIdAndTxt("UHD:tx_stream", "Usage: sdr('tx_stream', tx_data)");
    }

    // --- 1. Get Input Matrix ---
    const mxArray* tx_data_ma = prhs[1];
    size_t num_samples = mxGetM(tx_data_ma); // M = rows = num_samples
    size_t num_chans = mxGetN(tx_data_ma);   // N = cols = num_channels
    
    if (num_chans != sdr_device.tx_channels.size()) {
        mexErrMsgIdAndTxt("UHD:tx_stream", "tx_data column count does not match configured channel count.");
    }
    if (num_samples == 0) {
        mexWarnMsgIdAndTxt("UHD:tx_stream", "Empty data matrix provided. Nothing to send.");
        return;
    }

    // Get C++ pointer to the MATLAB data
    mxComplexDouble* in_ptr = mxGetComplexDoubles(tx_data_ma);
    
    // --- 2. Allocate C++ Transmit Buffers ---
    size_t buff_size = std::min(num_samples, sdr_device.tx_stream->get_max_num_samps());
    std::vector<std::vector<fc32_t>> channel_buffs(num_chans, std::vector<fc32_t>(buff_size));
    // For 'send', the pointers must be 'const void*'
    std::vector<const void*> buff_ptrs(num_chans); 
    for (size_t i = 0; i < num_chans; ++i) {
        buff_ptrs[i] = channel_buffs[i].data();
    }
    
    // --- 3. Set up TX Metadata ---
    uhd::tx_metadata_t md;
    md.start_of_burst = true;  // Mark the first packet
    md.end_of_burst = false;   // Will be set to true on the last packet
    md.has_time_spec = true;   // This is a timed command
    // Set time 0.1s in the future for synchronized start
    md.time_spec = sdr_device.usrp->get_time_now() + uhd::time_spec_t(0.1);
    mexPrintf("Issuing timed TX burst for %zu samples.\n", num_samples);

    try {
        // --- 4. Transmit Loop ---
        size_t total_sent = 0;
        
        while (total_sent < num_samples) {
            size_t num_to_send = std::min(buff_size, num_samples - total_sent);

            // --- 5. Copy data from MATLAB to C++ ---
            // This is the reverse of 'handle_rx_stream'.
            // We copy from MATLAB's column-major matrix
            // into our 'channel_buffs', converting from
            // double to float.
            for (size_t i = 0; i < num_chans; ++i) { 
                // Get pointer to the i-th column
                mxComplexDouble* col_ptr = in_ptr + (i * num_samples); 
                for (size_t j = 0; j < num_to_send; ++j) { 
                    // Read from (total_sent + j) offset
                    channel_buffs[i][j] = fc32_t(
                        (float)col_ptr[total_sent + j].real,
                        (float)col_ptr[total_sent + j].imag
                    );
                }
            }

            // If this is the last chunk, mark it.
            if (total_sent + num_to_send >= num_samples) {
                md.end_of_burst = true; 
            }

            // --- 6. Send the chunk ---
            // This blocks until the data is sent.
            size_t num_sent = sdr_device.tx_stream->send(buff_ptrs, num_to_send, md);
            total_sent += num_sent;
            
            // After the first packet, clear the "start of burst"
            // and "has time spec" flags.
            md.start_of_burst = false; 
            md.has_time_spec = false;
        }
        
        mexPrintf("TX stream complete. Sent %zu samples.\n", total_sent);

    } catch (const uhd::exception& e) {
        mexErrMsgIdAndTxt("UHD:tx_stream:RuntimeError", e.what());
    }
}

// --- v13/v16: Continuous Stream Handlers ---

/**
 * @brief Handler for: sdr('start_rx_stream')
 */
void handle_start_rx_stream(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[]) {
    sdr_device.check_rx_ready();
    
    if (sdr_device._rx_thread.joinable()) {
        mexWarnMsgIdAndTxt("UHD:start_rx_stream", "Stream thread is already running.");
        return;
    }
    
    // --- 1. Reset State ---
    sdr_device._stop_rx_thread = false;
    sdr_device.rx_buffer->clear();
    sdr_device.rx_error_queue->clear();

    // --- 2. Issue Stream Command ---
    // Tell the SDR to start streaming *indefinitely*.
    uhd::stream_cmd_t stream_cmd(uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS);
    stream_cmd.stream_now = true; // Start immediately
    sdr_device.rx_stream->issue_stream_cmd(stream_cmd);
    
    // --- 3. Start the Producer Thread ---
    // This is the key. We spawn a new C++ thread that
    // will run the 'rx_stream_thread_func' in the
    // background.
    sdr_device._rx_thread = std::thread(&SDRManager::rx_stream_thread_func, &sdr_device);
    
    mexPrintf("Continuous RX stream started.\n");
}

/**
 * @brief Handler for: [data, meta] = sdr('read_chunk', num_samples, timeout_ms)
 *
 * --- MODIFIED in v16 ---
 * This is the "Consumer" function for continuous streaming.
 * It reads data from the circular buffer that the
 * background thread is filling.
 */
void handle_read_chunk(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[]) {
    sdr_device.check_rx_ready();
    if (nrhs != 3 || nlhs > 2 || !mxIsDouble(prhs[1]) || !mxIsDouble(prhs[2])) {
         mexErrMsgIdAndTxt("UHD:read_chunk", "Usage: [data, meta] = sdr('read_chunk', num_samples, timeout_ms)");
    }
    
    // --- 1. Drain the error queue first ---
    // Check if the background thread reported any errors
    // since the last time we called this.
    while (!sdr_device.rx_error_queue->is_empty()) {
        std::string err = sdr_device.rx_error_queue->pop();
        // Display as a MATLAB warning.
        mexWarnMsgIdAndTxt("UHD:stream_error", err.c_str());
    }

    size_t num_samples_requested = static_cast<size_t>(mxGetScalar(prhs[1]));
    double timeout_ms = mxGetScalar(prhs[2]);
    size_t num_chans = sdr_device.rx_channels.size();
    bool timed_out = false;
    
    // --- 2. Implement blocking-with-timeout wait ---
    // This loop waits until the circular buffer has *at least*
    // 'num_samples_requested' or the timeout expires.
    auto start_time = std::chrono::steady_clock::now();
    while (true) {
        // Safely check the buffer size
        size_t num_available = sdr_device.rx_buffer->get_current_size();
        
        // Success: We have enough data
        if (num_available >= num_samples_requested) {
            break;
        }

        // Check for timeout
        auto now = std::chrono::steady_clock::now();
        std::chrono::duration<double, std::milli> elapsed = now - start_time;
        if (elapsed.count() > timeout_ms) {
            timed_out = true;
            break; // Timed out, exit the loop
        }

        // Don't spin-lock the CPU. Yield for 5ms.
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    // --- End of wait loop ---

    // --- 3. Decide how much data to *actually* read ---
    // After the loop, check the buffer one last time.
    // If we timed out, 'final_num_available' might be
    // less than 'num_samples_requested'.
    size_t final_num_available = sdr_device.rx_buffer->get_current_size();
    
    // We will read whichever is *smaller*:
    // 1. The amount we originally requested.
    // 2. The amount that is *actually* available.
    size_t num_to_read = std::min(num_samples_requested, final_num_available);

    // --- 4. Allocate the *exact* size needed and read data ---
    if (num_to_read == 0) {
        // If we're reading 0 samples, create a 0x0
        // empty matrix for MATLAB.
        plhs[0] = mxCreateNumericMatrix(0, 0, mxDOUBLE_CLASS, mxCOMPLEX);
    } else {
        // Allocate a matrix of *exactly* [num_to_read x num_chans]
        plhs[0] = mxCreateNumericMatrix(num_to_read, num_chans, mxDOUBLE_CLASS, mxCOMPLEX);
        mxComplexDouble* out_ptr = mxGetComplexDoubles(plhs[0]);
        
        // Call the buffer's 'read' function
        // (pointer, num_rows_in_matrix, samps_to_read)
        num_to_read = sdr_device.rx_buffer->read(out_ptr, num_to_read, num_to_read);
    }

    // --- 5. Create rich metadata output ---
    if (nlhs > 1) {
        const char* field_names[] = {"num_recd", "num_available", "timed_out"};
        plhs[1] = mxCreateStructMatrix(1, 1, 3, field_names);
        
        // num_recd: How many samples are in plhs[0]
        mxSetField(plhs[1], 0, "num_recd", mxCreateDoubleScalar((double)num_to_read));
        
        // num_available: How many samples are *still* in the
        // buffer (for debugging)
        mxSetField(plhs[1], 0, "num_available", mxCreateDoubleScalar(
            (double)sdr_device.rx_buffer->get_current_size()
        ));
        
        // timed_out: A boolean flag
        mxSetField(plhs[1], 0, "timed_out", mxCreateLogicalScalar(timed_out));
    }
}

/**
 * @brief Handler for: sdr('stop_rx_stream')
 */
void handle_stop_rx_stream(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[]) {
    sdr_device.check_rx_ready();
    
    // Check if the thread is actually running
    if (sdr_device._rx_thread.joinable()) {
        // 1. Set the atomic flag to 'true'. The
        //    background thread will see this and exit its
        //    'while' loop.
        sdr_device._stop_rx_thread = true;
        
        // 2. Wait for the thread to *fully* exit and clean up.
        //    This is a blocking call.
        sdr_device._rx_thread.join(); 
        mexPrintf("Continuous RX stream stopped.\n");
    } else {
        mexWarnMsgIdAndTxt("UHD:stop_rx_stream", "Stream thread is not running.");
    }
}

/**
 * @brief Handler for: sdr('start_tx_stream')
 *
 * This doesn't actually do anything in UHD. The TX stream
 * starts automatically on the first call to 'send'.
 * This is just for API symmetry with the RX side.
 */
void handle_start_tx_stream(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[]) {
    sdr_device.check_tx_ready();
    mexPrintf("Continuous TX stream armed. First 'write_chunk' will begin transmission.\n");
}

/**
 * @brief Handler for: sdr('write_chunk', tx_data)
 *
 * This is the "Producer" for continuous TX. It takes a
 * chunk of data from MATLAB and sends it to the SDR.
 */
void handle_write_chunk(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[]) {
    sdr_device.check_tx_ready();
    if (nrhs != 2 || !mxIsComplex(prhs[1])) {
        mexErrMsgIdAndTxt("UHD:write_chunk", "Usage: sdr('write_chunk', tx_data)");
    }
    
    // --- 1. Parse Input Matrix ---
    const mxArray* tx_data_ma = prhs[1];
    size_t num_samples = mxGetM(tx_data_ma);
    size_t num_chans = mxGetN(tx_data_ma); 
    
    if (num_chans != sdr_device.tx_channels.size()) {
        mexErrMsgIdAndTxt("UHD:write_chunk", "tx_data column count does not match configured channel count.");
    }
    if (num_samples == 0) return; // Nothing to send

    mxComplexDouble* in_ptr = mxGetComplexDoubles(tx_data_ma);
    
    // --- 2. Allocate and Fill C++ Buffers ---
    // Note: We allocate a buffer *exactly* 'num_samples' large.
    std::vector<std::vector<fc32_t>> channel_buffs(num_chans, std::vector<fc32_t>(num_samples));
    std::vector<const void*> buff_ptrs(num_chans);
    for (size_t i = 0; i < num_chans; ++i) {
        buff_ptrs[i] = channel_buffs[i].data();
    }
    
    // Copy from MATLAB (column-major) to C++ (planar)
    for (size_t i = 0; i < num_chans; ++i) { 
        mxComplexDouble* col_ptr = in_ptr + (i * num_samples);
        for (size_t j = 0; j < num_samples; ++j) {
            channel_buffs[i][j] = fc32_t((float)col_ptr[j].real, (float)col_ptr[j].imag);
        }
    }
    
    // --- 3. Send Data ---
    uhd::tx_metadata_t md;
    md.has_time_spec = false;   // Send immediately
    md.start_of_burst = false; // Not the start of a burst
    
    // This is a blocking call. It will wait until the
    // hardware has accepted the data.
    sdr_device.tx_stream->send(buff_ptrs, num_samples, md);
}

/**
 * @brief Handler for: sdr('stop_tx_stream')
 */
void handle_stop_tx_stream(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[]) {
    sdr_device.check_tx_ready();
    
    // To stop a continuous TX stream, we must send a
    // zero-length packet with the 'end_of_burst' flag set.
    uhd::tx_metadata_t md;
    md.end_of_burst = true;
    sdr_device.tx_stream->send("", 0, md); 
    
    mexPrintf("Continuous TX stream stopped.\n");
}

// --- Trigger Handlers (Disabled) ---
// These are stubbed out as placeholders.
void handle_set_rx_trigger(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[]) {
    sdr_device.check_open();
    mexWarnMsgIdAndTxt("UHD:triggers:Disabled",
        "Trigger support is disabled in this build. WIP for MEX code."
        "Please reach out to solutions@pervices.com if this is a requirement.");
}

void handle_set_tx_trigger(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[]) {
    sdr_device.check_open();
    mexWarnMsgIdAndTxt("UHD:triggers:Disabled",
        "Trigger support is disabled in this build. WIP for MEX code."
        "Please reach out to solutions@pervices.com if this is a requirement.");
}


// =================================================================
//  5. The Main MEX Function (Dispatcher)
// =================================================================
/**
 * @brief The single C-style entry point for the MEX file.
 *
 * MATLAB calls this function every time you run 'sdr(...)'.
 *
 * @param nlhs Number of Left-Hand-Side arguments (outputs).
 * @param plhs Array of pointers to Left-Hand-Side (output) mxArrays.
 * @param nrhs Number of Right-Hand-Side arguments (inputs).
 * @param prhs Array of const pointers to Right-Hand-Side (input) mxArrays.
 */
void mexFunction(int nlhs, mxArray* plhs[], int nrhs, const mxArray* prhs[])
{
    // --- 1. Check for minimum arguments ---
    if (nrhs < 1) {
        mexErrMsgIdAndTxt("UHD:Interface", "No command specified. Usage: sdr('command', ...)");
    }

    // --- 2. Get the Command String ---
    // The first argument *must* be a string command.
    std::string command = parse_string(prhs[0]);

    // --- 3. Set up Top-Level Error Handling ---
    // This 'try...catch' block is crucial. If any C++ code
    // (UHD or our own) throws an exception, this catches it
    // and converts it into a red MATLAB error message
    // instead of crashing MATLAB.
    try {
        // --- 4. The Dispatcher ---
        // This is a big 'if/else if' block that acts as a
        // router, calling the correct 'handle_...' function
        // based on the command string.
        
        if (command == "open") {
            if (nrhs != 2) mexErrMsgIdAndTxt("UHD:open", "Usage: sdr('open', device_args_string)");
            sdr_device.open(parse_string(prhs[1]));
        
        } else if (command == "close") {
            sdr_device.release(true); // Call verbose release
        
        } else if (command == "config_rx") {
            handle_config_rx(nlhs, plhs, nrhs, prhs);
        
        } else if (command == "config_tx") {
            handle_config_tx(nlhs, plhs, nrhs, prhs);
        
        } else if (command == "rx_stream") {
            handle_rx_stream(nlhs, plhs, nrhs, prhs);
        
        } else if (command == "tx_stream") {
            handle_tx_stream(nlhs, plhs, nrhs, prhs);

        // --- Continuous Stream Commands ---
        } else if (command == "start_rx_stream") {
            handle_start_rx_stream(nlhs, plhs, nrhs, prhs);
        } else if (command == "read_chunk") {
            handle_read_chunk(nlhs, plhs, nrhs, prhs);
        } else if (command == "stop_rx_stream") {
            handle_stop_rx_stream(nlhs, plhs, nrhs, prhs);
        } else if (command == "start_tx_stream") {
            handle_start_tx_stream(nlhs, plhs, nrhs, prhs);
        } else if (command == "write_chunk") {
            handle_write_chunk(nlhs, plhs, nrhs, prhs);
        } else if (command == "stop_tx_stream") {
            handle_stop_tx_stream(nlhs, plhs, nrhs, prhs);
            
        // --- Stubbed Trigger Commands ---
        } else if (command == "set_rx_trigger") { 
            handle_set_rx_trigger(nlhs, plhs, nrhs, prhs);
        } else if (command == "set_tx_trigger") { 
            handle_set_tx_trigger(nlhs, plhs, nrhs, prhs);

        // --- Utility Commands ---
        } else if (command == "check_locks") {
            sdr_device.check_open();
            mexPrintf("Checking motherboard 'ref_locked' sensor... ");
            if (poll_mboard_sensor(sdr_device.usrp, "ref_locked", true, 2.0)) {
                mexPrintf("OK\n");
            } else {
                mexErrMsgIdAndTxt("UHD:check_locks", "Reference clock is NOT locked.");
            }
        
        } else if (command == "get_info") {
            sdr_device.check_open();
            // Get the device's "pretty print" string
            // and return it as a MATLAB string.
            plhs[0] = mxCreateString(sdr_device.usrp->get_pp_string().c_str());

        } else if (command == "get_time") {
            sdr_device.check_open();
            // Get the SDR's internal hardware time
            uhd::time_spec_t now = sdr_device.usrp->get_time_now();
            // Return it as a single double value (full_secs + frac_secs)
            plhs[0] = mxCreateDoubleScalar(now.get_full_secs() + now.get_frac_secs());

        } else if (command == "set_clock_source") {
            sdr_device.check_open();
            if (nrhs != 2) mexErrMsgIdAndTxt("UHD:set_clock_source", "Usage: sdr('set_clock_source', 'source')");
            sdr_device.usrp->set_clock_source(parse_string(prhs[1]));
            mexPrintf("Clock source set to: %s\n", parse_string(prhs[1]).c_str());

        } else if (command == "get_sensor") {
            sdr_device.check_open();
            if (nrhs < 3) mexErrMsgIdAndTxt("UHD:get_sensor", "Usage: val = sdr('get_sensor', 'name', 'rx'|'tx'|'mboard', [channel])");
            
            std::string name = parse_string(prhs[1]);
            std::string type = parse_string(prhs[2]);
            
            // Dispatch based on sensor type
            if (type == "mboard") {
                uhd::sensor_value_t val = sdr_device.usrp->get_mboard_sensor(name, 0);
                plhs[0] = mxCreateString(val.to_pp_string().c_str());
            } else if (type == "rx") {
                if (nrhs != 4) mexErrMsgIdAndTxt("UHD:get_sensor", "RX sensor needs a channel index.");
                uhd::sensor_value_t val = sdr_device.usrp->get_rx_sensor(name, (size_t)mxGetScalar(prhs[3]));
                plhs[0] = mxCreateString(val.to_pp_string().c_str());
            } else if (type == "tx") {
                if (nrhs != 4) mexErrMsgIdAndTxt("UHD:get_sensor", "TX sensor needs a channel index.");
                uhd::sensor_value_t val = sdr_device.usrp->get_tx_sensor(name, (size_t)mxGetScalar(prhs[3]));
                plhs[0] = mxCreateString(val.to_pp_string().c_str());
            } else {
                mexErrMsgIdAndTxt("UHD:get_sensor", "Unknown sensor type. Use 'rx', 'tx', or 'mboard'.");
            }
        
        } else if (command == "get_gps") {
            sdr_device.check_open();
            // Return a struct with GPS info
            const char* field_names[] = {"locked", "time", "nmea"};
            plhs[0] = mxCreateStructMatrix(1, 1, 3, field_names);
            try {
                mxSetField(plhs[0], 0, "locked", mxCreateLogicalScalar(
                    sdr_device.usrp->get_mboard_sensor("gps_locked", 0).to_bool()));
                mxSetField(plhs[0], 0, "time", mxCreateString(
                    sdr_device.usrp->get_mboard_sensor("gps_time", 0).to_pp_string().c_str()));
                mxSetField(plhs[0], 0, "nmea", mxCreateString(
                    sdr_device.usrp->get_mboard_sensor("nmea_msg", 0).to_pp_string().c_str()));
            } catch (const uhd::exception& e) {
                 // Handle case where GPS is not present
                 mexWarnMsgIdAndTxt("UHD:get_gps", "Could not read GPS sensors. (Not present or not enabled?)");
                 plhs[0] = mxCreateStructMatrix(1, 1, 0, NULL);
            }

        // --- Fallback ---
        } else {
            std::string err = "Unknown command: '" + command + "'";
            mexErrMsgIdAndTxt("UHD:Interface", err.c_str());
        }

    } catch (const uhd::exception& e) {
        // Catch UHD-specific errors
        mexErrMsgIdAndTxt("UHD:RuntimeError", e.what());
    } catch (const std::exception& e) {
        // Catch standard C++ errors (e.g., std::runtime_error)
        mexErrMsgIdAndTxt("UHD:std_exception", e.what());
    } catch (...) {
        // Catch all other unknown errors
        mexErrMsgIdAndTxt("UHD:UnknownError", "An unknown C++ exception occurred.");
    }
}
