/**
  * app_net.h — 핸즈온 2 응용 (telemetry.c / cmd_server.c / main.c 공용)
  */
#ifndef APP_NET_H
#define APP_NET_H

#include <stdint.h>
#include <stddef.h>

/* 수신 PC (교육 환경: PC 192.168.77.10 ↔ 보드 192.168.77.20 직결) */
#define PC_IP0            192
#define PC_IP1            168
#define PC_IP2            77
#define PC_IP3            10
#define TELEMETRY_PORT    5005U     /* UDP: 보드 5005 → PC 5005 */
#define TELEMETRY_MS      200U      /* 5 Hz */
#define CMD_PORT          23U       /* TCP 명령 서버 */

void    telemetry_init(void);
void    telemetry_send(void);
void    cmd_server_init(void);

/* main.c 에 구현 */
int32_t read_temp_mC(void);                 /* DTS(온칩 디지털 온도 센서), 1 °C 분해능 */
void    app_led(uint8_t on);                /* LD1 수동 제어 (점멸 중지) */
int     app_link_str(char *out, size_t len);/* "100F" / "10H" / "DOWN" */

#endif /* APP_NET_H */
