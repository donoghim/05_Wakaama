echo "----- start LWM2M server IPv4 port 22102 with control socket /tmp/lwm2mserver-control.sock"
./build/sender \
  -t 3600 \
  -c 0 \
  -r /10250/0/1 \
  -d now \
  -i -v
