# Network setting
```text
    사용자 규칙 프로토콜 외부포트  내부 IP        내부 포트  
027 LwM2M-BS    UDP     22101    192.168.0.105  22101  
028 LwM2M-DM    UDP     22102    192.168.0.105  22102
```


|     | 사용자 규칙 | 프로토콜 | 외부 포트 | 내부 IP       | 내부 포트 |
| ---:| ---:       | :---    | :---      | :---          | :---     |
| 027 | LwM2M-BS   | UDP     | 22101     | 192.168.0.105 | 22101    |
| 028 | LwM2M-DM   | UDP     | 22102     | 192.168.0.105 | 22102    |

# 05_Wakaama 사용 방법

이 문서는 `05_Wakaama` 폴더에서 Bootstrap Server, LwM2M Server, Sender를 실행하고, EPNS 등록, lifetime 설정, `/10250` 데이터 write, DFOTA를 수행하는 방법을 정리한다.

## 1. 실행 순서

터미널을 3개 열고 아래 순서대로 실행한다.

### 1-1. Bootstrap Server 실행

```sh
cd ~/Wakaama/05_Wakaama/run/bootstrap_server
./00_bs_plain.sh
```

`00_bs_plain.sh`는 아래 명령을 실행한다.

```sh
./bootstrap_server -l 22101 -4 -f 01_bs_plain.ini
```

- IPv4 사용: `-4`
- Bootstrap Server UDP 포트: `22101`
- Bootstrap 설정 파일: `01_bs_plain.ini`

### 1-2. LwM2M Server 실행

```sh
cd ~/Wakaama/05_Wakaama/run/server
./11_server_sender_dfota.sh
```

`11_server_sender_dfota.sh`는 기본적으로 아래 설정으로 `lwm2mserver`를 실행한다.

```sh
./lwm2mserver -4 -l 22102 \
  -p /tmp/lwm2mserver-control.sock \
  -F dfota_fw \
  -u /dfota_fw \
  -H 115.90.109.11
```

- IPv4 사용: `-4`
- LwM2M Server UDP 포트: `22102`
- Sender 제어 소켓: `/tmp/lwm2mserver-control.sock`
- DFOTA firmware 디렉터리: `dfota_fw`
- DFOTA URI prefix: `/dfota_fw`
- DFOTA Package URI host/IP: `115.90.109.11`

필요하면 환경 변수로 값을 바꿔 실행할 수 있다.

```sh
DFOTA_HOST=서버_IP LWM2M_PORT=22102 DFOTA_FILE=A02_beta_to_A02.bin ./11_server_sender_dfota.sh
```

등록 직후 요청 방식은 실행 스크립트로 선택한다.

- `./11_server_sender_dfota.sh`: 현재 모드. `/10250/0/0`, `/26241/0/0` Observe를 바로 요청한다.
- `./12_server_commercial.sh`: 상용 모드. 서버 실행 옵션 `-C`를 추가하며 나머지 Sender/DFOTA 설정은 11번과 같다.

상용 모드는 앞 요청의 응답 또는 최종 타임아웃 후 다음 요청을 아래 순서로 전송한다.

```text
/4/0/8 Read
/4/0/0 Read
/10250/0/0 Observe
/26241/0/0 Observe
/3/0/3 Read
```

LwM2M Read와 Observe는 모두 CoAP `GET`으로 전송된다. 패킷 상세에 `Observe: 0` 옵션이 있으면 Observe 등록이고, 해당 옵션이 없으면 일반 Read이다.

### 1-3. Sender 실행

```sh
cd ~/Wakaama/05_Wakaama/07_sender
./10_sender.sh
```

`10_sender.sh`는 아래 명령을 실행한다.

```sh
./build/sender \
  -t 3600 \
  -c 0 \
  -r /10250/0/1 \
  -d now \
  -i -v
```

- `-t 3600`: 3600초마다 write
- `-c 0`: LwM2M Server에서 보이는 client ID 0번 대상
- `-r /10250/0/1`: write할 리소스 URI
- `-d now`: 현재 Unix epoch 시간을 데이터로 write
- `-i`: 실행 직후 1회 즉시 write
- `-v`: queued 로그 출력

`sender`는 LwM2M 패킷을 직접 보내지 않고, `/tmp/lwm2mserver-control.sock`으로 server에 write 요청을 전달한다. 실제 LwM2M Write 결과는 `run/server`의 `lwm2mserver` 콘솔에 비동기로 출력된다.

## 2. EPNS 등록 설정

EPNS endpoint는 Bootstrap Server 설정 파일에서 등록한다.

사용 파일:

```text
~/Wakaama/05_Wakaama/run/bootstrap_server/01_bs_plain.ini
```

현재 등록된 endpoint 예시는 아래와 같다.

```ini
[Endpoint]
Name=ASN_CSE-D-d726e8d1d6-QUEC
Server=101

[Endpoint]
Name=ASN_CSE-D-5c916f51f3-QUEC
Server=101

[Endpoint]
Name=ASN_CSE-D-548818ac83-QUEC
Server=101
```

새 EPNS를 등록하려면 `01_bs_plain.ini`에 `[Endpoint]` 블록을 추가하고 `Name`에 모듈의 endpoint name을 넣는다.

```ini
[Endpoint]
Name=ASN_CSE-D-새로운_EPNS-QUEC
Server=101
```

`Server=101`은 같은 파일의 `[Server] id=101` 블록을 의미한다. 이 서버 블록이 실제 LwM2M Server 주소와 lifetime을 클라이언트에 내려준다.

## 3. Lifetime 설정

Lifetime도 Bootstrap Server 설정 파일에서 변경한다.

사용 파일:

```text
~/Wakaama/05_Wakaama/run/bootstrap_server/01_bs_plain.ini
```

현재 NoSec 서버 설정은 아래와 같다.

```ini
[Server]
id=101
uri=coap://115.90.109.11:22102
bootstrap=no
lifetime=7200
security=NoSec
```

`lifetime=7200`은 클라이언트 registration lifetime을 7200초로 설정한다. 예를 들어 3600초로 바꾸려면 아래처럼 수정한다.

```ini
lifetime=3600
```

수정 후에는 Bootstrap Server를 재시작하고, 단말이 다시 bootstrap을 수행해야 새 설정이 반영된다.

## 4. Bootstrap Server 콘솔 명령

Bootstrap Server 실행 후 콘솔에서 사용할 수 있는 명령은 아래와 같다.

```text
boot URI [NAME]
q
```

### `boot URI [NAME]`

서버에서 클라이언트로 bootstrap을 시작한다.

```text
boot coap://CLIENT_IP:CLIENT_PORT ENDPOINT_NAME
```

예시:

```text
boot coap://192.168.0.10:56830 ASN_CSE-D-548818ac83-QUEC
```

- `URI`: bootstrap 대상 클라이언트 주소
- `NAME`: `01_bs_plain.ini`의 `[Endpoint] Name` 값
- `NAME`을 생략하면 ini 파일에서 이름이 없는 endpoint 설정을 사용한다.

### `q`

Bootstrap Server를 종료한다.

```text
q
```

## 5. LwM2M Server 콘솔 명령

`run/server`의 `lwm2mserver` 콘솔에서 `help`를 입력하면 명령 목록을 볼 수 있다. 주요 명령은 아래와 같다.

### 등록 client 확인

```text
list
```

client가 등록되면 서버 콘솔에 아래와 비슷한 로그가 출력된다.

```text
New client #0 registered.
```

여기서 `#0`이 이후 명령에 사용하는 `CLIENT#`이다.

### Read

```text
read CLIENT# URI
```

예시:

```text
read 0 /3/0/0
read 0 /10250/0/0
```

### Discover

```text
disc CLIENT# URI
```

예시:

```text
disc 0 /
disc 0 /10250
```

### Write

```text
write CLIENT# URI DATA
```

예시:

```text
write 0 /10250/0/1 1234567890
write 0 /10250/0/1 hello
```

### Partial Update

```text
update CLIENT# URI DATA
```

`DATA`는 지원되는 JSON 형식이어야 한다.

### Observe / Cancel Observe

```text
observe CLIENT# URI
cancel CLIENT# URI
```

예시:

```text
observe 0 /10250/0/0
cancel 0 /10250/0/0
```

이 서버 코드는 client가 새로 등록될 때 `/10250/0/0`과 `/26241/0/0` observe를 자동으로 걸도록 수정되어 있다.

### Execute

```text
exec CLIENT# URI
exec CLIENT# URI DATA
```

예시:

```text
exec 0 /5/0/2
```

### Attribute 설정

```text
time CLIENT# URI PMIN PMAX
attr CLIENT# URI LT GT [STEP]
clear CLIENT# URI
```

예시:

```text
time 0 /10250/0/0 10 60
attr 0 /10250/0/0 10 100 1
clear 0 /10250/0/0
```

### 종료

```text
q
```

## 6. 서버에서 `/10250`으로 데이터 write 하는 법

### 방법 1: LwM2M Server 콘솔에서 직접 write

`run/server` 터미널에서 client ID를 확인한다.

```text
list
```

client ID가 `0`이면 아래처럼 `/10250/0/1`에 데이터를 쓴다.

```text
write 0 /10250/0/1 1234567890
```

현재 시간을 Unix epoch 값으로 넣고 싶으면 shell에서 값을 만든 뒤 server 콘솔에 입력한다.

```sh
date +%s
```

출력된 값을 사용한다.

```text
write 0 /10250/0/1 1789000000
```

### 방법 2: Sender로 주기적으로 write

Sender는 server의 제어 소켓으로 write 요청을 보낸다. server는 반드시 `-p /tmp/lwm2mserver-control.sock` 옵션으로 실행되어 있어야 한다.

1회 즉시 write 후 3600초마다 현재 시간을 write:

```sh
cd ~/Wakaama/05_Wakaama/07_sender
./build/sender -t 3600 -c 0 -r /10250/0/1 -d now -i -v
```

테스트용으로 10초마다 3번만 write:

```sh
./build/sender -t 10 -c 0 -r /10250/0/1 -d now -i -v -n 3
```

고정 문자열 write:

```sh
./build/sender -t 60 -c 0 -r /10250/0/1 -d test_message -i -v -n 1
```

기본 제어 소켓이 아닌 다른 소켓을 사용할 때는 `-s`를 지정한다.

```sh
./build/sender -t 60 -c 0 -r /10250/0/1 -d now -i -v -s /tmp/lwm2mserver-control.sock
```

## 7. DFOTA 하는 법

### 7-1. Firmware 파일 준비

Firmware 파일은 server 실행 위치 기준 `dfota_fw` 디렉터리에 있어야 한다.

현재 확인된 파일:

```text
~/Wakaama/05_Wakaama/run/server/dfota_fw/A02_to_A02_beta.bin
~/Wakaama/05_Wakaama/run/server/dfota_fw/A02_beta_to_A02.bin
```

### 7-2. DFOTA 가능한 server로 실행

```sh
cd ~/Wakaama/05_Wakaama/run/server
./11_server_sender_dfota.sh
```

server는 firmware 파일을 CoAP GET으로 제공한다. 기본 Package URI 형식은 아래와 같다.

```text
coap://115.90.109.11:22102/dfota_fw/파일명
```

예시:

```text
coap://115.90.109.11:22102/dfota_fw/A02_beta_to_A02.bin
```

`115.90.109.11`이 단말에서 접근 가능한 서버 IP가 아니면 `DFOTA_HOST`를 실제 IP로 바꿔 server를 실행한다.

```sh
DFOTA_HOST=192.168.0.100 ./11_server_sender_dfota.sh
```

### 7-3. Client 등록 확인

server 콘솔에서 client ID를 확인한다.

```text
list
```

예를 들어 client ID가 `0`이면 다음 단계에서 `0`을 사용한다.

### 7-4. DFOTA 명령 실행

server 콘솔에서 아래 명령을 입력한다.

```text
dfota CLIENT# FILE
```

예시:

```text
dfota 0 A02_beta_to_A02.bin
dfota 0 A02_to_A02_beta.bin
```

서버 내부 동작은 아래 순서로 진행된다.

1. `/5/0/3` Firmware State를 observe한다.
2. `/5/0/1` Package URI에 `coap://HOST:PORT/dfota_fw/FILE`을 write한다.
3. 단말이 firmware 파일을 CoAP GET으로 다운로드한다.
4. `/5/0/3` 값이 다운로드 완료 상태가 되면 서버가 `/5/0/5` Update Result를 read한다.
5. `/5/0/5` 값이 `0`이면 서버가 `/5/0/2` Update를 execute한다.

수동으로 Package URI를 write하려면 아래처럼 입력할 수도 있다.

```text
write 0 /5/0/1 coap://115.90.109.11:22102/dfota_fw/A02_beta_to_A02.bin
```

이 경우 update 실행은 별도로 해야 한다.

```text
exec 0 /5/0/2
```

## 8. 자주 확인할 것

- Bootstrap Server 포트는 `22101`이다.
- LwM2M Server 포트는 `22102`이다.
- `01_bs_plain.ini`의 `uri=coap://115.90.109.11:22102`가 실제 server IP와 port를 가리켜야 한다.
- 새 EPNS를 추가하거나 lifetime을 바꾼 뒤에는 Bootstrap Server를 재시작해야 한다.
- Sender를 쓰려면 server가 `-p /tmp/lwm2mserver-control.sock` 옵션으로 실행되어 있어야 한다.
- DFOTA 파일명에는 `/` 또는 `..`을 넣을 수 없다. 파일은 `run/server/dfota_fw` 아래에 있어야 한다.
- server 콘솔에서 client ID는 항상 `list`로 확인한 값을 사용한다.