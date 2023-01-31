#Created by Renaud Di Bernardo (renaud.d@pervices.com) 2021-07-20
import matplotlib.pyplot as plt
import sys
import binascii
import socket
from pcapng import FileScanner
from pcapng import blocks 

smp_12bit = 0
if len(sys.argv) != 4 :
    if len(sys.argv) == 5:
        if sys.argv[4] == '12bits':
            smp_12bit = 1
        else:
            sys.exit("\n **Error, missing arg : You need to provide : \n\
                        1. the pcapng file name \n\
                        2. the Dest IP Address of the packet to filter\n\
                        3. its UDP Dest port! \n\
                        4. '12bits' if samples are 12 bits (optional)\n")
    else:
        sys.exit("\n **Error, missing arg : You need to provide : \n\
                    1. the pcapng file name \n\
                    2. the Dest IP Address of the packet to filter\n\
                    3. its UDP Dest port! \n\
                    4. '12bits' if samples are 12 bits (optional)\n")





if '.pcapng' in str(sys.argv[1]):
    pass
else:
    sys.exit("\n **Error, wrong arg : The file has to be .pcapng! \n")

#########################
######## CLASSES ########
#########################
class bcolors:
    HEADER = '\033[95m'
    OKBLUE = '\033[94m'
    OKCYAN = '\033[96m'
    OKGREEN = '\033[92m'
    WARNING = '\033[93m'
    FAIL = '\033[91m'
    ENDC = '\033[0m'
    BOLD = '\033[1m'
    UNDERLINE = '\033[4m'

class EthernetFrame():
    def __init__(self, packet_bytes):
        self._parse_packet(packet_bytes)
    
    def _parse_packet(self, packet_bytes):
        self.dst = packet_bytes[0:6]
        self.src = packet_bytes[6:12]
        self.type = packet_bytes[12:14]
        self.data = packet_bytes[14:]
    
    def __str__(self):
        return 'EthernetFrame:\nDestination: {}\t or: 0x{}\nSource     : {}\t or: 0x{}\nType: {}     or: 0x{}\nData: {}'.format(self.dst, binascii.hexlify(self.dst).decode('ascii'), self.src, binascii.hexlify(self.src).decode('ascii'), self.type, binascii.hexlify(self.type).decode('ascii'), self.data)

class IPv4_Packet():
    def __init__(self, data):
        self._parse_packet(data)
    
    
    def _parse_packet(self, data):
        self.version = data[0] >> 4
        
        # extract header length (number of 32-bit words in the header)
        ihl     = data[0] & int('00001111', 2)
        header_size = ihl * 4 # so multiply by 4 to get the number of bytes
        
        # get the total size of the packet (header + data)
        total_size = int(binascii.hexlify(data[2:4]), 16)
        
        # set the internal values (this also drops the padding from the internet frame)
        self.header = data[0:header_size]
        self.data = data[header_size:total_size]
        self.protocol = data[9]
        self.src_ip = data[12:16]
        self.dst_ip = data[16:20]

class UDP_Packet():
    def __init__(self,data):
        self._parse_packet(data)

    def _parse_packet(self, data):
        self.src_port = data[0:2]
        self.dst_port = data[2:4]
        self.length   = data[4:6]
        self.crc      = data[6:8]
        self.data     = data[8:]

    def __str__(self):
        return 'UDP Packet:\nDestination Port: {}\t or: 0x{}\nSource Port    : {}\t or: 0x{}\nLength: {}     or: 0x{}\n'.format(self.dst_port, binascii.hexlify(self.dst_port).decode('ascii'), self.src_port, binascii.hexlify(self.src_port).decode('ascii'), self.length, binascii.hexlify(self.length).decode('ascii'))


class VITA_Packet():
    def __init__(self, data):
        self._parse_packet(data)

    def _parse_packet(self, data):
        self.pkt_type    = (data[0]     ) >> 4
        self.class_id    = (data[0] &  8) >> 3
        self.trailer_inc = (data[0] &  4) >> 2
        self.ts_type     = (data[1]     ) >> 6
        self.frts_type   = (data[1] & 48) >> 4
        self.seq_num     = (data[1] & 15)
        self.length      = (data[2:4]   ) 
        
        self.stream_id_included      =  (self.pkt_type  == 1)
        self.frac_timestamp_included = ((self.frts_type == 3) or (self.frts_type == 1))
        if (self.stream_id_included == 1):
            self.str_id      = (data[4:8]   )
            data_start       = 8
            if (self.frac_timestamp_included == 1) :
                self.frac_ts     = (data[8:16]  )
                data_start       = 16
        else:
            data_start       = 4
            if (self.frac_timestamp_included == 1) :
                self.frac_ts     = (data[4:12]  )
                data_start       = 12

        #Calculate header length based on what header fields are included or not :
        VRT_LENGTH              = 4;
        STREAM_ID_LENGTH        = 4*self.stream_id_included;
        FRAC_TIMESTAMP_LENGTH   = 8*self.frac_timestamp_included;
        header_length_byte  = VRT_LENGTH + STREAM_ID_LENGTH + FRAC_TIMESTAMP_LENGTH
        #Payload length in bytes :
        payload_length_byte = (int(binascii.hexlify(self.length), 16)) * 4 - header_length_byte
        self.payload_length_byte = payload_length_byte
        if (self.trailer_inc == 1):
            self.data      = (data[data_start:data_start+payload_length_byte-4])
            self.trailer   = (data[data_start+payload_length_byte-4:data_start+payload_length_byte])
        else:
            self.data      = (data[data_start:data_start+payload_length_byte])

    def __str__(self):
        return 'VITA Packet:\nstr_id: {}\t or: 0x{}\nLength: {}     or: 0x{}\n'.format(self.str_id, binascii.hexlify(self.str_id).decode('ascii'), self.length, binascii.hexlify(self.length).decode('ascii'))


#########################
######END CLASSES #######
#########################

##FUNCTION TO TRANSLATE 16b hex signed to INT
def twos_complement(hexstr,bits):
    value = int(hexstr,16)
    if value & (1 << (bits-1)):
        value -= 1 << bits
    return value


################## Pcapng to Enhanced Packets
def get_pacpng_packet_blocks(filename):
    """
    Reads a pcapng file and creates a list of EnhancedPacket Blocks from the file
    """
    i=0
    packet_blocks = []
    with open(filename, 'rb') as fp: #open as read binary
        scanner = FileScanner(fp)
        for block in scanner:
            if isinstance(block, blocks.EnhancedPacket):
                packet_blocks.append(block)
            #if i < 6:
            #    print(packet_blocks)
            #    i=i+1
    
    return packet_blocks


################## Packet block (1 block from the Enhanced Packets) to Ethernet Frame
def get_eth_frame(packet_block):
    # Check that the block's associated interface is the LINKTYPE_ETHERNET (link_type = 1)
    # Src: https://www.winpcap.org/ntar/draft/PCAP-DumpFileFormat.html#appendixLinkTypes
    if not packet_block.interface.link_type == 1:
        return None
    
    packet_data = packet_block.packet_data
    ethernet_frame = EthernetFrame(packet_data)
    return ethernet_frame


################## Ethernet Frame to IPv4 packet
def get_ipv4_packet(ethernet_frame):
    # Check that the ethernet frame has type 0x0800 and is IPv4
    if not ethernet_frame.type == b'\x08\x00':
        return None
    
    ipv4_packet = IPv4_Packet(ethernet_frame.data)
    return ipv4_packet

################## IPV4 Packet to UDP Packet
def get_udp_packet(ipv4_packet):
    #Check that the IPv4 packet has type
    if not ipv4_packet.protocol == 17: #17 = UDP     01 = ICMP       06 = TCP 
        return None

    udp_packet = UDP_Packet(ipv4_packet.data)
    return udp_packet

################## UPD Packet to VITA-49 Packet
def get_vita_packet(udp_packet):
    #Check that the UDP packet has type
    if not str(int(binascii.hexlify(udp_pkt.dst_port), 16)) == sys.argv[3]: #Keep only VITA49 Packets that match the destination port chosen by the user.
        print("bad dst-port = "+ str(int(binascii.hexlify(udp_pkt.dst_port), 16)))
        return None

    vita_packet = VITA_Packet(udp_packet.data)
    return vita_packet


#Get the EnhancedPacket blocks from the file passed in Arg
pbs = get_pacpng_packet_blocks(str(sys.argv[1]))
num_total_pkts=len(pbs)
print("Number of packets in the file: ",num_total_pkts)
print('\n')
# 128 bits interface : 16 bytes :
#flags =      [6:3]  2   1    0
#flags = EMPTY[3:0] EOP SOP VALID
# EMPTY[3:0] : 16 values that specify how many bytes (16 bytes = 128bits) are not valid

# 64 bits interface : 8 bytes :
#flags =      [5:3]  2   1    0
#flags = EMPTY[2:0] EOP SOP VALID
# EMPTY[2:0] : 8 values that specify how many bytes (8 bytes = 64bits) are not valid
found=0

def convert_16b_to_12b(block_of_16bits):
#this function doesn't really convert 16b to 12b, but read 12bits samples packed
#into 16bits blocks, and re write them to 16bits blocks, but with correct mapping
    #print("in function")
    #print(len(block_of_16bits))
    s = ""
    for i in range(len(block_of_16bits)):
        snew = format(block_of_16bits[i], '02x')
        #print("s["+str(i)+"] = "+str(snew))
        s = s + str(snew)
    #at this point we have a string of the whole packet

    sbin =""
    #convert to binary:
    for i in range(len(s)):
        sformattedbin = format(int(s[i],16), '04b')
        sbin = sbin   + format(int(s[i],16), '04b')
    #at this point we have a string of the whole packet in binary

    #Now read 12bits by 12bits and pack them into 16bits blocks
    lensur12 = int(len(sbin)/12)
    s_formatted = []
    for i in range(lensur12):
        this_block = sbin[i*12:i*12+12]
        this_block_int = int(this_block,2)
        this_block_hex = format(this_block_int, '04x')
        this_block_hex_0 = int(this_block_hex[1:2],16) #only last significant digit is not 0
        if (this_block_hex_0 > 7) : #negative number, pad one's
            this_block_hex_0_signed = this_block_hex_0 + 240
        else:
            this_block_hex_0_signed = this_block_hex_0

        this_block_hex_1 = int(this_block_hex[2:4],16)
        #print('function0: '+ str(this_block_hex_0))
        #print('function0_signed: '+ str(this_block_hex_0_signed))
        #print('function1: '+ str(this_block_hex_1))
        s_formatted.append(this_block_hex_0_signed)
        s_formatted.append(this_block_hex_1)
        #s_formatted.append(this_block_int)
        #if i > 8:
        #    sys.exit("\n **endtest \n")

    return s_formatted


error_sid = 0
error_seqnum = 0
current_pkt = 0
pkt=0
decade=0
skip_stream_id = 1
stream_id = 0
old_seq_num = 0
new_seq_num = 0
i_samples = []
q_samples = []

i_samples_per_pkt = []
q_samples_per_pkt = []
i = 0
val = 'o'
max_packets = 0
circle = 0
while (val != 'y') and (val != 'n'):
    val = input("Would you like to circle through all the packets 1 by 1 ? y or n \n")
    if (val == 'n'):
        val = 'o'
        while (val != 'y') and (val != 'n'):
            val = input("Would you like to process all the packets ? y or n \n")
            if val == 'y':
                max_packets = num_total_pkts
                break
            elif val == 'n' :
                mx_packets = input("How many packets would you like to process ? (must be an int) \n")
                while not (mx_packets.isdigit() and int(mx_packets) > 0):
                    mx_packets = input("How many packets would you like to process ? (must be an int) \n")
                max_packets = int(mx_packets)
    elif (val == 'y'):
        max_packets = num_total_pkts
        print('Going to cicle through all the packets one by one and display all the graphs')
        circle = 1

swp = 'o'
swap = 0
#while (swp != 'y') and (swp != 'n'):
#    swp = input("swap bytes of each samples ? y or n \n")
#if (swp == 'y'):
#    swap = 1
#else:
#    swap = 0

for pb in pbs: #For each block in all the packet blocks. (each packet)
    #Get the ethernet frame from the Packet blocks:
    eth_frame = get_eth_frame(pb)
    if (pkt < max_packets) and eth_frame: #If this block happens to be an Ethernet frame
        ipv4_pkt = get_ipv4_packet(eth_frame)
        if ipv4_pkt: #If this Eth Frame happens to be an IPv4 packet (IPV6 not supported yet!)
            if socket.inet_ntoa(ipv4_pkt.dst_ip) == sys.argv[2]: #If this IPv4 pkt IP Destination matches what the user wants
                udp_pkt = get_udp_packet(ipv4_pkt)
                if udp_pkt and str(int(binascii.hexlify(udp_pkt.dst_port), 16)) == sys.argv[3]: #If the IPv4 pkt is a UDP packet and UDP Dest matches what the user wants, this is a packet we are looking for!
                    found = 1
                    vita_pkt = get_vita_packet(udp_pkt)
                    this_isample = []
                    this_qsample = []
                    if pkt == 0:
                        if vita_pkt.stream_id_included:
                            stream_id = vita_pkt.str_id
                            skip_stream_id = 0
                        #print("stream ID = "+str(int(binascii.hexlify(stream_id), 16)))
                        new_seq_num = vita_pkt.seq_num
                    else:
                        if (vita_pkt.stream_id_included) and (not vita_pkt.str_id == stream_id):
                            print(bcolors.BOLD + bcolors.FAIL + "     **ERROR** " + bcolors.ENDC + bcolors.FAIL + " STREAM IDs don't match" + bcolors.ENDC + " At " + bcolors.UNDERLINE + "packet "  + str(pkt) + bcolors.ENDC )
                            error_sid = error_sid +1
                        old_seq_num = new_seq_num
                        new_seq_num = vita_pkt.seq_num
                        if not (old_seq_num+1) == new_seq_num:
                            if not ((old_seq_num == 15) and (new_seq_num == 0)):
                                sys.stdout.write('\r')
                                num_dropped_pkt = (new_seq_num - old_seq_num) % 15
                                print(bcolors.BOLD + bcolors.FAIL + "     **ERROR** " + bcolors.ENDC + bcolors.FAIL + " SEQ NUM not incremented by 1." + bcolors.ENDC + " At " + bcolors.UNDERLINE + "packet "  + str(pkt) + bcolors.ENDC + " Seq_Num goes From "+ str(old_seq_num) + " to " +str(new_seq_num) \
                                    + '. It looks like ' + str(num_dropped_pkt) + ' packets were dropped ?')
                                error_seqnum = error_seqnum +1

                    a=vita_pkt.data 
                    #print(a)
                    if vita_pkt.trailer_inc == 1:
                        length = vita_pkt.payload_length_byte - 4
                    else:
                        length = vita_pkt.payload_length_byte
                    #print('length = '+str(length))
                    if (smp_12bit == 1):
                        #this function doesn't really convert 16b to 12b, but read 12bits samples packed
                        #into 16bits blocks, and re write them to 16bits blocks, but with correct mapping
                        s = convert_16b_to_12b(a)
                        #Each 12-bit block is converted into a 16-bit block
                        #So each 2*12-bit blocks (24b = 3 bytes) is converted into a 2*16-bit block (32b = 4 bytes) 
                        length = int(length / 3) * 4
                    else:
                        s = a
 
                    i = 0
                    while i in range(length):
                        if (swap == 1):
                            i_sample = str(hex(s[i+1]))[2:]+str(hex(s[i+0]))[2:]
                            q_sample = str(hex(s[i+3]))[2:]+str(hex(s[i+2]))[2:]
                        else:
                            i_sample = format(s[i+0], '02x')+ format(s[i+1], '02x')
                            q_sample = format(s[i+2], '02x')+ format(s[i+3], '02x')
                        #print(i_sample + ' '  + q_sample)

                        this_isample.append(i_sample)
                        this_qsample.append(q_sample)
                        i_samples.append(i_sample)
                        q_samples.append(q_sample)
                        i=i+4
                        #print("found")

                    i_samples_per_pkt.append(this_isample)
                    q_samples_per_pkt.append(this_qsample)
                    pkt = pkt+1
                    # Loading-like bar (tells you that the script is not stuck):
    if max_packets > 19 :
        if current_pkt%(num_total_pkts//20) == 0:
            decade=current_pkt//(num_total_pkts//20)
            sys.stdout.write('\r')
            sys.stdout.write("[%-20s] %d%%" %('='*decade, 5*decade))
            sys.stdout.flush
    #current_pkt refers here to the actual current packet being processed from all the packets (even if the user doesn't want this one)
    #So the loading bar tells you where you are in the pcapng file.
    current_pkt = current_pkt + 1

sys.stdout.write('\r')
if found == 1: 
    print('Found '+ str(pkt) +' packets in the capture. Done.')
else:
    sys.exit('Destination IP address and port combination not present in the capture.')

if (circle == 1):
    pltpkt = 0
    for z in range(max_packets):

        #print("stream ID = "+str(int(binascii.hexlify(stream_id), 16)))
        i_samples_dec = []
        for x in range(len(i_samples_per_pkt[z])):
            i_samples_dec.append(twos_complement(i_samples_per_pkt[z][x],16))

        q_samples_dec = []
        for y in range(len(q_samples_per_pkt[z])):
            q_samples_dec.append(twos_complement(q_samples_per_pkt[z][y],16))
        
        #print(i_samples_dec)
        #print(" ")
        #print(q_samples_dec)

        #print('\n')

        result_dec_flat    = [[],[]]
        result_dec_flat[0] = i_samples_dec
        result_dec_flat[1] = q_samples_dec
        label_result       = [[],[]]
        label_result[0]    = 'I'
        label_result[1]    = 'Q'
        
        fig, axs = plt.subplots(2, sharex=True, sharey=False, gridspec_kw={'hspace': 0})   
        fig.suptitle('VITA Packet #' +str(pltpkt) )
        
        index = 0
        for ax in axs.flat:
            ax.set(xlabel='Sample', ylabel='Level')
        #    ax.title.set_text(' ' + str(index))
            ax.plot(result_dec_flat[index], linestyle = 'solid', linewidth = 0.9, color = 'blue', label = label_result[index]) 
            ax.legend(loc="upper right")
            index = index+1

        pltpkt = pltpkt+1
        plt.show()
        val = 'o'
        while (val != 'y') and (val != 'n'):
            val = input("Would you like to see next packet ? y or n \n")
            if val == 'y':
                print("Processing next packet")
            elif val == 'n':
                sys.exit('Done.')
                        
else:

    x7ff  = []
    x800  = []
    x7fff = []
    x8000 = []

    if skip_stream_id == 0:
        print("stream ID = "+str(int(binascii.hexlify(stream_id), 16)))
    i_samples_dec = []
    print("number of samples = " + str(len(i_samples)))
    for x in range(len(i_samples)):
        i_samples_dec.append(twos_complement(i_samples[x],16))
        x7ff.append(2047)
        x800.append(-2048)
        x7fff.append(32767)
        x8000.append(-32768)
    
    #print(i_samples_dec)

    q_samples_dec = []
    for y in range(len(q_samples)):
        q_samples_dec.append(twos_complement(q_samples[y],16))
    
    print('\n')

    result_dec_flat    = [[],[]]
    result_dec_flat[0] = i_samples_dec
    result_dec_flat[1] = q_samples_dec
    label_result       = [[],[]]
    label_result[0]    = 'I'
    label_result[1]    = 'Q'
    
    fig, axs = plt.subplots(2, sharex=True, sharey=False, gridspec_kw={'hspace': 0})   
    fig.suptitle('VITA Packet analyzed')
    
    index = 0
    for ax in axs.flat:
        ax.set(xlabel='Sample', ylabel='Level')
    #    ax.title.set_text(' ' + str(index))
        ax.plot(result_dec_flat[index], linestyle = 'solid', linewidth = 0.9, color = 'blue', label = label_result[index]) 
        if (smp_12bit == 1):
            ax.plot(x7ff, linestyle = 'dashed', linewidth = 0.5, color = 'green', label = 'Max (0x7FF)') 
            ax.plot(x800, linestyle = 'dashed', linewidth = 0.5, color = 'red', label = 'Min (0x800)') 
        else:
            ax.plot(x7fff, linestyle = 'dashed', linewidth = 0.5, color = 'green', label = 'Max (0x7FFF)') 
            ax.plot(x8000, linestyle = 'dashed', linewidth = 0.5, color = 'red', label = 'Min (0x8000)') 
        ax.legend(loc="upper right")
        index = index+1
    
    val = 'o'

    while (val != 'y') and (val != 'n'):
        val = input("Would you like to see the graph? y or n \n")
        if val == 'y':
            plt.show()
  
print("Done!")
