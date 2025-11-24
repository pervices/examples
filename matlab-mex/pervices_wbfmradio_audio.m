%==========================================================================
% test_pervices_wbfm_offset.m
%
% Per Vices MatLab Implementation
% Copyright (C) 2025 Per Vices Corporation
%
% SPDX-ID: GPL-3.0
%
% This program is free software: you can redistribute it and/or modify
% it under the terms of the GNU General Public License as published by
% the Free Software Foundation, either version 3 of the License, or
% (at your option) any later version.
%
% This program is distributed in the hope that it will be useful,
% but WITHOUT ANY WARRANTY; without even the implied warranty of
% MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
% GNU General Public License for more details.
%
% You should have received a copy of the GNU General Public License
% along with this program.  If not, see <http://www.gnu.org/licenses/>.
%==========================================================================
%
% This script implements "Offset Tuning" in software.
% This is a common way to take advantage of Per Vices' SDRs large BW.
%
% 1. We tune the SDR *hardware* to a frequency *near* our target.
% 2. We use a Digital Down-Converter (DDC) to digitally
%    shift our target station to 0 Hz, filter, and decimate it.
%==========================================================================
clear; clc; close all;

% --- Tunable Parameters ---
SDR_DEVICE_ARGS = 'crimson';
SDR_GAIN        = 75;
SDR_ANTENNA     = 'A';
SDR_CHANNEL     = [0];

% --- Target & Offset Tuning ---
TARGET_FREQ     = 95.5e6;    % <<< Your 95.5 MHz station
HW_OFFSET       = 500e3;     % 500 kHz offset
HW_CENTER_FREQ  = TARGET_FREQ + HW_OFFSET; % Tune hardware to 96.0 MHz
DDC_OFFSET_FREQ = HW_OFFSET; % Digitally shift by -500 kHz

% --- DSP Parameters ---
SDR_SAMPLE_RATE   = 1e6;       % 1 Msps (Good for WBFM)
DDC_DECIMATION    = 4;         % 1e6 / 4 = 250e3
DDC_OUTPUT_RATE   = SDR_SAMPLE_RATE / DDC_DECIMATION; % 250 kHz
DDC_BANDWIDTH     = 200e3;     % WBFM channel bandwidth
AUDIO_SAMPLE_RATE = 48000;     % 48 kHz

% --- CHUNK_SIZE MATH (CRITICAL) ---
% 1. DDC decimates by 4.
% 2. FMDemod's internal resampler (250/48) decimates by 125.
% 3. CHUNK_SIZE must be a multiple of (4 * 125) = 500
CHUNK_SIZE = 8000 * 4;  % 32000 (multiple of 500)
TIMEOUT_MS = 1000;

% --- Setup MATLAB DSP Objects ---
fprintf('Setting up DSP objects...\n');

% 1. Digital Down-Converter (DDC)
% This block is our "freq_xlating_fir_filter"
ddc = dsp.DigitalDownConverter(...
    'SampleRate', SDR_SAMPLE_RATE, ...
    'DecimationFactor', DDC_DECIMATION, ...
    'CenterFrequency', DDC_OFFSET_FREQ, ... % Apply the -500 kHz shift
    'Bandwidth', DDC_BANDWIDTH);

% 2. WBFM Demodulator (takes 250 kHz in)
fm_demod = comm.FMBroadcastDemodulator(...
    'SampleRate', DDC_OUTPUT_RATE, ...
    'AudioSampleRate', AUDIO_SAMPLE_RATE, ...
    'Stereo', false);

% 3. Audio Player
audio_player = audioDeviceWriter(...
    'SampleRate', AUDIO_SAMPLE_RATE, ...
    'SupportVariableSizeInput', true);

% --- Initialize SDR ---
fprintf('Opening SDR with args: %s\n', SDR_DEVICE_ARGS);
try
    sdr('open', SDR_DEVICE_ARGS);
catch ME
    fprintf(2, 'Failed to open SDR. Is it connected and is the MEX file in the path?\n');
    rethrow(ME);
end

% --- CRITICAL: Use onCleanup to guarantee sdr('close') ---
cleanup_obj = onCleanup(@() sdr_interface_cleanup);

% Configure RX
rx_config.rate      = SDR_SAMPLE_RATE;
rx_config.freq      = HW_CENTER_FREQ;  % Tune hardware to 96.0 MHz
rx_config.gain      = SDR_GAIN;
rx_config.bw        = SDR_SAMPLE_RATE;
rx_config.antenna   = char(SDR_ANTENNA);
rx_config.channels  = SDR_CHANNEL;

fprintf('Configuring RX Hardware to %.2f MHz (Target is %.2f MHz)...\n', ...
    HW_CENTER_FREQ/1e6, TARGET_FREQ/1e6);
sdr('config_rx', rx_config);

% Start the continuous stream
fprintf('Starting RX stream...\n');
sdr('start_rx_stream');

% --- Main Processing Loop ---
fprintf('Starting processing loop. Press Ctrl+C to stop.\n');

while true % Run until Ctrl+C
    
    % 1. Get data from SDR
    [rx_samples, meta] = sdr('read_chunk', CHUNK_SIZE, TIMEOUT_MS);
    
    % Check for issues
    if meta.timed_out
        fprintf(2, 'Warning: Read chunk timed out! (Received %d samples)\n', meta.num_recd);
    end
    if meta.num_available > (CHUNK_SIZE * 5)
        fprintf(2, 'Warning: SDR buffer is filling! (%d samples)\n', meta.num_available);
    end
    
    if ~isempty(rx_samples)
        % 2. DDC: Shift (-500k), Filter, Decimate (1MHz -> 250kHz)
        demod_input = ddc(rx_samples(:,1));
        
        % 3. Demodulate (250 kHz -> 48 kHz)
        audio_signal = fm_demod(demod_input);
        
        % 4. Play Audio
        audio_player(audio_signal);
    end
end


% --- Cleanup Function ---
function sdr_interface_cleanup()
    fprintf('\nCleaning up resources...\n');
    
    % Stop the audio player
    if exist('audio_player', 'var') && isLocked(audio_player)
        release(audio_player);
        fprintf('Audio player released.\n');
    end
    
    % Stop the stream and close the SDR
    try
        sdr('stop_rx_stream');
        fprintf('SDR stream stopped.\n');
        sdr('close');
        fprintf('SDR closed.\n');
    catch ME
        fprintf(2, 'Error during SDR cleanup: %s\n', ME.message);
    end
end
