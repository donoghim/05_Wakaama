Build guide (통합 소스 기준, 260911 통합 이후)

하나의 소스 트리(wakaama/)에서 bootstrap_server, lwm2mclient, lwm2mserver,
lightclient를 개별 또는 한 번에 빌드한다. 각 타겟은 CMake가 독립적으로
컴파일하므로 서로 간섭하지 않는다.

## 루트의 make_*.sh 스크립트 사용 (권장)

quectel@quectel-OptiPlex-7090:~/Wakaama/05_Wakaama$ ./make_all.sh              # 전체 (bootstrap_server, lwm2mclient, lwm2mserver, lightclient)
quectel@quectel-OptiPlex-7090:~/Wakaama/05_Wakaama$ ./make_bootstrap_server.sh # bootstrap_server만
quectel@quectel-OptiPlex-7090:~/Wakaama/05_Wakaama$ ./make_server.sh          # lwm2mserver만
quectel@quectel-OptiPlex-7090:~/Wakaama/05_Wakaama$ ./make_client.sh          # lwm2mclient(NoSec)만
quectel@quectel-OptiPlex-7090:~/Wakaama/05_Wakaama$ ./make_lightclient.sh     # lightclient만

각 스크립트는 build/ 가 없으면 최초 1회 자동으로
  cmake -S wakaama/examples -B build
를 실행한 뒤 필요한 타겟만 빌드한다. 완전히 새로 설정하려면 -c 옵션:

quectel@quectel-OptiPlex-7090:~/Wakaama/05_Wakaama$ ./make_all.sh -c

빌드 결과물:
  build/bootstrap_server/bootstrap_server
  build/client/lwm2mclient
  build/server/lwm2mserver
  build/lightclient/lightclient

lwm2mclient_tinydtls(DTLS 클라이언트)는 make_*.sh에 포함되어 있지 않다
(autoconf/automake/libtool 필요). 필요하면 직접 빌드:
  cmake --build build --target lwm2mclient_tinydtls

## 원시 cmake 명령으로 직접 빌드/개별 타겟 지정

quectel@quectel-OptiPlex-7090:~/Wakaama/05_Wakaama$ cmake -S wakaama/examples -B build
quectel@quectel-OptiPlex-7090:~/Wakaama/05_Wakaama$ cmake --build build --target lwm2mserver -j"$(nproc)"   # 타겟 하나만
quectel@quectel-OptiPlex-7090:~/Wakaama/05_Wakaama$ cmake --build build -j"$(nproc)"                         # 전체(tinydtls 포함, 실패 가능)

다시 빌드하려면:
quectel@quectel-OptiPlex-7090:~/Wakaama/05_Wakaama$ rm -rf build
quectel@quectel-OptiPlex-7090:~/Wakaama/05_Wakaama$ ./make_all.sh

실행 스크립트/설정 파일은 run/ 아래 역할별로 있다.
  run/bootstrap_server/  (00_bs_plain.sh, 01_bs_dtls.sh, *.ini, psk.txt, key.txt)
  run/server/            (00_server.sh, 10_server_sender.sh, 11_server_sender_dfota.sh, dfota_fw/)
  run/client/            (00_client.sh)

sender는 기존과 동일하게 별도 유틸이다.
quectel@quectel-OptiPlex-7090:~/Wakaama/05_Wakaama/07_sender$ rm -rf build
quectel@quectel-OptiPlex-7090:~/Wakaama/05_Wakaama/07_sender$ cmake -S . -B build
quectel@quectel-OptiPlex-7090:~/Wakaama/05_Wakaama/07_sender$ cmake --build build
