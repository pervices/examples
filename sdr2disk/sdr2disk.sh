#/bin/bash

# This script starts n2disk threads to record data from cyan

set -Eeuo pipefail

# Debug for initial run
#set -x

# #Set flags
# # -e : Exit immediately on command failure
# # -o pipefail : propagate exit codes on pipes to right most.
# # -u : treat unset variables as an error
# # -x : print each command prior to executing it.
# # -E : ensure that errors are caught and cleaned up.

# trap "echo Trap was triggered" cleanup SIGINT SIGTERM ERR EXIT

# Internal parameters
SFP_PORT_A='nt:stream0'
SFP_PORT_A_STORAGE='/storage0/storage' #For the storage directories, make sure to allow the n2disk user read and write priviledges
SFP_PORT_B='nt:stream1'
SFP_PORT_B_STORAGE='/storage1/storage' #For the storage directories, make sure to allow the n2disk user read and write priviledges
SFP_PORT_C='nt:stream2'
SFP_PORT_C_STORAGE='/storage2/storage' #For the storage directories, make sure to allow the n2disk user read and write priviledges
SFP_PORT_D='nt:stream3'
SFP_PORT_D_STORAGE='/storage3/storage' #For the storage directories, make sure to allow the n2disk user read and write priviledges
PACKET_SIZE='8958' #In Bytes (-s)
MAX_FILE_LENGTH='250' #In MBytes (-p)
BUFFER_LENGTH='4096' #In MBytes (-b)
POLL_DURATION='1' #In usec (-q)
CHUNK_LENGTH='4096' #In KBytes (-C)



###
# Compile date and time
###
RUN_TIME_DATE=$(TZ=UTC date "+%F-%H%M%S%N")


#Help Summary;
function help_summary {
    echo -e "Usage : ./sdr2disk -n [sfpA &| sfpB &| sfpC &| sfpD] -t [CAPTURE TIME IN SECONDS] -o [FILENAME]\n"
    echo -e "Any combination of ports can be used seperated by commas, please make sure you have enough storage for the specified capture time, filename will be followed by date and time of run."
    echo -e "Examples:"
    echo -e "\t ./sdr2disk.sh -p sfpA,sfpB,sfpC,sfpD -t 10 -o run1\n"
    echo -e "\t ./sdr2disk.sh -p sfpA,sfpB,sfpD -t 100 -o run2\n"
    echo -e "\t ./sdr2disk.sh -p sfpC,sfpD -t 500 -o run3\n"
    echo -e "\t ./sdr2disk.sh -p sfpA -t 1000 -o run4\n"
    exit
}


function containsElement () {
  local e match="$1"
  shift
  for e; do [[ "$e" == "$match" ]] && return 0; done
  return 1
}

if:IsSet() {
  [[ ${!1-x} == x ]] && return 1 || return 0
}

####
# Main script
####


while getopts ":hp:t:o:" opt; do
    case $opt in
        p ) echo "Ports = $OPTARG "
            set -f # disable glob
            IFS=',' # split on space characters
            ports_array=($OPTARG) ;; # use the split+glob operator
        t ) echo "Capture Time = $OPTARG" 
            capture_time=$OPTARG;;
        o ) echo "File prefix = $OPTARG"
            file_prefix=$OPTARG ;;
        * ) help_summary ;;
    esac
done

#Error Checks:
if:IsSet ports_array || help_summary
if:IsSet capture_time || help_summary
if:IsSet file_prefix || help_summary

for a in "${ports_array[@]}"
do
    if [[ "$a" =~ ^(sfpA|sfpB|sfpC|sfpD)$ ]]; then
        PORT=1
    else
        help_summary
    fi

done

if:IsSet PORT || help_summary

if ! [[ $capture_time =~ ^\+?\.?[0-9]+\.?[0-9]*$ ]] || [[ $capture_time =~ ^[\+-]?0?\.?0*$ ]] && ! [[ $capture_time =~ ^\.*$ ]]
then
    help_summary
fi

if [[ $file_prefix == "." ]] || [[ $file_prefix == ".." ]]; then
    # "." and ".." are added automatically and always exist, so you can't have a
    # file named . or .. //
    help_summary
fi
val=$(echo "${#file_prefix}")
if [ $val -gt 255 ]; then
   # String's length check
   help_summary
fi

if ! [[ $file_prefix =~ ^[0-9a-zA-Z._-]+$ ]]; then
    # Checks whether valid characters exist
    help_summary
fi

_key=$(echo $file_prefix | cut -c1-1)
if ! [[ $_key =~ ^[0-9a-zA-Z.]+$ ]]; then
    # Checks the first character
    help_summary
fi

#Run n2disk threads
#If you want to generate index files add the following flags after 'n2disk', FLAGS= -ZI
if { containsElement "sfpA" "${ports_array[@]}" == 0 ;}
then
    ntpl -e "Assign[streamid=0] = port == 0"
    eval "sudo n2disk -i $SFP_PORT_A -o $SFP_PORT_A_STORAGE -s $PACKET_SIZE -p $MAX_FILE_LENGTH -b $BUFFER_LENGTH -q $POLL_DURATION -C $CHUNK_LENGTH -S 0 -c 1 -w 2" &
fi

if { containsElement "sfpB" "${ports_array[@]}" == 0 ;}
then
    ntpl -e "Assign[streamid=1] = port == 1"
    eval "sudo n2disk -i $SFP_PORT_B -o $SFP_PORT_B_STORAGE -s $PACKET_SIZE -p $MAX_FILE_LENGTH -b $BUFFER_LENGTH -q $POLL_DURATION -C $CHUNK_LENGTH -S 3 -c 4 -w 5" &
fi

if { containsElement "sfpC" "${ports_array[@]}" == 0 ;}
then
    ntpl -e "Assign[streamid=2] = port == 2"
   eval "sudo n2disk -i $SFP_PORT_C -o $SFP_PORT_C_STORAGE -s $PACKET_SIZE -p $MAX_FILE_LENGTH -b $BUFFER_LENGTH -q $POLL_DURATION -C $CHUNK_LENGTH -S 6 -c 7 -w 8" &
fi

if { containsElement "sfpD" "${ports_array[@]}" == 0 ;}
then
    ntpl -e "Assign[streamid=3] = port == 3"
   eval "sudo n2disk -i $SFP_PORT_D -o $SFP_PORT_D_STORAGE -s $PACKET_SIZE -p $MAX_FILE_LENGTH -b $BUFFER_LENGTH -q $POLL_DURATION -C $CHUNK_LENGTH -S 9 -c 10 -w 11" &
fi

sleep 5
sleep $capture_time
eval "sudo pkill -U n2disk"
sleep 2
ntpl -e "Delete = All"
#bash parse_pcap.sh $RUN_TIME_DATE $file_prefix
