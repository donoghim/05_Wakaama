script_dir=$(cd "$(dirname "$0")" && pwd)
cd "$script_dir"

echo "----- start bootstrap server with No Security"
../../build/bootstrap_server/bootstrap_server -l 22121 -4 -f 21_bs_plain_local.ini
