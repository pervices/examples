#!/bin/bash

rxaddr=192.168.10.2
txaddr=192.168.11.2
#rxchan=0,1,2,3
#txchan=0,1,2,3
rxchan=0
txchan=0
# chosen for ~-10dBm in
rxgain=40
txgain=15
rxfreq=2e9
txfreq=2e9
bandwidth=1e9

/usr/lib/uhd/examples/rxtx_inter_device_stream --rx_args addr=$rxaddr --tx_args addr=$txaddr --rate $bandwidth --rx_channels $rxchan --tx_channels $txchan --rx_gain $rxgain --tx_gain $txgain --rx_freq $rxfreq --tx_freq $txfreq --ref external

