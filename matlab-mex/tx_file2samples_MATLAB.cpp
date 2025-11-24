/*=================================================================
 * tx_file2samples_MATLAB.cpp
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
 * This MEX-file bridges MATLAB to a Per Vices (UHD) SDR
 * for a high-performance transmit-from-file operation.
 *
 * It uses the same static C++ class as the receive.cpp to manage
 * the SDR's lifecycle, ensuring the device is initialized only once
 * and shared between MEX functions.
 *
 * Calling in MATLAB:
 * [MEX filename]('[data filename]', center_freq_hz, sample_rate_sps, gain_db)
 *
 * Example:
 * tx_file2samples_MATLAB('my_data.bin', 1.0e9, 10e6, 20.0);
 *
 *=================================================================*/

#include "mex.h"
#include <uhd/usrp/multi_usrp.hpp>
#include <uhd/stream.hpp>
#include <uhd/exception.hpp>

#include <string>
#include <vector>
#include <complex>
#include <fstream>
#include <iostream>
#include <memory>     // For std::unique_ptr
#include <algorithm>  // For std::min

// Use the C++11 typedef for complex float (same as UHD's "fc32")
typedef std::complex<float> fc32_t;

/**
 * @brief Manages the lifecycle of the UHD device.
 *
 * This class uses the RAII pattern. A static instance will be created
 * when the MEX file is first loaded. Its constructor finds and
 * initializes the SDR. Its destructor is called automatically
 * when the MEX file is unloaded (e.g., 'clear all'),
 * safely releasing the device.
 *
 * NOTE: This is IDENTICAL to the class in rx_samples2file_MATLAB.cpp. This is
 * intentional. The linker will create a single static instance
 * shared by all MEX files that link it (if built together) or
 * in this case, it just creates its own persistent object.
 * As long as the class definition is the same, the behavior is what we want:
 * a persistent, initialized device.
 */
class UHDDeviceManager {
public:
    // This is public so mexFunction can access it.
    uhd::usrp::multi_usrp::sptr usrp;

    /**
     * @brief Constructor: Called when MEX is loaded.
     */
    UHDDeviceManager() {
        mexPrintf("UHDDeviceManager: Initializing SDR device...\n");
        try {
            // Find the first Per Vices device
            usrp = uhd::usrp::multi_usrp::make(uhd::device_addr_t());
            mexPrintf("UHDDeviceManager: Device found and initialized.\n");
        } catch (const uhd::exception& e) {
            mexErrMsgIdAndTxt("UHD:DeviceInitFailed", e.what());
        }
    }

    /**
     * @brief Destructor: Called when MEX is unloaded.
     */
    ~UHDDeviceManager() {
        mexPrintf("UHDDeviceManager: Releasing SDR device...\n");
        // The sptr's destructor will be called automatically.
    }
};

// Create the one-and-only static instance of our device manager.
static UHDDeviceManager sdr_device;


/**
 * @brief The main MEX entry point.
 */
void mexFunction(int nlhs, mxArray* plhs[], int nrhs, const mxArray* prhs[])
{
    // --- 1. Check Arguments ---

    // We expect 4 inputs, 0 outputs
    if (nrhs != 4) {
        mexErrMsgIdAndTxt("UHD:transmit:invalidNumInputs",
            "Four inputs required: (filename, freq, rate, gain)");
    }
    if (nlhs > 0) {
        mexErrMsgIdAndTxt("UHD:transmit:maxlhs",
            "Zero output arguments expected.");
    }

    // Check input types
    if (!mxIsChar(prhs[0])) {
        mexErrMsgIdAndTxt("UHD:transmit:invalidInput", "Input 1 (filename) must be a string.");
    }
    if (!mxIsDouble(prhs[1]) || mxIsComplex(prhs[1])) {
        mexErrMsgIdAndTxt("UHD:transmit:invalidInput", "Input 2 (freq) must be a real double scalar.");
    }
    if (!mxIsDouble(prhs[2]) || mxIsComplex(prhs[2])) {
        mexErrMsgIdAndTxt("UHD:transmit:invalidInput", "Input 3 (rate) must be a real double scalar.");
    }
    if (!mxIsDouble(prhs[3]) || mxIsComplex(prhs[3])) {
        mexErrMsgIdAndTxt("UHD:transmit:invalidInput", "Input 4 (gain) must be a real double scalar.");
    }

    // --- 2. Parse Inputs ---

    std::unique_ptr<char, decltype(&mxFree)> filename_c_str(mxArrayToString(prhs[0]), &mxFree);
    std::string filename(filename_c_str.get());

    double center_freq = mxGetScalar(prhs[1]);
    double sample_rate = mxGetScalar(prhs[2]);
    double gain = mxGetScalar(prhs[3]);
    
    // Get the persistent USRP object
    uhd::usrp::multi_usrp::sptr usrp = sdr_device.usrp;

    // Check if the static object's init failed (e.g., no device)
    if (!usrp) {
        mexErrMsgIdAndTxt("UHD:transmit:deviceNotReady", 
            "SDR device is not initialized. 'clear all' and try again.");
        return;
    }

    // --- 3. Configure SDR and Stream ---

    try {
        mexPrintf("Configuring SDR: Freq=%.2f MHz, Rate=%.2f Msps, Gain=%.1f dB\n",
            center_freq / 1e6, sample_rate / 1e6, gain);
            
        // Set all TX parameters
        usrp->set_tx_rate(sample_rate);
        usrp->set_tx_freq(center_freq);
        usrp->set_tx_gain(gain);
        // We'll just use the first channel (channel 0)
        usrp->set_tx_antenna("TX/RX", 0); // Check your device for valid antenna names

        // Create a transmit streamer
        // "fc32" means complex<float>
        uhd::stream_args_t stream_args("fc32"); 
        stream_args.channels = {0}; // Use channel 0
        uhd::tx_streamer::sptr tx_stream = usrp->get_tx_stream(stream_args);

        // Open the input file
        std::ifstream infile(filename, std::ifstream::binary);
        if (!infile) {
            mexErrMsgIdAndTxt("UHD:transmit:fileOpenFailed", "Could not open input file.");
        }

        // --- 4. Stream Data ---

        // This is our transmit buffer.
        // We make it big (max samples per packet) to be efficient.
        std::vector<fc32_t> tx_buff(tx_stream->get_max_num_samps());
        size_t total_samples_sent = 0;

        // Configure the stream metadata
        uhd::tx_metadata_t md;
        md.has_time_spec = false;     // Don't use a timed send, send immediately
        md.start_of_burst = true;   // The first packet is the start of a burst
        md.end_of_burst = false;      // We will set this to true on the last packet
        
        mexPrintf("Streaming from %s...\n", filename.c_str());

        while (true) {
            // Read a chunk of data from the file
            infile.read(reinterpret_cast<char*>(tx_buff.data()), tx_buff.size() * sizeof(fc32_t));
            
            // gcount() tells us how many bytes were *actually* read
            size_t samps_read = infile.gcount() / sizeof(fc32_t);

            // If we're at the end of the file, this is our last packet
            if (infile.eof()) {
                md.end_of_burst = true;
            }

            if (samps_read > 0) {
                // Send the samples we read
                size_t samps_sent = tx_stream->send(tx_buff.data(), samps_read, md);
                total_samples_sent += samps_sent;
            }

            if (md.end_of_burst) {
                break; // We're done
            }
            
            // After the first send, no subsequent packet is the start
            md.start_of_burst = false;
        }

        // Cleanup
        infile.close();
        mexPrintf("Streaming complete. Sent %zu samples.\n", total_samples_sent);

    } catch (const uhd::exception& e) {
        mexErrMsgIdAndTxt("UHD:RuntimeError", e.what());
    } catch (const std::exception& e) {
        mexErrMsgIdAndTxt("STD:RuntimeError", e.what());
    }
    
    return;
}
