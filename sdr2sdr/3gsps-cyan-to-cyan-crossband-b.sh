#!/bin/bash

rxaddr=192.168.10.2
txaddr=192.168.11.2
#rxchan=0,1,2,3
#txchan=0,1,2,3
rxchan=1
txchan=1
# chosen for ~-10dBm in
rxgain=49
txgain=24
rxfreq=2e9
txfreq=0
bandwidth=3e9

/usr/lib/uhd/examples/rxtx_inter_device_stream --rx_args addr=$rxaddr --tx_args addr=$txaddr --rate $bandwidth --rx_channels $rxchan --tx_channels $txchan --rx_gain $rxgain --tx_gain $txgain --rx_freq $rxfreq --tx_freq $txfreq --ref external

