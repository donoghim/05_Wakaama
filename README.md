# 05_Wakaama

Eclipse Wakaama(LwM2M) 기반의 Bootstrap Server / LwM2M Server / 예제 Client / Sender 통합 워크스페이스.

- 소스: `wakaama/` (하나의 소스 트리에서 4개 실행 파일을 한 번에 빌드)
- 빌드 산출물: `build/` (git에는 포함하지 않음, 재생성 가능)
- 실행 스크립트/설정: `run/` (역할별로 분리)
- 주기적 Write 유틸: `07_sender/`

## 목차

1. [디렉터리 구조](#디렉터리-구조)
2. [사전 준비물](#사전-준비물)
3. [빌드 (make 하는 법)](#빌드-make-하는-법)
4. [시작하는 방법](#시작하는-방법)
5. [Local Dashboard와 systemd 운영](#local-dashboard와-systemd-운영)
6. [세부 사용법](#세부-사용법)
   - [Bootstrap Server 콘솔](#bootstrap-server-콘솔)
   - [EPNS 등록 설정](#epns-등록-설정)
   - [Lifetime 설정](#lifetime-설정)
   - [LwM2M Server 콘솔 명령](#lwm2m-server-콘솔-명령)
   - [서버에서 /10250으로 데이터 write 하는 법](#서버에서-10250으로-데이터-write-하는-법)
   - [Sender로 주기적으로 write 하는 법](#sender로-주기적으로-write-하는-법)
   - [DFOTA 하는 법](#dfota-하는-법)
   - [로그 / 디버그 출력](#로그--디버그-출력)
7. [트러블슈팅](#트러블슈팅)
8. [자주 확인할 것](#자주-확인할-것)

## 디렉터리 구조

```
05_Wakaama/
├── wakaama/                # Wakaama 소스 (bootstrap_server / client / lightclient / server 공용)
│   └── examples/
│       ├── bootstrap_server/
│       ├── client/
│       ├── lightclient/
│       ├── server/         # lwm2mserver.c (DFOTA 로직 포함)
│       └── shared/
├── build/                  # cmake 빌드 산출물 (빌드 후 생성됨, git 추적 안 함)
│   ├── bootstrap_server/bootstrap_server
│   ├── client/lwm2mclient (+ lwm2mclient_tinydtls)
│   ├── lightclient/lightclient
│   └── server/lwm2mserver
├── run/
│   ├── bootstrap_server/   # 00_bs_plain.sh, 01_bs_dtls.sh, *.ini, psk.txt, key.txt
│   ├── server/             # 00_server.sh, 10_server_sender.sh, 11_server_sender_dfota.sh, dfota_fw/
│   └── client/             # 00_client.sh
├── dashboard/              # 로컬 웹 대시보드, systemd user service, 설치 스크립트
└── 07_sender/               # 제어소켓으로 주기적 Write를 요청하는 별도 유틸
```

각 실행 파일(bootstrap_server / lwm2mclient / lwm2mserver / lightclient)은 CMake가 독립적으로 컴파일하므로 하나의 소스 트리에서 동시에 빌드해도 서로 간섭하지 않는다.

## 사전 준비물

- `cmake` (3.13 이상), `make`, `gcc`
- Local Dashboard를 쓰려면 `python3`, `python3-venv`가 필요하다.
  ```sh
  sudo apt install python3-venv
  ```
- (선택) `lwm2mclient_tinydtls`(DTLS 지원 클라이언트)를 빌드하려면 `autoconf`, `automake`, `libtool`이 필요하다.
  ```sh
  sudo apt install autoconf automake libtool
  ```
  설치되어 있지 않아도 NoSec 클라이언트(`lwm2mclient`)와 server/bootstrap_server 빌드에는 영향이 없다.

## 빌드 (make 하는 법)

루트에 있는 `make_*.sh` 스크립트로 빌드한다. 모두 `build/`가 없으면 최초 1회 자동으로 `cmake -S wakaama/examples -B build`를 실행한 뒤, 필요한 타겟만 빌드한다.

| 스크립트 | 빌드 대상 |
|---|---|
| `./make_all.sh` | 핵심 4개 타겟 전부: `bootstrap_server`, `lwm2mclient`, `lwm2mserver`, `lightclient` |
| `./make_bootstrap_server.sh` | `bootstrap_server`만 |
| `./make_server.sh` | `lwm2mserver`만 |
| `./make_client.sh` | `lwm2mclient`(NoSec)만 |
| `./make_lightclient.sh` | `lightclient`만 |

```sh
cd ~/Wakaama/05_Wakaama
./make_all.sh              # 전체 빌드
./make_server.sh            # server만
./make_bootstrap_server.sh  # bootstrap_server만
./make_client.sh            # client(NoSec)만
./make_lightclient.sh       # lightclient만
```

완전히 새로 설정(reconfigure)하려면 `-c` 옵션을 준다:

```sh
./make_all.sh -c
```

빌드 결과:

```text
build/bootstrap_server/bootstrap_server
build/client/lwm2mclient
build/server/lwm2mserver
build/lightclient/lightclient
```

`lwm2mclient_tinydtls`(DTLS 지원 클라이언트)는 `make_*.sh`에 포함되어 있지 않다 (autoconf/automake/libtool 필요). 필요하면 직접 빌드한다:

```sh
cmake --build build --target lwm2mclient_tinydtls
```

스크립트 없이 직접 cmake 명령으로 빌드/개별 타겟 지정도 가능하다:

```sh
cmake -S wakaama/examples -B build
cmake --build build --target lwm2mserver -j"$(nproc)"   # 타겟 하나만
cmake --build build -j"$(nproc)"                         # 전체 (tinydtls 포함, autotools 필요시 실패 가능)
```

`07_sender`는 별도 CMake 프로젝트다.

```sh
cd ~/Wakaama/05_Wakaama/07_sender
rm -rf build
cmake -S . -B build
cmake --build build
```

## 시작하는 방법

터미널을 3개 열고 아래 순서대로 실행한다. (bootstrap → server → client/모뎀)

### 1) Bootstrap Server

```sh
cd ~/Wakaama/05_Wakaama/run/bootstrap_server
./00_bs_plain.sh
```

내부적으로 다음을 실행한다:

```sh
../../build/bootstrap_server/bootstrap_server -l 22101 -4 -f 01_bs_plain.ini
```

- `-4`: IPv4 사용
- `-l 22101`: Bootstrap Server UDP 포트
- `-f 01_bs_plain.ini`: Bootstrap 설정 파일 (NoSec)

PSK/DTLS로 실행하려면 `./01_bs_dtls.sh` (포트 `22001`, `02_bs_dtls.ini`).

### 2) LwM2M Server

```sh
cd ~/Wakaama/05_Wakaama/run/server
./11_server_sender_dfota.sh
```

기본적으로 다음을 실행한다:

```sh
../../build/server/lwm2mserver -4 -l 22102 \
  -p /tmp/lwm2mserver-control.sock \
  -F dfota_fw \
  -u /dfota_fw \
  -H 115.90.109.11
```

- `-4 -l 22102`: IPv4, LwM2M Server UDP 포트
- `-p`: Sender가 사용할 제어 소켓
- `-F dfota_fw -u /dfota_fw`: DFOTA firmware 디렉터리와 CoAP URI prefix
- `-H`: DFOTA Package URI에 들어갈 host/IP (단말이 접근 가능한 주소여야 함)

환경변수로 값을 바꿔 실행할 수 있다:

```sh
DFOTA_HOST=서버_IP LWM2M_PORT=22102 DFOTA_FILE=A02_beta_to_A02.bin ./11_server_sender_dfota.sh
```

DFOTA/Sender 기능 없이 단순 실행만 하려면:

```sh
cd ~/Wakaama/05_Wakaama/run/server
./00_server.sh                 # ./lwm2mserver -4 -l 22102
# 또는 sender 제어 소켓만 필요하면
./10_server_sender.sh          # -p /tmp/lwm2mserver-control.sock 포함
```

> ⚠️ 콘솔 명령 입력(`list`, `read`, `write` 등)이 되려면 **실제 터미널(tty)에서 foreground로 실행**해야 한다. `nohup`/`systemd`/`< /dev/null`처럼 tty가 없는 방식으로 실행하면 콘솔 입력 대기 로직이 비활성화되어 명령을 칠 수 없다 (단, 서버 자체는 정상 동작한다).

### 3) 예제 Client (모뎀 대신 테스트용)

```sh
cd ~/Wakaama/05_Wakaama/run/client
EP=client01 ./00_client.sh
```

내부적으로 다음을 실행한다:

```sh
../../build/client/lwm2mclient -4 -h 127.0.0.1 -p 22102 -n client01
```

`HOST`, `PORT`, `EP` 환경변수로 접속 대상/포트/endpoint 이름을 바꿀 수 있다. 실제 모뎀을 쓴다면 이 단계는 생략하고 모뎀이 Bootstrap Server(`22101`)로 접속하도록 설정한다.

서버에 다음과 같이 표시되면 등록 성공이다:

```text
New client #0 registered.
```

### 4) Sender (선택, 주기적 write)

```sh
cd ~/Wakaama/05_Wakaama/07_sender
./10_sender.sh
```

자세한 내용은 [Sender로 주기적으로 write 하는 법](#sender로-주기적으로-write-하는-법) 참고.

## Local Dashboard와 systemd 운영

기존 tmux/foreground 실행 방식은 콘솔 명령을 직접 입력하거나 디버깅할 때 유지할 수 있다. 로컬 웹 대시보드를 사용하면 Bootstrap Server와 LwM2M Server를 `systemd --user`가 관리하고, 브라우저에서 상태/로그/Bootstrap INI/EPNS/Write/DFOTA를 관리한다.

대시보드는 기본적으로 `127.0.0.1:8080`에만 열리므로 같은 PC의 브라우저에서만 접근할 수 있다.

### 1) 서비스 설치와 시작

처음 한 번 아래 스크립트로 user service 파일을 설치한다.

```sh
cd ~/Wakaama/05_Wakaama
./dashboard/install-bootstrap-service.sh
./dashboard/install-server-service.sh
```

> ⚠️ 기존 tmux에서 Bootstrap Server 또는 LwM2M Server가 실행 중이면 먼저 `Ctrl+C`로 종료한다. 같은 UDP 포트(`22101`, `22102`)는 한 프로세스만 사용할 수 있다.

두 서비스를 시작한다.

```sh
systemctl --user start wakaama-bootstrap.service
systemctl --user start wakaama-server.service
```

상태와 포트를 확인한다.

```sh
systemctl --user is-active wakaama-bootstrap.service wakaama-server.service
ss -lunp | rg ':(22101|22102)\b'
stat -c '%F %a %n' /tmp/lwm2mserver-control.sock
```

정상이면 두 service는 `active`이고 control socket은 아래처럼 표시된다.

```text
소켓 600 /tmp/lwm2mserver-control.sock
```

### 2) Dashboard 시작

```sh
cd ~/Wakaama/05_Wakaama
WAKAAMA_BOOTSTRAP_SERVICE=wakaama-bootstrap.service \
WAKAAMA_SERVER_SERVICE=wakaama-server.service \
./dashboard/start.sh
```

브라우저에서 `http://127.0.0.1:8080`으로 접속한다. 처음 실행하면 `dashboard/.venv` 가상환경을 사용한다. 다른 포트를 사용하려면 아래처럼 실행한다.

```sh
WAKAAMA_DASHBOARD_PORT=8081 \
WAKAAMA_BOOTSTRAP_SERVICE=wakaama-bootstrap.service \
WAKAAMA_SERVER_SERVICE=wakaama-server.service \
./dashboard/start.sh
```

대시보드의 기능은 다음과 같다.

- Bootstrap Server / LwM2M Server process, UDP listener, systemd journal 로그 표시
- `run/bootstrap_server/01_bs_plain.ini` 조회, EPNS 추가/수정, diff 확인, backup 생성, 안전한 적용
- `Restart Bootstrap` 버튼으로 Bootstrap Server만 재시작
- 현재 등록된 endpoint/client ID/lifetime/Object 목록 표시
- 선택된 endpoint에 대한 local control socket 기반 Write 요청
- `run/server/dfota_fw/`의 승인된 firmware 목록과 SHA-256을 표시하고 DFOTA 요청

Write 또는 DFOTA가 `queued`로 표시되면 local control socket 전달은 성공한 것이다. 실제 CoAP 결과와 DFOTA 진행 상태는 오른쪽 Server 로그에서 확인한다. Server 재시작 뒤에는 단말이 다시 Bootstrap/Register 해야 등록 목록에 나타난다.

### 3) service 로그와 제어

`run/dashboard/`에는 아래 번호 기반 helper script가 있다.

| Script | 설명 |
|---|---|
| `00_dashboard_start.sh` | Dashboard 시작 (`127.0.0.1:8080`) |
| `01_server_start.sh` | Bootstrap Server와 LwM2M Server를 함께 시작 |
| `02_server_stop.sh` | 두 service를 함께 중지 |
| `03_server_restart.sh` | 두 service를 함께 재시작 |
| `10_bs_start.sh`, `11_bs_stop.sh`, `12_bs_restart.sh` | Bootstrap Server만 개별 제어 |
| `20_lwm2m_start.sh`, `21_lwm2m_stop.sh`, `22_lwm2m_restart.sh` | LwM2M Server만 개별 제어 |
| `30_status.sh` | service, UDP listener, control socket 상태 확인 |

예를 들어 모든 service를 시작하고 Dashboard를 실행하려면 다음을 사용한다.

```sh
cd ~/Wakaama/05_Wakaama/run/dashboard
./01_server_start.sh
./00_dashboard_start.sh
```

Dashboard와 service를 종료하려면 아래 순서로 실행한다. `00_dashboard_start.sh`를 실행한 terminal에서 `Ctrl+C`를 누르면 Dashboard만 종료되며, Bootstrap Server와 LwM2M Server는 계속 실행된다.

```sh
# 1. Dashboard를 실행한 terminal에서 Ctrl+C

# 2. 다른 terminal에서 두 service를 함께 중지
cd ~/Wakaama/05_Wakaama/run/dashboard
./02_server_stop.sh
```

개별 service만 중지하려면 `./11_bs_stop.sh` 또는 `./21_lwm2m_stop.sh`를 사용한다. 종료 뒤 상태는 `./30_status.sh`로 확인한다.

개별 service를 제어하거나 직접 command를 사용하려면 아래와 같다.

```sh
# 실시간 로그
journalctl --user -u wakaama-bootstrap.service -f
journalctl --user -u wakaama-server.service -f

# 개별 재시작/중지
systemctl --user restart wakaama-bootstrap.service
systemctl --user restart wakaama-server.service
systemctl --user stop wakaama-bootstrap.service
systemctl --user stop wakaama-server.service
```

로그인 시 자동 시작을 원하면 다음을 실행한다.

```sh
systemctl --user enable wakaama-bootstrap.service
systemctl --user enable wakaama-server.service
```

system 재부팅 뒤 사용자 로그인 없이 service를 유지하려면 linger를 별도로 설정한다.

```sh
sudo loginctl enable-linger "$USER"
```

## 세부 사용법

### Bootstrap Server 콘솔

```text
boot URI [NAME]
q
```

- `boot coap://CLIENT_IP:CLIENT_PORT ENDPOINT_NAME` — 서버에서 클라이언트로 bootstrap을 시작한다.
  ```text
  boot coap://192.168.0.10:56830 ASN_CSE-D-548818ac83-QUEC
  ```
  `NAME`을 생략하면 ini 파일에서 이름이 없는 endpoint 설정을 사용한다.
- `q` — 종료.

### EPNS 등록 설정

설정 파일: `run/bootstrap_server/01_bs_plain.ini`

```ini
[Endpoint]
Name=ASN_CSE-D-d726e8d1d6-QUEC
Server=101

[Endpoint]
Name=ASN_CSE-D-5c916f51f3-QUEC
Server=101
```

새 EPNS를 등록하려면 `[Endpoint]` 블록을 추가하고 `Name`에 모듈의 endpoint name을 넣는다. `Server=101`은 같은 파일의 `[Server] id=101` 블록(실제 LwM2M Server 주소/lifetime)을 가리킨다.

### Lifetime 설정

같은 파일의 `[Server]` 블록에서 변경한다.

```ini
[Server]
id=101
uri=coap://115.90.109.11:22102
bootstrap=no
lifetime=7200
security=NoSec
```

수정 후에는 Bootstrap Server를 재시작하고, 단말이 다시 bootstrap을 수행해야 반영된다.

### LwM2M Server 콘솔 명령

`lwm2mserver` 콘솔에서 `help`로 전체 목록을 볼 수 있다.

| 명령 | 설명 | 예시 |
|---|---|---|
| `list` | 등록된 client 목록 | `list` |
| `read CLIENT# URI` | 리소스 읽기 | `read 0 /3/0/0` |
| `disc CLIENT# URI` | Discover | `disc 0 /10250` |
| `write CLIENT# URI DATA` | 값 쓰기 | `write 0 /10250/0/1 1234567890` |
| `update CLIENT# URI DATA` | Partial Update (JSON) | |
| `observe CLIENT# URI` | Observe 시작 | `observe 0 /10250/0/0` |
| `cancel CLIENT# URI` | Observe 취소 | `cancel 0 /10250/0/0` |
| `exec CLIENT# URI [DATA]` | Execute | `exec 0 /5/0/2` |
| `time CLIENT# URI PMIN PMAX` | pmin/pmax attribute | `time 0 /10250/0/0 10 60` |
| `attr CLIENT# URI LT GT [STEP]` | lt/gt/step attribute | `attr 0 /10250/0/0 10 100 1` |
| `clear CLIENT# URI` | attribute 제거 | `clear 0 /10250/0/0` |
| `dfota CLIENT# FILE` | DFOTA 실행 | `dfota 0 A02_beta_to_A02.bin` |
| `q` | 종료 | |

client가 등록되면 콘솔에 `New client #0 registered.`가 출력되고, 여기서 `#0`이 이후 명령에 쓰는 `CLIENT#`이다. client 등록 시 서버가 자동으로 `/10250/0/0`, `/26241/0/0`에 observe를 건다.

### 서버에서 `/10250`으로 데이터 write 하는 법

**방법 1: 콘솔에서 직접 write**

```text
list
write 0 /10250/0/1 1234567890
```

현재 시간(Unix epoch)을 넣고 싶으면 쉘에서 `date +%s`로 값을 만든 뒤 그대로 입력한다.

**방법 2: Sender로 주기적으로 write** — 아래 참고.

### Sender로 주기적으로 write 하는 법

Sender는 LwM2M 패킷을 직접 보내지 않고 `/tmp/lwm2mserver-control.sock`으로 server에 write 요청을 전달한다. server가 `-p /tmp/lwm2mserver-control.sock` 옵션으로 실행 중이어야 한다.

```sh
cd ~/Wakaama/05_Wakaama/07_sender

# 1회 즉시 write 후 3600초마다 현재 시간 write
./build/sender -t 3600 -c 0 -r /10250/0/1 -d now -i -v

# 테스트용: 10초마다 3번만 write
./build/sender -t 10 -c 0 -r /10250/0/1 -d now -i -v -n 3

# 고정 문자열 write
./build/sender -t 60 -c 0 -r /10250/0/1 -d test_message -i -v -n 1

# 기본 제어 소켓이 아닌 다른 소켓 사용
./build/sender -t 60 -c 0 -r /10250/0/1 -d now -i -v -s /path/to/socket
```

- `-t`: 주기(초), `-c`: server 기준 client ID, `-r`: write할 URI, `-d`: 데이터(`now` = 현재 epoch), `-i`: 실행 직후 즉시 1회 write, `-v`: `queued` 로그 출력, `-n`: 반복 횟수 제한

실제 LwM2M Write 응답은 `sender`가 아니라 `run/server`의 `lwm2mserver` 콘솔에 비동기로 출력된다.

### DFOTA 하는 법

1. Firmware 파일을 `run/server/dfota_fw/`에 둔다. (현재 `A02_to_A02_beta.bin`, `A02_beta_to_A02.bin`)
2. DFOTA 옵션으로 server 실행:
   ```sh
   cd ~/Wakaama/05_Wakaama/run/server
   ./11_server_sender_dfota.sh
   # 단말이 접근 불가능한 IP라면
   DFOTA_HOST=192.168.0.100 ./11_server_sender_dfota.sh
   ```
3. 콘솔에서 `list`로 client ID 확인.
4. DFOTA 실행:
   ```text
   dfota 0 A02_beta_to_A02.bin
   dfota 0 A02_to_A02_beta.bin
   ```

서버 내부 동작:

1. `/5/0/3` Firmware State observe
2. `/5/0/1` Package URI에 `coap://HOST:PORT/dfota_fw/FILE` write
3. 단말이 CoAP GET으로 firmware 다운로드
4. `/5/0/3`이 다운로드 완료 상태가 되면 서버가 `/5/0/5` Update Result read
5. `/5/0/5`가 `0`이면 서버가 `/5/0/2` Update execute

수동으로 하려면:

```text
write 0 /5/0/1 coap://115.90.109.11:22102/dfota_fw/A02_beta_to_A02.bin
exec 0 /5/0/2
```

### 로그 / 디버그 출력

server 빌드에는 다음이 켜져 있다 (`wakaama/examples/server/CMakeLists.txt`):

- `LWM2M_WITH_LOGS` — Wakaama 코어 라이브러리 내부 함수 트레이스 (`[registration_step:2131] Entering` 등)
- `DEBUG=1` — CoAP 옵션 파싱/직렬화 저수준 디버그 (`er-coap-13.c`)
- `COAP_RESPONSE_TIMEOUT=5` — CoAP 응답 타임아웃 5초 (기본 2초보다 길게)
- 모뎀 호환용 CoAP quirks(비표준 옵션 2048/2049을 Observe로 처리 등, `LWM2M_SERVER_MODE`에서만 활성화)

이 트레이스는 서버 메인 루프가 이벤트(UDP 패킷/DFOTA 제어소켓/콘솔 입력)를 처리할 때마다 찍히므로, 등록 이후 콘솔에서 Enter만 쳐도 다음 4줄이 반복해서 보일 수 있다:

```text
[lwm2m_step:384] timeoutP: 60
[registration_step:2131] Entering
[transaction_step:460] Entering
[lwm2m_step:491] Final timeoutP: 60
```

이 트레이스는 전부 **stderr**로만 출력되고, 등록/Notify/DFOTA 진행 메시지는 전부 **stdout**으로 출력된다. 콘솔을 깔끔하게 쓰고 싶으면 stderr만 리다이렉트하면 된다:

```sh
# 완전히 버림
./lwm2mserver -4 -l 22102 2>/dev/null

# 파일로 보관 (문제 생기면 확인 가능)
./lwm2mserver -4 -l 22102 2>server-debug.log
```

`run/server/*.sh` 스크립트를 직접 수정해 기본으로 리다이렉트하게 만들어도 된다.

## 트러블슈팅

- **`Error opening socket: 98`**: 해당 UDP 포트(22101/22102 등)를 이미 다른 프로세스가 쓰고 있다. `ss -uln | grep <포트>`, `ps aux | grep -E "bootstrap_server|lwm2mserver"`로 기존 프로세스를 확인 후 종료한다.
- **콘솔 명령이 안 먹힘**: `systemd`처럼 tty 없이 실행하면 server console 입력(`list`, `read`, `write` 등)은 의도적으로 비활성화된다. 이 경우 Dashboard의 등록 목록/Write/DFOTA를 사용하거나, 직접 콘솔 명령이 필요하면 실제 터미널에서 foreground로 실행한다. `lwm2mserver`는 tty가 아닐 때 stdin을 `select()`에서 제외하므로 CPU busy-loop 없이 서버 동작은 유지한다.
- **`lwm2mclient_tinydtls` 빌드 실패 (`autoreconf: 명령어를 찾을 수 없음`)**: `sudo apt install autoconf automake libtool` 설치 후 `build` 디렉터리를 지우고 다시 빌드한다. DTLS가 필요 없다면 무시해도 된다 (`lwm2mclient`, `bootstrap_server`, `lwm2mserver`, `lightclient`는 영향 없음).
- **DFOTA가 진행 안 됨**: `DFOTA_HOST`가 단말이 실제로 접근 가능한 IP인지 확인한다. `dfota_fw` 폴더 안에 파일이 있는지, 파일명에 `/`나 `..`이 없는지 확인한다.

## 자주 확인할 것

- Bootstrap Server 포트는 `22101`, LwM2M Server 포트는 `22102`이다.
- `run/bootstrap_server/01_bs_plain.ini`의 `uri=coap://...`가 실제 server IP/port를 가리켜야 한다.
- 새 EPNS를 추가하거나 lifetime을 바꾼 뒤에는 Bootstrap Server를 재시작해야 한다.
- Sender를 쓰려면 server가 `-p /tmp/lwm2mserver-control.sock` 옵션으로 실행되어 있어야 한다.
- DFOTA 파일은 `run/server/dfota_fw` 아래에 있어야 한다.
- server 콘솔에서 client ID는 항상 `list`로 확인한 값을 사용한다 (서버 재시작 시 바뀔 수 있음).
- 직접 server/bootstrap_server 콘솔 명령을 쓸 때만 실제 터미널(tty) foreground 실행이 필요하다. Dashboard/systemd 운영에서는 console 대신 Dashboard와 `journalctl`을 사용한다.
