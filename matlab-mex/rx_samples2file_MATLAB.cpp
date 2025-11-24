/*=================================================================
 * rx_samples2file_MATLAB.cpp
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
 * for a high-performance receive-to-file operation.
 *
 * It uses a static C++ class to manage the SDR's lifecycle,
 * ensuring the device is initialized only once (on first call)
 * and properly released when the MEX is cleared.
 *
 * Calling in MATLAB:
 * [MEX filename]('[data filename]', center_freq_hz, sample_rate_sps, num_samples, gain_db)
 *
 * Example:
 * rx_samples2file_MATLAB('my_data.dat', 1.0e9, 10e6, 500000, 20.0);
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
#include <memory> // For std::unique_ptr

// Use the C++11 typedef for complex float (same as UHD's "fc32")
typedef std::complex<float> fc32_t;

/**
 * @brief Manages the lifecycle of the UHD device.
 *
 * This class uses the RAII pattern. A static instance will be created
 * when the MEX file is first loaded. Its constructor finds and
 * initializes the SDR. Its destructor is called automatically
 * when the MEX file is unloaded (e.g., 'clear receive'),
 * safely releasing the device.
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
            // An empty device_addr_t() finds the first device.
            usrp = uhd::usrp::multi_usrp::make(uhd::device_addr_t());
            mexPrintf("UHDDeviceManager: Device found and initialized.\n");
        } catch (const uhd::exception& e) {
            // Critical error, we can't continue.
            // Using mexErrMsgIdAndTxt will stop the MEX from loading.
            mexErrMsgIdAndTxt("UHD:DeviceInitFailed", e.what());
        }
    }

    /**
     * @brief Destructor: Called when MEX is unloaded.
     */
    ~UHDDeviceManager() {
        mexPrintf("UHDDeviceManager: Releasing SDR device...\n");
        // The sptr's destructor will be called automatically,
        // which handles all UHD device cleanup.
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

    // We expect 5 inputs, 0 outputs
    if (nrhs != 5) {
        mexErrMsgIdAndTxt("UHD:receive:invalidNumInputs",
            "Five inputs required: (filename, freq, rate, num_samps, gain)");
    }
    if (nlhs > 0) {
        mexErrMsgIdAndTxt("UHD:receive:maxlhs",
            "Zero output arguments expected.");
    }

    // Check input types
    if (!mxIsChar(prhs[0])) {
        mexErrMsgIdAndTxt("UHD:receive:invalidInput", "Input 1 (filename) must be a string.");
    }
    if (!mxIsDouble(prhs[1]) || mxIsComplex(prhs[1])) {
        mexErrMsgIdAndTxt("UHD:receive:invalidInput", "Input 2 (freq) must be a real double scalar.");
    }
    if (!mxIsDouble(prhs[2]) || mxIsComplex(prhs[2])) {
        mexErrMsgIdAndTxt("UHD:receive:invalidInput", "Input 3 (rate) must be a real double scalar.");
    }
    if (!mxIsDouble(prhs[3]) || mxIsComplex(prhs[3])) {
        mexErrMsgIdAndTxt("UHD:receive:invalidInput", "Input 4 (num_samps) must be a real double scalar.");
    }
    if (!mxIsDouble(prhs[4]) || mxIsComplex(prhs[4])) {
        mexErrMsgIdAndTxt("UHD:receive:invalidInput", "Input 5 (gain) must be a real double scalar.");
    }

    // --- 2. Parse Inputs ---

    // mxArrayToString is a C-style function, we must free its memory.
    // We use a std::unique_ptr for exception-safe cleanup.
    std::unique_ptr<char, decltype(&mxFree)> filename_c_str(mxArrayToString(prhs[0]), &mxFree);
    std::string filename(filename_c_str.get());

    double center_freq = mxGetScalar(prhs[1]);
    double sample_rate = mxGetScalar(prhs[2]);
    size_t num_samples = static_cast<size_t>(mxGetScalar(prhs[3]));
    double gain = mxGetScalar(prhs[4]);

    // Get the persistent USRP object
    uhd::usrp::multi_usrp::sptr usrp = sdr_device.usrp;

    // Check if the static object's init failed (e.g., no device)
    if (!usrp) {
        mexErrMsgIdAndTxt("UHD:receive:deviceNotReady",
            "SDR device is not initialized. 'clear receive' and try again.");
        return;
    }

    // --- 3. Configure SDR and Stream ---

    try {
        mexPrintf("Configuring SDR: Freq=%.2f MHz, Rate=%.2f Msps, Gain=%.1f dB\n",
            center_freq / 1e6, sample_rate / 1e6, gain);

        // Set all RX parameters
        usrp->set_rx_rate(sample_rate);
        usrp->set_rx_freq(center_freq);
        usrp->set_rx_gain(gain);
        // We'll just use the first channel (channel 0)
        usrp->set_rx_antenna("RX1", 0); // Check your device for valid antenna names

        // Create a receive streamer
        // "fc32" means complex<float>
        uhd::stream_args_t stream_args("fc32");
        stream_args.channels = {0}; // Use channel 0
        uhd::rx_streamer::sptr rx_stream = usrp->get_rx_stream(stream_args);

        // Open the output file
        std::ofstream outfile(filename, std::ofstream::binary);
        if (!outfile) {
            mexErrMsgIdAndTxt("UHD:receive:fileOpenFailed", "Could not open output file.");
        }

        // --- 4. Stream Data ---

        // Setup streaming command
        uhd::stream_cmd_t stream_cmd(uhd::stream_cmd_t::STREAM_MODE_NUM_SAMPS_AND_DONE);
        stream_cmd.num_samps = num_samples;
        stream_cmd.stream_now = true;
        rx_stream->issue_stream_cmd(stream_cmd);

        // This is our receive buffer.
        // We make it big to mitigate overflow.
        std::vector<fc32_t> rx_buff(rx_stream->get_max_num_samps());
        size_t total_samples_recd = 0;

        mexPrintf("Streaming %zu samples to %s...\n", num_samples, filename.c_str());

        while (total_samples_recd < num_samples) {
            // Determine how many samples to request in this batch
            size_t num_to_recv = std::min(rx_buff.size(), num_samples - total_samples_recd);

            uhd::rx_metadata_t md;
            size_t num_recd = rx_stream->recv(rx_buff.data(), num_to_recv, md, 3.0); // 3.0s timeout

            // Check for errors
            if (md.error_code != uhd::rx_metadata_t::ERROR_CODE_NONE) {
                mexWarnMsgIdAndTxt("UHD:receive:streamError", md.strerror().c_str());
            }

            // Write the received samples to the file
            outfile.write(reinterpret_cast<const char*>(rx_buff.data()), num_recd * sizeof(fc32_t));

            total_samples_recd += num_recd;
        }

        // Cleanup
        outfile.close();
        mexPrintf("Streaming complete. Received %zu samples.\n", total_samples_recd);

    } catch (const uhd::exception& e) {
        mexErrMsgIdAndTxt("UHD:RuntimeError", e.what());
    } catch (const std::exception& e) {
        mexErrMsgIdAndTxt("STD:RuntimeError", e.what());
    }

    return;
}
