DFOTA_HOST=${DFOTA_HOST:-115.90.109.11}
LWM2M_PORT=${LWM2M_PORT:-22102}
DFOTA_FW_ROOT=${DFOTA_FW_ROOT:-dfota_fw}
DFOTA_URI_PREFIX=${DFOTA_URI_PREFIX:-/dfota_fw}
DFOTA_FILE=${DFOTA_FILE:-A02_beta_to_A02.bin}

echo "----- start LWM2M server IPv4 port ${LWM2M_PORT} with control socket /tmp/lwm2mserver-control.sock"
echo "----- DFOTA URI host ${DFOTA_HOST}, firmware root ${DFOTA_FW_ROOT}, URI prefix ${DFOTA_URI_PREFIX}"
echo "----- after client registration, run: dfota CLIENT_ID ${DFOTA_FILE}"
./lwm2mserver -4 -l "${LWM2M_PORT}" -p /tmp/lwm2mserver-control.sock -F "${DFOTA_FW_ROOT}" -u "${DFOTA_URI_PREFIX}" -H "${DFOTA_HOST}"