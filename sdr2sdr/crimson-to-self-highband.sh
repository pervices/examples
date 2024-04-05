#!/bin/bash

rxaddr=192.168.10.12
txaddr=192.168.10.12
rxchan=1
txchan=0
rxgain=0
txgain=0
rxfreq=1e9
txfreq=1e9
bandwidth=162.5e6
#bandwidth=81.25e6
#bandwidth=10e6

/usr/lib/uhd/examples/rxtx_inter_device_stream --rx_args addr=$rxaddr --tx_args addr=$txaddr --rate $bandwidth --rx_channels $rxchan --tx_channels $txchan --rx_gain $rxgain --tx_gain $txgain --rx_freq $rxfreq --tx_freq $txfreq
