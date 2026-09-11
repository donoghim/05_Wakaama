cmake_minimum_required(VERSION 3.13)

project(lwm2mserver C)

include(../../wakaama.cmake)

add_executable(lwm2mserver lwm2mserver.c)
# LWM2M_SERVER_MODE also enables server-only CoAP quirks/diagnostics in coap/er-coap-13/er-coap-13.c
# (see #ifdef LWM2M_SERVER_MODE guards there) for compatibility with modems observed in the field.
target_compile_definitions(lwm2mserver PRIVATE LWM2M_SERVER_MODE LWM2M_WITH_LOGS DEBUG=1 COAP_RESPONSE_TIMEOUT=5)
target_sources_wakaama(lwm2mserver)
target_sources_shared(lwm2mserver)
