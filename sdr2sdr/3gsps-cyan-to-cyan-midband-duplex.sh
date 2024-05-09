#!/bin/bash

a_args="addr=192.168.10.2"
b_args="addr=192.168.11.2"
rate=3000e6
a_rx_channels=0,1,2,3
a_tx_channels=0,1,2,3
b_rx_channels=0,1,2,3
b_tx_channels=0,1,2,3
a_rx_freq=2e9
b_rx_freq=2e9
a_tx_freq=2e9
b_tx_freq=2e9
a_rx_gain=37
b_rx_gain=37
b_tx_gain=14
a_tx_gain=10

/usr/lib/uhd/examples/rxtx_inter_device_stream --a_args $a_args --b_args $b_args --rate $rate --a_rx_channels $a_rx_channels --a_tx_channels $a_tx_channels --b_rx_channels $b_rx_channels --b_tx_channels $b_tx_channels \
--a_rx_freq $a_rx_freq --b_rx_freq $b_rx_freq --a_tx_freq $a_tx_freq --b_tx_freq $b_tx_freq --a_rx_gain $a_rx_gain --b_rx_gain $b_rx_gain --b_tx_gain $b_tx_gain --a_tx_gain $a_tx_gain

