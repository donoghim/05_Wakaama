# LwM2M 서버-클라이언트 데이터 전송 확인 노트

이 문서는 현재 워크스페이스의 Wakaama 예제 서버와 클라이언트, 그리고 실제 모뎀의 기본 동작을 확인하는 절차를 정리한다.

## 1. 구성과 용어

- 서버: `build/server/lwm2mserver` (실행 스크립트: `run/server/00_server.sh`)
- 예제 클라이언트: `build/client/lwm2mclient` (실행 스크립트: `run/client/00_client.sh`)
- 서버 UDP 포트: `22102` (`run/server/00_server.sh` 기준)
- Endpoint Name (`ep`): LwM2M 장비의 논리 식별자. 장비별로 고유해야 한다.
- Client ID: 서버가 실행 중에만 사용하는 번호. 서버 재시작 뒤에는 바뀔 수 있다.

서버는 등록 요청의 `ep` 값을 기준으로 장비를 구분한다. UDP 소스 IP와 포트는 셀룰러 NAT 또는 재접속으로 달라질 수 있으므로 장비 고유 식별자로 사용하지 않는다.

## 2. 서버 실행

터미널 1에서 서버를 실행한다.

```bash
cd /home/quectel/Wakaama/05_Wakaama/run/server
./00_server.sh
```

스크립트 대신 직접 실행할 수도 있다.

```bash
cd /home/quectel/Wakaama/05_Wakaama
./build/server/lwm2mserver -4 -l 22102
```

서버 콘솔에서 사용할 수 있는 기본 명령은 다음과 같다.

```text
help
list
read CLIENT_ID URI
write CLIENT_ID URI DATA
observe CLIENT_ID URI
cancel CLIENT_ID URI
exec CLIENT_ID URI
q
```

## 3. 예제 클라이언트 연결

터미널 2에서 예제 클라이언트를 실행한다.

```bash
cd /home/quectel/Wakaama/05_Wakaama
./build/client/lwm2mclient -4 -h 127.0.0.1 -p 22102 -n client01
```

또는 `run/client/00_client.sh` (환경변수 `HOST`, `PORT`, `EP`로 조정 가능)를 사용해도 된다.

서버에 다음과 같이 표시되면 등록 성공이다.

```text
New client #0 registered.
name: "client01"
```

서버에서 등록 상태를 확인한다.

```text
list
```

클라이언트 터미널에서는 다음 명령으로 로컬 오브젝트를 확인한다. 이 명령들은 서버 콘솔에서는 사용할 수 없다.

```text
ls
disp
dump /3/0
```

## 4. 서버에서 데이터 조회와 쓰기

`CLIENT_ID`는 서버의 `list` 결과에 표시된 번호로 바꾼다. 아래 예시는 `0`번 클라이언트다.

```text
read 0 /3/0
```

`/3/0`은 표준 Device Object이며, 제조사, 모델, 시리얼 번호 등의 값을 반환한다.

예제 클라이언트의 Test Object는 Object ID `31024`이며, 인스턴스는 `10`, `11`, `12`다. 따라서 `/31024/0/1`은 존재하지 않아 `4.04 Not Found`가 반환된다.

```text
read 0 /31024/10/1
write 0 /31024/10/1 42
read 0 /31024/10/1
```

기대값은 최초 `20`, Write 후 `42`다.

| URI | 의미 | 동작 |
| --- | --- | --- |
| `/31024/10/1` | 정수 리소스 | Read/Write |
| `/31024/10/2` | 실행 리소스 | Execute |
| `/31024/11/3` | 실수 리소스 | Read/Write |
| `/31024/12/5` | 문자열 리소스 | Read/Write |

추가 예시:

```text
write 0 /31024/11/3 -12.5
write 0 /31024/12/5 hello
exec 0 /31024/10/2
```

`31024`는 Wakaama 예제의 테스트용 사설 Object ID이며, 표준 OMA LwM2M Object가 아니다.

## 5. 모뎀의 데이터 전송 Observe 확인

모뎀이 제공하는 데이터 Object/Resource URI를 확인한 뒤 서버에서 Observe를 요청한다. 현재 환경에서 사용한 예시는 `/10250/0/0`이다.

```text
observe 0 /10250/0/0
```

모뎀이 값을 전송하면 서버에 다음과 비슷하게 표시된다.

```text
Notify from client #0 /10250/0/0 number 1
    non block transfer
    3 bytes received of type application/octet-stream:
    31 32 33  123
```

알림 CoAP 패킷의 주요 필드는 다음과 같다.

| 필드 | 의미 |
| --- | --- |
| CoAP `2.05 Content` | Observe 알림의 정상 응답 코드 |
| Token | 서버가 보낸 Observe 요청과 알림을 연결하는 식별자 |
| Observe 옵션 | 알림 시퀀스 번호 |
| Content-Format `42` | `application/octet-stream` |
| Payload | 실제 모뎀 데이터. `31 32 33`은 ASCII `123` |

Confirmable(CON) 알림이면 서버는 같은 Message ID로 Empty ACK (`0.00`)를 돌려준다. ACK는 패킷 수신 확인일 뿐, 등록 상태 확인과는 별개다.

Observe를 중단하려면 다음을 사용한다.

```text
cancel 0 /10250/0/0
```

### 5.1 서버에서 모뎀으로 데이터 쓰기

현재 모뎀에서 `/10250/0/1`은 서버 Write를 허용하는 리소스로 확인됐다. 서버의 `write` 명령은 일반 입력값을 `text/plain`으로 전송한다.

```text
write 1 /10250/0/1 234
```

위 명령은 Client ID `1`의 `/10250/0/1`에 ASCII 문자열 `234`를 전송한다. 성공하면 모뎀이 다음 응답을 보낸다.

```text
Client #1 /10250/0/1 : 2.04 (COAP_204_CHANGED)
```

실제 반영 값을 확인할 수 있는 모뎀이라면 다음 Read를 수행한다.

```text
read 1 /10250/0/1
```

`/10250/0/0`은 모뎀에서 서버로 알림을 보내는 Observe 대상이며, `/10250/0/1`은 서버에서 모뎀으로 값을 전달하는 Write 대상이다. Client ID는 서버 재시작 또는 재등록 뒤 바뀔 수 있으므로 항상 `list` 결과를 기준으로 사용한다.

## 6. 등록 갱신과 서버 재시작

클라이언트는 lifetime 동안 주기적으로 Registration Update를 보낸다.

```text
POST /rd/0
Client #0 updated.
```

`/rd/0`은 최초 등록 시 서버가 부여한 Registration Location이다. 정상 동작이다.

서버를 종료하거나 재시작하면 예제 서버의 등록 목록과 Observe 상태는 메모리에서 사라진다. 이때 모뎀이 이전 Observe 관계의 알림을 보내더라도 서버는 CoAP ACK만 보낼 수 있으며 `list`에는 장비가 나타나지 않는다.

서버 재시작 후 복구 절차:

1. 모뎀에서 재등록을 강제한다. 모뎀의 LwM2M 세션/프로세스를 재시작하거나, 사용하는 AT 명령의 재접속 또는 재등록 기능을 사용한다.
2. 서버에서 `list`를 실행해 Endpoint Name과 Client ID를 확인한다.
3. 새 Client ID로 필요한 Observe를 다시 설정한다.

```text
list
observe NEW_CLIENT_ID /10250/0/0
```

## 7. 클라이언트 정상 종료

예제 클라이언트는 클라이언트 콘솔에서 `quit`를 입력해 정상 종료한다.

```text
quit
```

정상 종료하면 Deregister 요청이 전송되고 서버에는 `Client #N unregistered.`가 표시된다. `Ctrl+C`로 종료하면 Deregister 없이 끊길 수 있으며, 서버는 lifetime이 만료될 때까지 등록 정보를 유지할 수 있다.

## 8. 빠른 점검 순서

```text
1. 서버 실행: ./lwm2mserver -4 -l 22102
2. 클라이언트/모뎀 연결 또는 재등록
3. 서버에서 list
4. read CLIENT_ID /3/0
5. observe CLIENT_ID /10250/0/0
6. 모뎀 데이터 전송 후 Notify 로그 확인
7. 서버를 재시작했다면 재등록과 Observe를 다시 수행
```