script_dir=$(cd "$(dirname "$0")" && pwd)
cd "$script_dir"

echo "----- start bootstrap server with PSK, IPv4 port 22001"
../../build/bootstrap_server/bootstrap_server -l 22001 -4 -f 02_bs_dtls.ini
