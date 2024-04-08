#!/bin/bash

rxaddr="192.168.10.2"
txaddr="192.168.11.2"
#rxchan=0,1,2,3
#txchan=0,1,2,3
rxchan=0
txchan=0
rxgain=65
txgain=53
rxfreq=15e9
txfreq=15e9
bandwidth=1e9
#bandwidth=500000000
#bandwidth=250000000
#bandwidth=10000000

/usr/lib/uhd/examples/rxtx_inter_device_stream --rx_args addr=192.168.10.2 --tx_args addr=192.168.11.2 --rate $bandwidth --rx_channels $rxchan --tx_channels $txchan --rx_gain $rxgain --tx_gain $txgain --rx_freq $rxfreq --tx_freq $txfreq --ref external

