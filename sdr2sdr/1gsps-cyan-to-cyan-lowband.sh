#!/bin/bash

rxaddr=192.168.10.2
txaddr=192.168.11.2
#rxchan=0,1,2,3
#txchan=0,1,2,3
rxchan=0
txchan=0
# chosen for ~-30dBm in
rxgain=15
txgain=20
rxfreq=0
txfreq=0
bandwidth=1000000000
#bandwidth=500000000
#bandwidth=250000000
#bandwidth=10000000

/usr/lib/uhd/examples/rxtx_inter_device_stream --rx_args addr=$rxaddr --tx_args addr=$txaddr --rate $bandwidth --rx_channels $rxchan --tx_channels $txchan --rx_gain $rxgain --tx_gain $txgain --rx_freq $rxfreq --tx_freq $txfreq

