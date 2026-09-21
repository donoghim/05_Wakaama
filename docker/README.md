# Docker Dashboard Runtime

This deployment runs one container. The container starts the dashboard only;
the dashboard Start, Stop, and Restart buttons manage `bootstrap_server` and
`lwm2mserver` in the same container. It does not run systemd.

## Start

```sh
cp .env.example .env
# Set PUBLIC_ENDPOINT and, if necessary, DFOTA_HOST in .env.
docker compose up -d --build
```

Open `http://127.0.0.1:8080`, then start Bootstrap Server and LwM2M Server
from the dashboard. The dashboard is intentionally published only on localhost
by default. Use a VPN or authenticated reverse proxy before setting
`DASHBOARD_BIND_ADDRESS=0.0.0.0`.

The host must allow inbound UDP `22101` and `22102` when devices connect from
outside the host. `PUBLIC_ENDPOINT` must be the public IP address or FQDN that
those devices can reach.

## Persistent data

All mutable state is in `./docker-data`, outside the image:

```text
docker-data/
  config/01_bs_plain.ini  Bootstrap configuration and endpoint list
  backups/                Dashboard backups before INI replacement
  firmware/               Uploaded DFOTA artifacts
  logs/                   Bootstrap and LwM2M server logs
  run/                    Local LwM2M control socket
```

On first start, the entrypoint creates `config/01_bs_plain.ini` from the
template using `PUBLIC_ENDPOINT` and `BOOTSTRAP_LIFETIME`. It does not overwrite
an existing file. To preserve existing endpoint and credential settings, copy
the current `run/bootstrap_server/01_bs_plain.ini` to
`docker-data/config/01_bs_plain.ini` before the first `docker compose up`.

To change `lifetime`, server URI, or endpoints, use the dashboard editor or
edit `docker-data/config/01_bs_plain.ini`, then press Restart Bootstrap in the
dashboard. A device receives the changed lifetime only when it bootstraps again.

## Operations

```sh
docker compose ps
docker compose logs -f
docker compose down
```

Do not use `docker compose down -v` for this deployment: it can remove Docker
volumes. The bind-mounted `docker-data/` directory is the operational backup.

This runtime is for web-based operation. Use the existing host-native console
and tmux scripts for protocol debugging, interactive commands, and packet
captures. Do not run their Bootstrap or LwM2M servers while this container is
using the same UDP ports.