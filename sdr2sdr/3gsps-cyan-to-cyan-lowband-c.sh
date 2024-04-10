#!/bin/bash

rxaddr=192.168.10.2
txaddr=192.168.11.2
#rxchan=0,1,2,3
#txchan=0,1,2,3
rxchan=2
txchan=2
# chosen for ~-30dBm in
rxgain=20
txgain=24
rxfreq=0
txfreq=0
bandwidth=3e9

/usr/lib/uhd/examples/rxtx_inter_device_stream --rx_args addr=$rxaddr --tx_args addr=$txaddr --rate $bandwidth --rx_channels $rxchan --tx_channels $txchan --rx_gain $rxgain --tx_gain $txgain --rx_freq $rxfreq --tx_freq $txfreq --ref external

