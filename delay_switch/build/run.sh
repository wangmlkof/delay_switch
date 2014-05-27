sh down.sh
sh show.sh
./recv  -n 2800  --link xgbe6 &
./switch &
./send -n 3000 --link xgbe1 --delay 100 --prepare 15 traf1to2.pcap
sh show.sh
