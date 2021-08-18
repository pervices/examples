#Created by Renaud Di Bernardo (renaud.d@pervices.com) 2021-07-20
    #edited by Victoria Sipinkarovski (
import matplotlib.pyplot as plt
import sys
import shutil
import os
import binascii
import socket
from pcapng import FileScanner
from pcapng import blocks 


if len(sys.argv) != 7 :
    sys.exit("\n **Error, missing arg : You need to provide : \n\
                1. the pcapng file name \n\
                2. the Dest IP Address of the packet to filter\n\
                3. its UDP Dest port! \n")

if '.pcapng' in str(sys.argv[1]):
    pass
else:
    sys.exit("\n **Error, wrong arg : The file has to be .pcapng! \n")

#########################
######## CLASSES ########
#########################

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
        self.str_id      = (data[4:8]   )
        self.frac_ts     = (data[8:16]  )

        header_length_byte  = 4+4+8
        #print(str(int(binascii.hexlify(self.length), 16)))
        payload_length_byte = (int(binascii.hexlify(self.length), 16)) * 4 - header_length_byte
        self.payload_length_byte = payload_length_byte
        if (self.trailer_inc == 1):
            self.data      = (data[16:16+payload_length_byte-4])
            self.trailer   = (data[16+payload_length_byte-4:16+payload_length_byte])
        else:
            self.data      = (data[16:16+payload_length_byte-1])

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
    if not str(int(binascii.hexlify(udp_pkt.dst_port), 16)) == sys.argv[3]: #17 = UDP     01 = ICMP       06 = TCP  TODO CHANGE THAT
        print("bad dst-port = "+ str(int(binascii.hexlify(udp_pkt.dst_port), 16)))
        return None

    vita_packet = VITA_Packet(udp_packet.data)
    return vita_packet


#Get the EnhancedPacket blocks from the file passed in Arg
#pbs = get_pacpng_packet_blocks('./'+str(sys.argv[1]))
pbs = get_pacpng_packet_blocks(str(sys.argv[1]))
num_total_pkts=len(pbs)
print("Number of packets in the file: ",num_total_pkts)
print('\n')
print("This will take up more time if there are alot of packets")
print('\n')
# 128 bits interface : 16 bytes :
#flags =      [6:3]  2   1    0
#flags = EMPTY[3:0] EOP SOP VALID
# EMPTY[3:0] : 16 values that specify how many bytes (16 bytes = 128bits) are not valid

# 64 bits interface : 8 bytes :
#flags =      [5:3]  2   1    0
#flags = EMPTY[2:0] EOP SOP VALID
# EMPTY[2:0] : 8 values that specify how many bytes (8 bytes = 64bits) are not valid

addr=0
found=0
flag_valid = 1
flag_valid_sop = 3
flag_valid_eop_zeroempty = 5

entire_16bytes_lines =0
relicats =  0
current_pkt = 0
pkt=0
last_line = " "
decade=0
stream_id = 0
old_seq_num = 0
new_seq_num = 0
i_samples = []
q_samples = []
i = 0
for pb in pbs: #For each block in all the packet blocks.
    #Get the ethernet frame from the Packet blocks:
    #print("in for loop")
    eth_frame = get_eth_frame(pb)
    if eth_frame: #If this block happens to be an Ethernet frame
        ipv4_pkt = get_ipv4_packet(eth_frame)
        if ipv4_pkt: #If this Eth Frame happens to be an IPv4 packet (IPV6 not supported yet!)
            if socket.inet_ntoa(ipv4_pkt.dst_ip) == sys.argv[2]: #If this IPv4 pkt IP Destination matches what the user wants
                udp_pkt = get_udp_packet(ipv4_pkt)
                #print ("udp port = " + str(int(binascii.hexlify(udp_pkt.dst_port), 16)))
                #print("in get udp pkt")
                if udp_pkt and str(int(binascii.hexlify(udp_pkt.dst_port), 16)) == sys.argv[3]: #If the IPv4 pkt is a UDP packet and UDP Destmatches what the user wants, this is a packet we are looking for!
                    found = 1
                    vita_pkt = get_vita_packet(udp_pkt)
                    #print("in udp pky if")
                    if pkt == 0:
                        stream_id = vita_pkt.str_id
                        print("stream ID = "+str(int(binascii.hexlify(stream_id), 16)))
                        new_seq_num = vita_pkt.seq_num
                    else:
                        if not vita_pkt.str_id == stream_id:
                            print("ERROR, stream ids don't match, pkt= " + str(pkt))
                        old_seq_num = new_seq_num
                        new_seq_num = vita_pkt.seq_num
                        if not (old_seq_num+1) == new_seq_num:
                            if not ((old_seq_num == 15) and (new_seq_num == 0)):
                                print("ERROR, SEQ NUM not incremented, old = "+ str(old_seq_num) + " new = " +str(new_seq_num))
                                print("ERROR, pkt= " + str(pkt))

                    s=vita_pkt.data 
                    if vita_pkt.trailer_inc == 1:
                        length = vita_pkt.payload_length_byte - 4
                    else:
                        length = vita_pkt.payload_length_byte
                    #print('length = '+str(length))
                    i = 0
                    while i in range (length):
                        i_sample = str(hex(s[i+0]))[2:]+str(hex(s[i+1]))[2:]
                        q_sample = str(hex(s[i+2]))[2:]+str(hex(s[i+3]))[2:]
                        #print(i_sample + ' '  + q_sample)
                        i_samples.append(i_sample)
                        q_samples.append(q_sample)
                        i=i+4
                        #print("found")
                    pkt = pkt+1
                    # Loading-like bar (tells you that the script is not stuck):

                
    #if current_pkt%(num_total_pkts//20) == 0 and found == 1:
    #    decade=current_pkt//(num_total_pkts//20)
    #    sys.stdout.write('\r')
    #    sys.stdout.write("[%-20s] %d%%" %('='*decade, 5*decade))
    #    sys.stdout.flush
    #current_pkt refers here to the actual current packet being processed from all the packets (even if the user doesn't want this one)
    #So the loading bar tells you where you are in the pcapng file.
    current_pkt = current_pkt + 1

print("")
if found == 1: 
    print("stream ID = "+str(int(binascii.hexlify(stream_id), 16)))
    i_samples_dec = []
    for x in range(len(i_samples)):
        i_samples_dec.append(twos_complement(i_samples[x],16))

    q_samples_dec = []
    for y in range(len(q_samples)):
        q_samples_dec.append(twos_complement(q_samples[y],16))
        
    i_samples_bin = []
    for w in range(len(i_samples_dec)):
        temp = "{0:b}".format(i_samples_dec[w])
        i_samples_bin.append(temp)
        
    q_samples_bin = []
    for z in range(len(q_samples_dec)):
        temp = "{0:b}".format(q_samples_dec[w])
        q_samples_bin.append(temp)

    #print(i_samples_dec)

    #Write binary data into files


    print('\n')



    result_dec_flat    = [[],[]]
    result_dec_flat[0] = i_samples_dec
    result_dec_flat[1] = q_samples_dec
    label_result       = [[],[]]
    label_result[0]    = 'I'
    label_result[1]    = 'Q'
    
    temp = sys.argv[1]
    temp2 = temp.replace(".pcapng", "", 1)

    fig, axs = plt.subplots(2, sharex=True, sharey=False, gridspec_kw={'hspace': 0})   
    fig.suptitle('VITA Packet analyzed: '+sys.argv[6]+"-sdr2disk-"+sys.argv[5]+"-"+sys.argv[4]+"-"+sys.argv[3])

    index = 0
    for ax in axs.flat:
        ax.set(xlabel='Sample', ylabel='Level')
    #    ax.title.set_text(' ' + str(index))
        ax.plot(result_dec_flat[index], linestyle = 'solid', linewidth = 0.9, color = 'blue', label = label_result[index]) 
        ax.legend(loc="upper right")
        index = index+1

    if found == 1:
        print("Saving binary values into files")
        file = open(sys.argv[6]+"-sdr2disk-"+sys.argv[5]+"-"+sys.argv[4]+"-"+sys.argv[3]+"bin_val.txt", "w+")
        #file = open(sys.argv[6]+"-sdr2disk-"+sys.argv[5]+"-"+sys.argv[4]+"-"+sys.argv[3]+"bin_val.txt", "w+")
        file.write("i_samples = " + str(i_samples_bin) + "\nq_samples = " + str(q_samples_bin))
        file.close()
        shutil.move(sys.argv[6]+"-sdr2disk-"+sys.argv[5]+"-"+sys.argv[4]+"-"+sys.argv[3]+"bin_val.txt", 'bin_val_files')
        #file.write("i_samples = " )
        #for i in len(i_samples_bin):
        #    i -= 1
        #    file.write(str(i_samples_bin[i])
        #    file.write("\n")
        #file.write("\nq_samples = ")
        #for y in len(q_samples_bin):
        #    y -= 1
        #    file.write(str(q_samples_bin[y])
        #    file.write("\n")
        #file.close()
        #shutil.move(sys.argv[6]+"-sdr2disk-"+sys.argv[5]+"-"+sys.argv[4]+"-"+sys.argv[3]+"bin_val.txt", 'bin_val_files')

    #val = 'o'
    if found == 1: 
        print('Found '+ str(pkt) +' packets in the capture. Done.')
        #while (val != 'y') and (val != 'n'):
            #val = input("Would you like to see the graph? y or n \n")
            #if val == 'y':
        print("Will graph the first 5000 samples")
        plt.xlim([0, 5000])
        plt.savefig(sys.argv[6]+"-sdr2disk-"+sys.argv[5]+"-"+sys.argv[4]+"-"+sys.argv[3]+"Figure5000Samples.png", bbox_inches='tight')
        #plt.show()
        shutil.move(sys.argv[6]+"-sdr2disk-"+sys.argv[5]+"-"+sys.argv[4]+"-"+sys.argv[3]+"Figure5000Samples.png", 'bin_val_files')
        
    else:
        print('Destination IP address not present in the capture.')
else:
    print("No UDP packet with given port number: "+ sys.argv[3] +" and address: " + sys.argv[2])

print("Done! Please wait a few moments for this section to end")


