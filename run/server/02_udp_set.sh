echo "----------- Change UDP session timeout"
sudo modprobe nf_conntrack
echo 3600 > /proc/sys/net/netfilter/nf_conntrack_udp_timeout
echo 3600 > /proc/sys/net/netfilter/nf_conntrack_udp_timeout_stream
