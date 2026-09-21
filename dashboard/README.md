# Local Dashboard

Bootstrap Server와 LwM2M Server를 위한 읽기 전용 local dashboard다.
기본적으로 `127.0.0.1:8080`에만 bind하며, server command 전송, INI file 변경,
기존 tmux workflow 대체는 하지 않는다.

## 사전 준비

Debian/Ubuntu에서는 아래 OS package를 설치해 virtual environment를 생성한다.

```bash
sudo apt install python3-venv
```

## 시작

repository root에서 실행한다.

```bash
./dashboard/start.sh
```

browser에서 `http://127.0.0.1:8080`을 연다. 다른 local port를 사용하려면:

```bash
WAKAAMA_DASHBOARD_PORT=8081 ./dashboard/start.sh
```

172.x 내부망에서 접속하려면 host를 열어서 실행한다.

```bash
WAKAAMA_DASHBOARD_HOST=0.0.0.0 ./dashboard/start.sh
```

또는 `run/dashboard/00_dashboard_start.sh`를 사용하면 기본적으로 모든 interface에 bind하고,
장비에 172.x address가 있으면 해당 URL을 안내한다.

## Systemd 없이 Dashboard가 서버를 관리하는 방식

`run/dashboard/01_dashboard_managed_start.sh`는 systemd나 tmux 없이 Dashboard만 먼저
시작한다. Browser에서 Bootstrap Server와 LwM2M Server의 `Start` 버튼을 누르면 dashboard가
허용된 실행 파일만 직접 시작하고, `Stop`/`Restart` 버튼도 같은 process를 제어한다.

```bash
cd ~/Wakaama/05_Wakaama
# UDP 22101/22102를 사용하는 기존 user service 또는 tmux server를 먼저 중지한다.
./run/dashboard/01_dashboard_managed_start.sh
```

기본 경로는 기존 build, INI, log, firmware directory를 사용한다. 필요한 경우 아래 환경변수로
테스트 포트와 경로를 바꿀 수 있다.

```bash
WAKAAMA_DASHBOARD_PORT=8081 \
WAKAAMA_BOOTSTRAP_PORT=22111 \
WAKAAMA_LWM2M_PORT=22112 \
./run/dashboard/01_dashboard_managed_start.sh
```

Bootstrap INI를 저장하거나 endpoint/lifetime을 변경한 뒤에는 Dashboard의 `Restart Bootstrap`
버튼을 누른다. Bootstrap Server는 시작 시에만 INI를 읽으므로 이미 provision된 device에는
재-bootstrap 또는 재기동이 필요하다. Dashboard를 종료하면 dashboard가 시작한 두 server도
SIGINT로 정리한다. 이 모드는 interactive server console을 제공하지 않으므로 protocol debugging은
기존 terminal/tmux 실행 방식을 사용한다.

## Bootstrap INI 관리

dashboard는 기본적으로 `run/bootstrap_server/01_bs_plain.ini`를 관리한다. Server ID와
endpoint name을 검증하고, `dashboard/backups/`에 timestamp backup을 저장한 뒤 INI를
atomic replace한다. 변경 사항을 apply해도 Bootstrap Server는 자동으로 restart하지 않는다.

Bootstrap Server를 tmux에서 supervisor가 관리하는 process로 옮길 때는 포함된 user service를 설치한다.

```bash
./dashboard/install-bootstrap-service.sh
# 이 service와 tmux Bootstrap Server는 모두 UDP 22101을 사용하므로 먼저 tmux Server를 중지한다.
systemctl --user start wakaama-bootstrap.service
WAKAAMA_BOOTSTRAP_SERVICE=wakaama-bootstrap.service ./dashboard/start.sh
```

`WAKAAMA_BOOTSTRAP_SERVICE`를 설정하면 Restart Bootstrap button은 해당 user service만
restart하고 Bootstrap log panel은 journal output을 읽는다. tmux-managed test server에서는
browser에 raw tmux command를 넣지 말고 button을 비활성화한 채 사용한다. dashboard는
local-only이며 restart 권한은 dashboard를 시작한 account에 의해 결정된다.

## LwM2M Server Service

현재 test session을 restart할 수 있을 때 LwM2M Server를 tmux에서 포함된 user service로 옮긴다.

```bash
./dashboard/install-server-service.sh
# 이 service와 tmux LwM2M Server는 모두 UDP 22102를 사용하므로 먼저 tmux Server를 중지한다.
systemctl --user start wakaama-server.service
WAKAAMA_BOOTSTRAP_SERVICE=wakaama-bootstrap.service \
WAKAAMA_SERVER_SERVICE=wakaama-server.service \
./dashboard/start.sh
```

service는 `run/server/12_server_commercial.sh`를 실행하므로 상용 등록 시퀀스와
현재 DFOTA launch setting을 함께 사용한다: UDP `22102`, local control socket
`/tmp/lwm2mserver-control.sock`, firmware directory `dfota_fw`, URI prefix
`/dfota_fw`, DFOTA host `115.90.109.11`. Server console input은 비활성화되므로
client 확인, Write, DFOTA action은 dashboard에서 수행한다.

Dashboard의 각 Server panel에는 `Start`, `Restart`, `Stop` button이 있다. button은
`WAKAAMA_BOOTSTRAP_SERVICE`, `WAKAAMA_SERVER_SERVICE`로 설정된 user service만 제어하며,
다른 shell command는 실행하지 않는다. `Stop` 또는 `Restart` 뒤에는 device가 다시
Bootstrap/Register해야 LwM2M Server의 registered-client table에 다시 표시된다.

Service log panel은 현재 service 실행 인스턴스가 시작된 시각 이후의 journal 중 가장
최신 2000개 항목을 조회한다. 화면에는 브라우저 부하를 제한하기 위해 그 결과의 최신
48 KiB를 시간순으로 표시하며, binary 또는 UTF-8이 아닌 message도 대체 문자로 안전하게
표시한다.

## 기존 tmux Pane 연결

기존처럼 Bootstrap Server와 LwM2M Server를 tmux에서 실행한 상태로 유지할 수 있다.
전용 dashboard log file이 없으면 dashboard는 `bootstrap_server`, `lwm2mserver`가
포함된 pane을 자동으로 찾아 최근 output을 표시한다.

이후 tmux output의 전용 append-only copy를 유지하려면
`tmux list-panes -a -F '#S:#I.#P  #{pane_current_command}'`로 pane name을 찾은 뒤,
dashboard에 log copy를 연결한다.

```bash
./dashboard/tmux-logs.sh attach wakaama:0.0 wakaama:0.1
```

첫 번째 pane은 Bootstrap Server, 두 번째 pane은 LwM2M Server여야 한다. 이 명령은
dashboard의 기존 log를 비우고 새 terminal output을 `dashboard/logs/bootstrap.log`,
`dashboard/logs/server.log`에 append한다. tmux pane은 interactive 상태로 유지되며 변경되지 않는다.

LwM2M Server panel은 Server output의 최신 `list` 형식 client block을 추출해 읽기 전용
registered-client table로 표시한다. 즉시 client state를 refresh하려면 Server tmux pane에서
`list`를 실행한다.

## Local Write Queue

LwM2M Server panel은 현재 등록된 endpoint, LwM2M URI, single-line value를 받는다.
dashboard는 endpoint를 현재 client ID로 resolve한 뒤 기존 local `WRITE` control datagram을
`/tmp/lwm2mserver-control.sock`으로 전송한다. queued confirmation은 datagram이 local socket에
전달됐다는 뜻이며 실제 CoAP result는 Server log에 asynchronous하게 표시된다. 다른 local
socket path를 사용하려면 dashboard 시작 전 `WAKAAMA_CONTROL_SOCKET`을 설정한다.

## DFOTA Queue

dashboard에서 `.bin`, `.img`, `.hex` firmware file을 upload하면 `run/server/dfota_fw/`에 atomic 저장되고
DFOTA 목록에 바로 추가된다. file name은 영문/숫자/`.`/`_`/`-`만 허용하며 최대 크기는 64 MiB다.
동일한 file name은 기존 artifact 보호를 위해 upload할 수 없다. dashboard는 기본적으로 이 directory 바로
아래의 regular file을 목록으로 표시하고 선택한 artifact의 SHA-256을 보여준다. 현재 등록된 endpoint에 대해 목록에 있는 file만 local
`DFOTA` control request로 queue한다. 새 control request를 사용하려면 workspace rebuild 뒤
`lwm2mserver`를 restart해야 한다. device download/result sequence는 Server log에
asynchronous하게 표시된다.

log copy가 더 이상 필요 없으면 연결을 해제한다.

```bash
./dashboard/tmux-logs.sh detach wakaama:0.0 wakaama:0.1
```

## 다른 Log 위치

file을 복사하지 않고 다른 launcher가 생성한 log를 dashboard에서 읽으려면 아래처럼 지정한다.

```bash
WAKAAMA_BOOTSTRAP_LOG=/path/to/bootstrap.log \
WAKAAMA_SERVER_LOG=/path/to/server.log \
./dashboard/start.sh
```