#!/bin/bash
ip=192.168.10.2
#The ip argument holds the IP address of the SDR. If one host will 
#control multiple SDRs see PVHT-6: Configure a Custom Management IP Address.
#Also note that data may be streamed between 2 different SDRs and if so you 
#will need to set the corresponding IP address for the receiving and
#transmitting SDRs.
channels=0,1,2,3
#The channels argument means that on each line in which multiple 
#configurations are listed they are listed in the order of channels:0 (also
#known as Channel A), 1 (a.k.a. Channel B), etc. If a different number of
#channels is desired, change the number of comma separated numbers on each
#configuration line.
rxfreq=4458000000,4948000000,611250000,686250000
#The rxfreq argement sets the center frequency of each recieve channel in
#Hz. In this example RX Channel A will be tuned to a center frequency of
#4.458GHz, and RX Channel D will be tuned to 686.25MHz.
txfreq=$rxfreq
#The txfreq argument sets the center frequency of each transmit channel 
#in Hz. In this example it is set to use the same frequency for each TX
#channel as the corresponding RX channel.
rxgain=36,46,16,16
#The rxgain argument sets the RF gain setting of each receive channel. 
#Generally higher gain settings are required to compensate for higher 
#losses as the center frequency is higher. In this example the gain 
#settings are set to roughly correspond to a Crimson Unit with input signal 
#at -10dBm at each of the example frequencies.
txgain=32
#The txgain argument sets the RF gain setting of each transmit channel. In 
#this example the RF gain setting of 32 will be applied to all 4 transmit 
#channels. Any argument that is to be the same for all channels can be set 
#with one number like this. If different gain settings are desired for each
#transmit channel, the setting for each channel can be listed in comma 
#separated for just like the rxgain example.
bandwidth=4924242,10833333,731982,625000
#The bandwidth argument sets the sample rate of each pair of channls in 
#samples per second. In this example receive channel A and transmit channel A
#will both be set to 4.924242 MSPS. The sample rate is generally equal to the 
#bandwidth of the signal that will be received and transmitted.

/usr/lib/uhd/examples/rxtx_inter_device_stream --rx_args addr=$ip --tx_args addr=$ip --rx_channels $channels --tx_channels $channels --rate $bandwidth  --tx_gain $txgain --rx_gain $rxgain --tx_freq $txfreq --rx_freq $rxfreq
