script_dir=$(cd "$(dirname "$0")" && pwd)
cd "$script_dir"

echo "----- start bootstrap server with No Security"
../../build/bootstrap_server/bootstrap_server -l 22101 -4 -f 01_bs_plain.ini
