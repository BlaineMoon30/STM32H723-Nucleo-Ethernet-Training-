/**
  * app_rtos.h — 핸즈온 3 응용 (echo_server.c / web_app.c / mqtt_task.c / main.c 공용)
  */
#ifndef APP_RTOS_H
#define APP_RTOS_H

#include <stdint.h>
#include <stddef.h>

#define APP_USE_MQTT      0         /* Step 3(선택): 1로 바꾸고 PC에서 mosquitto 실행 */

#define PC_IP0            192       /* PC 192.168.77.10 : MQTT 브로커 */
#define PC_IP1            168
#define PC_IP2            77
#define PC_IP3            10
#define ECHO_PORT         7U
#define BROKER_PORT         1883U
#define MQTT_CLIENT_ID    "h723-01"

void    server_task(void *arg);     /* echo_server.c  — netconn 에코 (포트 7)   */
void    web_init(void);             /* web_app.c      — httpd + SSI + CGI       */
void    mqtt_task(void *arg);       /* mqtt_task.c    — 1초마다 온도 발행 (선택) */

/* main.c */
int32_t read_temp_mC(void);
void    app_led(uint8_t on);
int     app_link_str(char *out, size_t len);

#endif /* APP_RTOS_H */
