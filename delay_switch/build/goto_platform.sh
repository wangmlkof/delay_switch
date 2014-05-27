tile-monitor --dev usb0 --resume \
 --upload recv /opt/recv \
 --upload send /opt/send \
 --upload switch /opt/switch \
 --upload traf1to2.pcap /opt/traf1to2.pcap \
 --upload run.sh /opt/run.sh \
 --upload show.sh /opt/show.sh \
 --upload down.sh /opt/down.sh \
 --quit

tile-console --serial /dev/tileusb0/console
