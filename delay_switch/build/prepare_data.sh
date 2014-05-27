mac1="001aca00d29e"
mac2="001aca00d29f"
mac3="001aca00d2a0"
mac4="001aca00d2a1"
mac5="001aca00d29a"
mac6="001aca00d29b"
mac7="001aca00d29c"
mac8="001aca00d29d"
#mac_broad="ffffffffffff"
input1="traf3000.pcap"
#input2="traf10.pcap"
./generator -f 1 -s $mac1 -d $mac2 -i $input1 -o traf1to2.pcap
#./generator -f 1 -s $mac1 -d $mac_broad -i $input2 -o traf1tob.pcap
