FROM debian:bookworm AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY wakaama ./wakaama
RUN cmake -S wakaama/examples -B build \
    && cmake --build build --target bootstrap_server lwm2mserver -j"$(nproc)"

FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
    gettext-base \
    gosu \
    procps \
    python3 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY --from=builder /src/build/bootstrap_server/bootstrap_server /app/bin/bootstrap_server
COPY --from=builder /src/build/server/lwm2mserver /app/bin/lwm2mserver
COPY dashboard /app/dashboard
COPY docker/start-dashboard.sh /app/bin/start-dashboard
COPY run/bootstrap_server/01_bs_plain.ini /app/defaults/01_bs_plain.ini

RUN chmod 0755 /app/bin/start-dashboard

ENV WAKAAMA_RUNTIME_MODE=managed \
    WAKAAMA_DASHBOARD_HOST=0.0.0.0 \
    WAKAAMA_DASHBOARD_PORT=8080 \
    WAKAAMA_BOOTSTRAP_BINARY=/app/bin/bootstrap_server \
    WAKAAMA_LWM2M_SERVER_BINARY=/app/bin/lwm2mserver \
    WAKAAMA_BOOTSTRAP_INI=/data/config/01_bs_plain.ini \
    WAKAAMA_BACKUP_DIR=/data/backups \
    WAKAAMA_BOOTSTRAP_LOG=/data/logs/bootstrap.log \
    WAKAAMA_SERVER_LOG=/data/logs/server.log \
    WAKAAMA_CONTROL_SOCKET=/data/run/lwm2mserver-control.sock \
    WAKAAMA_DFOTA_FIRMWARE_DIR=/data/firmware

EXPOSE 8080/tcp 22101/udp 22102/udp
VOLUME ["/data"]

ENTRYPOINT ["/app/bin/start-dashboard"]