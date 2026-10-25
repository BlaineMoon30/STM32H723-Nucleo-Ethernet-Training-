# STM32 Ethernet Training — 핸즈온 소스 (NUCLEO-H723ZG)

STM32 Ethernet Training 교재의 핸즈온에서 쓰는 STM32CubeMX 프로젝트와 PC 도구입니다.
모든 프로젝트는 **.ioc → STM32CubeMX Generate → USER CODE 블록에만 코드 추가**로 만들었습니다.
CubeMX에서 `.ioc`를 열어 설정을 바꾸고 다시 Generate해도 실습 코드와 링커 추가분은 유지됩니다.

| 폴더 | 슬라이드 | 내용 |
|---|---|---|
| `01_raw_eth/` | 핸즈온 1 | HAL만: MDIO로 PHY 스캔 → 링크 → Raw 프레임(EtherType 0x88B5) 송신, 수신 hexdump |
| `02_lwip_raw/` | 핸즈온 2 | lwIP NO_SYS(RAW API): ping, UDP 텔레메트리(5005), TCP 명령 서버(23) |
| `03_rtos_http/` | 핸즈온 3 | FreeRTOS + lwIP: netconn 에코(7), httpd SSI/CGI(80), (선택) MQTT |
| `03_faulty/03_faulty_A·B·C/` | 핸즈온 3 Step 4 | 03_rtos_http에 결함을 하나씩 넣은 고장 진단 실습 |
| `bin/` | 핸즈온 0~3 | 빌드 결과 `.elf` / `.bin`. `00_ping_responder.bin` = 02_lwip_raw 완성본 (ST-LINK 드라이브에 드래그 앤 드롭) |
| `pc/` | 핸즈온 2·3 | `recv_telemetry.py` (UDP 5005 수신), `listen.conf` (mosquitto 1883) |
| `tools/makefsdata.py` | 핸즈온 3 | `fs/` 웹 페이지 → `fsdata_custom.c` 변환 |

## 핸즈온별 예제 위치

| 핸즈온 | 예제 |
|---|---|
| 핸즈온 0 — 환경 셋업 + PC에서 패킷 관찰 | [`bin/00_ping_responder.bin`](bin/) (보드에 쓰는 완성본) · [`pc/`](pc/) |
| 핸즈온 1 — CubeMX 설정 → lwIP 없이 Raw 프레임 송수신 | [`01_raw_eth/`](01_raw_eth/) |
| 핸즈온 2 — ping → UDP 텔레메트리 → TCP 명령 서버 | [`02_lwip_raw/`](02_lwip_raw/) · [`pc/recv_telemetry.py`](pc/recv_telemetry.py) |
| 핸즈온 3 — FreeRTOS + 소켓 웹서버, 고장 진단 | [`03_rtos_http/`](03_rtos_http/) · [`03_faulty/`](03_faulty/) (A·B·C) |

미리 빌드한 `.bin` / `.elf`는 [`bin/`](bin/)과 [Releases](../../releases)에 있습니다.

## 확인된 환경

- STM32CubeMX 6.18.1, STM32Cube FW_H7 V1.13.0 (LwIP 2.2.1, FreeRTOS 10.x CMSIS-RTOS v2)
- STM32CubeIDE 2.1.1 (GNU Tools for STM32), 모든 프로젝트 **0 errors, 0 warnings**
- 보드: NUCLEO-H723ZG (LAN8742A, RMII), 550 MHz. 로그: ST-LINK VCP 115200 8N1 (BSP COM1 = USART3)
- 네트워크: PC 192.168.77.10 ↔ 보드 192.168.77.20 / 255.255.255.0 직결, 보드 MAC 02:00:00:11:22:33

> **실보드 검증 완료 (2026-09-27)** — NUCLEO-H723ZG + PC USB LAN 어댑터(192.168.77.10) 직결.
> 01: PHY 스캔·링크 100M Full·송신 1초 주기·수신 덤프 / 02: ping 10/10, 텔레메트리 5 Hz 무손실, TCP 23 명령 전부, LED GPIO 확인 /
> 03: ping, 에코, 웹(SSI·404·CGI) 100/100 무누수 / 결함 A·B·C 모두 재현.

## 여는 방법

1. **CubeMX로 설정 보기**: `01_raw_eth/01_raw_eth.ioc` 더블클릭 → Pinout/Middleware 확인 → *Generate Code*
2. **CubeIDE로 빌드**: File → Import → *Existing Projects into Workspace* → 프로젝트 폴더 선택 → Build
3. 플래시: CubeIDE Run, 또는 `bin/*.bin`을 NOD_H723ZG 드라이브에 드래그 앤 드롭

## 메모리 배치 (모든 프로젝트 공통)

```
D2 SRAM 0x3000 0000 (32 KB)   MPU Region 1 = 32 KB Normal non-cacheable (TEX1 C0 B0)
                              MPU Region 2 = 앞 1 KB Device (TEX0 C0 B1 S1)  — 번호가 높아 우선
  0x3000 0000  Rx 디스크립터 ×4 (24 B씩)   .RxDescripSection   ← CubeMX ETH 설정
  0x3000 0200  Tx 디스크립터 ×4            .TxDescripSection
  0x3000 0400  RX 버퍼                     .RxArraySection (핸즈온 1 RxPool 8×1536)
                                           .Rx_PoolSection (lwIP RX_POOL 12×~1568, ~0x3000 4D83)
  0x3000 5400  lwIP 힙 MEM_SIZE 10240      LWIP_RAM_HEAP_POINTER (~0x3000 7BFF)
AXI SRAM 0x2400 0000 (캐시 가능)  일반 변수, FreeRTOS 힙, lwIP MEMP 풀, 핸즈온 1 frame[] (송신 전 Clean)
```

- 링커 스크립트(`STM32H723ZGTX_FLASH.ld`, `_RAM.ld`)에 `.eth_d2 (NOLOAD)` 블록을 넣었습니다.
  02·03은 `ASSERT(__eth_d2_end <= 0x30005400)`로 RX 버퍼가 lwIP 힙과 겹치면 링크 에러가 납니다.
- CubeMX 생성 `ethernetif.c`는 송신 전에 캐시 Clean을 하지 않습니다. 그래서 lwIP 힙을 D2(NC)에 둡니다.

## 프로젝트별 요약

### 01_raw_eth (핸즈온 1)
- CubeMX: ETH RMII, MAC 02:00:00:11:22:33, Rx/Tx Desc 4, RxBuffLen 1536, ETH IRQ 우선순위 5, ICache/DCache + MPU.
- `main.c` USER CODE: `phy_scan()`(MDIO 0~31), `link_update()`(BSR 두 번 읽기 → PHYSCSR → `HAL_ETH_SetMACConfig` → `HAL_ETH_Start_IT`),
  `send_hello()`(64 B 브로드캐스트, 0x88B5, `SCB_CleanDCache_by_Addr` 후 `HAL_ETH_Transmit_IT`), RX 콜백 + hexdump.
- 기대 로그: `PHY @0 ID1=0x0007 ID2=0xC13x` → `Link UP 100M Full-duplex` → PC ARP 등 수신 프레임 덤프.
- Wireshark: `eth.type == 0x88b5`

### 02_lwip_raw (핸즈온 2)
- CubeMX: LWIP (DHCP 끔, 192.168.77.20), MEM_SIZE 10240 @0x30005400, CHECKSUM_BY_HARDWARE, DTS(온칩 온도 센서).
- `Core/Src/telemetry.c` — 200 ms마다 `{seq, temp_mC, uptime_ms}` 12 B → 192.168.77.10:5005
- `Core/Src/cmd_server.c` — TCP 23: `READY` 후 `LED ON` / `LED OFF` / `STATUS` / `TEMP` / `HELP` (줄 단위, telnet 옵션 바이트 무시, `tcp_recved` 포함)
- LD1 점멸 = 동작 중 (LED 명령을 받으면 점멸 중지), LD2 = 링크.
- PC: `py pc\recv_telemetry.py`, `telnet 192.168.77.20 23` (또는 PuTTY Raw)
- Step 2 DHCP 시연: CubeMX에서 LWIP_DHCP Enabled → Generate. `MX_LWIP_Init`이 `dhcp_start`를 부르고 main 루프가 받은 IP를 출력합니다.

### 03_rtos_http (핸즈온 3)
- CubeMX: FreeRTOS CMSIS_V2 (heap 48 KB, 스택 오버플로 검사 2), HAL 타임베이스 TIM6, LWIP + HTTPD(SSI·CGI·동적 헤더·custom fsdata) + LWIP_STATS.
- `Core/Src/echo_server.c` — netconn 에코 서버 포트 7 (`server_task`)
- `Core/Src/web_app.c` — `httpd_init` + SSI 태그 `ip` `link` `temp` + CGI `/led.cgi?v=1|0` (코어 잠금 안에서 초기화)
- `fs/index.shtml`, `fs/404.html` → `py tools\makefsdata.py 03_rtos_http\fs 03_rtos_http\LWIP\App\fsdata_custom.c`
- `Core/Src/mqtt_task.c` — `Core/Inc/app_rtos.h`에서 `APP_USE_MQTT 1`로 켬. 브로커 192.168.77.10:1883, 토픽 `lab/h723-01/temp`, LWT `lab/h723-01/status`.
  PC: `mosquitto -v -c pc\listen.conf`, `mosquitto_sub -t "lab/#" -v`
- **USER 버튼(B1)** = 진단 출력: ETH_DMACSR/DMADSR/MACCR 비트, RX_POOL·NETBUF·lwIP 힙·FreeRTOS 힙 사용량.
