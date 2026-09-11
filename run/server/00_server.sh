script_dir=$(cd "$(dirname "$0")" && pwd)
cd "$script_dir"

echo "----- start LWM2M server IPv4 port 22102"
../../build/server/lwm2mserver -4 -l 22102
