# Docker Dashboard Runtime

이 배포는 하나의 container로 동작한다. container가 시작되면 dashboard만 실행하며,
dashboard의 Start, Stop, Restart 버튼이 같은 container 안의 `bootstrap_server`와
`lwm2mserver`를 관리한다. systemd는 사용하지 않는다.

## 시작

```sh
./start_docker.sh
```

처음 실행하면 `.env.example`에서 `.env`를 생성하고 종료한다. `.env`에서
PUBLIC_ENDPOINT와 필요한 경우 DFOTA_HOST를 설정한 뒤 `./start_docker.sh`를 다시 실행한다.
스크립트가 `docker compose up -d --build`를 실행하고 container 상태를 표시한다.

`http://127.0.0.1:8080`을 열고 dashboard에서 Bootstrap Server와 LwM2M Server를
시작한다. dashboard는 기본적으로 localhost에만 공개된다. `DASHBOARD_BIND_ADDRESS=0.0.0.0`으로
변경하기 전에는 VPN 또는 인증된 reverse proxy를 구성한다.

device가 host 외부에서 접속하는 경우 host firewall은 UDP `22101`, `22102` inbound를
허용해야 한다. `PUBLIC_ENDPOINT`는 device가 실제로 도달할 수 있는 public IP address 또는
FQDN이어야 한다.

## Persistent data

변경되는 모든 data는 image 외부의 `./docker-data`에 저장된다.

```text
docker-data/
  config/01_bs_plain.ini  Bootstrap configuration 및 endpoint 목록
  backups/                INI 교체 전 dashboard backup
  firmware/               upload한 DFOTA artifact
  logs/                   Bootstrap 및 LwM2M server log
  run/                    local LwM2M control socket
```

entrypoint는 dashboard와 server를 시작하기 전에 이 directory의 owner를 `PUID:PGID`로
설정한다. 제공되는 `.env.example`은 이 project의 기본값 `1001:1001`을 사용한다. 다른
host에 배포할 때는 `id -u`, `id -g` 결과에 맞춰 값을 변경한다.

최초 시작 시 entrypoint는 project의 `run/bootstrap_server/01_bs_plain.ini`를
`config/01_bs_plain.ini`로 복사한다. 이미 persistent file이 있으면 덮어쓰지 않으므로,
dashboard에서 변경한 endpoint, lifetime, credential은 container 또는 image를 교체해도
유지된다. 다른 host에 배포하기 전에는 source INI의 LwM2M server URI를 device가 도달할 수
있는 public address 또는 FQDN으로 변경한 뒤 image를 build한다.

`lifetime`, server URI, endpoint를 변경하려면 dashboard editor 또는
`docker-data/config/01_bs_plain.ini`를 사용한 뒤 dashboard에서 Restart Bootstrap을 누른다.
변경된 lifetime은 device가 다시 bootstrap할 때 적용된다.

## 운영 명령

Docker runtime을 시작할 때는 project root에서 다음 명령을 사용한다.

```sh
./start_docker.sh
```

Docker runtime을 종료할 때는 다음 명령을 사용한다. `docker/` directory 안에서
실행해도 project root로 이동한 뒤 동일하게 동작한다.

```sh
./docker/stop_docker.sh
```

또는 project root에서 직접 실행할 수 있다.

```sh
./stop_docker.sh
```

두 script 모두 `docker compose down`을 실행하며, container와 Compose network만 종료한다.
`docker-data/`의 설정, endpoint, firmware, backup, log는 삭제하지 않는다.

```sh
docker compose ps
docker compose logs -f
docker compose down
```

이 배포에서 `docker compose down -v`는 사용하지 않는다. Docker volume을 제거할 수 있다.
bind mount한 `docker-data/` directory가 운영 data의 backup이다.

이 runtime은 web 기반 운영용이다. protocol debugging, interactive command, packet capture는
기존 host-native console과 tmux script를 사용한다. 이 container가 같은 UDP port를 사용하는
동안에는 해당 Bootstrap 또는 LwM2M server를 동시에 실행하지 않는다.