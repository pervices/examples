#!/bin/bash

shopt -s globstar

function help_sum {
    echo -e "Missing arg, please provide; "
    echo -e "\t1. Pcap or Pcapng file ( Provide complete location ie: /storage0/storage/1629230682.356592/1629230684.105763.pcap )"
    echo -e "\t2. Destination Address ( For 9R, Channels A & B: 10.10.10.10 , Channels C & D: 10.10.11.10, Channels E & F: 10.10.12.10, Channels G H & I: 10.10.13.10 )"
    echo -e "\t3. Desired Port number ( Channel A: 42836, Channel B: 42837, Channel C: 42838, Channel D: 42839, Channel E: 42840, Channel F: 42841, Channel G: 42842, Channel H: 42843 , Channel I: 42844)"
    echo -e "\t4. Name for the savefile"
    echo -e "\tExample: "
    echo -e "\t\tbash parse_pcap.sh filename 10.10.10.10 42836 test0\n"
    echo -e "\t\tbash parse_pcap.sh filename 10.10.10.10 42837 test1\n"
    echo -e "\t\tbash parse_pcap.sh filename 10.10.11.10 42838 test2\n"
    echo -e "\t\tbash parse_pcap.sh filename 10.10.11.10 42839 test3\n"
    echo -e "\t\tbash parse_pcap.sh filename 10.10.12.10 42840 test4\n"
    echo -e "\t\tbash parse_pcap.sh filename 10.10.12.10 42841 test5\n"
    echo -e "\t\tbash parse_pcap.sh filename 10.10.13.10 42842 test6\n"
    echo -e "\t\tbash parse_pcap.sh filename 10.10.13.10 42843 test7\n"
    echo -e "\t\tbash parse_pcap.sh filename 10.10.13.10 42844 test8\n"
    exit
    
}

set -e

# Internal parameters
SFP_PORT_A_STORAGE='/storage0/storage' #For the storage directories, make sure to allow the n2disk user read and write priviledges
SFP_PORT_B_STORAGE='/storage1/storage' #For the storage directories, make sure to allow the n2disk user read and write priviledges
SFP_PORT_C_STORAGE='/storage2/storage' #For the storage directories, make sure to allow the n2disk user read and write priviledges
SFP_PORT_D_STORAGE='/storage3/storage' #For the storage directories, make sure to allow the n2disk user read and write priviledges
declare -a DESTADDR=(10.10.10.10 10.10.11.10 10.10.12.10 10.10.13.10)
declare -a PORTS=(42836 42837 42838 42839 42840 42841 42842 42843)
declare -a CHANNELS=(A B C D E F G H)

 #Creating directory to store binary and graphs
sudo mkdir -p /storage0/storage/bin_val_files
sudo mkdir -p /storage1/storage/bin_val_files
sudo mkdir -p /storage2/storage/bin_val_files
sudo mkdir -p /storage3/storage/bin_val_files

file=$1
RUN_TIME_DATE=$(TZ=UTC date "+%F-%H%M%S%N")

if [ "${file: -5}" == ".pcap" ] ; then
    temp=$file
    temp+="ng"
    sudo tshark -F pcapng -r $file -w $temp
    sudo rm $file
    sudo python3 vita_pcapng_to_binary.py $temp $2 $3 $4-sdr2disk-$RUN_TIME_DATE
    exit
fi

if [ "${file: -7}" == ".pcapng" ] ; then
    sudo python3 vita_pcapng_to_binary.py $file $2 $3 $4-sdr2disk-$RUN_TIME_DATE
    exit
fi

help_sum
exit

