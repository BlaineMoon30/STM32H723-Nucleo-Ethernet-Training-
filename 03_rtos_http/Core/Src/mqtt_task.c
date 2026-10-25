/**
  * mqtt_task.c — Step 3 (선택): MQTT로 온도 발행 (lwIP apps/mqtt, 브로커 = PC mosquitto)
  *   app_rtos.h 에서 APP_USE_MQTT 1 로 켠다.
  *   mqtt_* 함수는 lwIP RAW API 계열 → 태스크에서 부를 때는 LOCK_TCPIP_CORE() 안에서.
  */
#include "main.h"
#include "app_rtos.h"

#if APP_USE_MQTT
#include "cmsis_os.h"
#include "lwip/apps/mqtt.h"
#include "lwip/tcpip.h"
#include <stdio.h>
#include <string.h>

static mqtt_client_t *cli;
static const struct mqtt_connect_client_info_t ci = {
  .client_id = MQTT_CLIENT_ID, .keep_alive = 60,
  .will_topic = "lab/" MQTT_CLIENT_ID "/status", .will_msg = "offline", .will_qos = 1, .will_retain = 1
};

static void conn_cb(mqtt_client_t *c, void *arg, mqtt_connection_status_t st)
{
  (void)arg;
  printf("MQTT connection status %d\r\n", (int)st);
  if (st == MQTT_CONNECT_ACCEPTED)                  /* tcpip_thread 문맥 → 잠금 불필요 */
  {
    mqtt_publish(c, "lab/" MQTT_CLIENT_ID "/status", "online", 6, 1, 1, NULL, NULL);
  }
}

void mqtt_task(void *arg)
{
  (void)arg;
  ip_addr_t broker;
  IP_ADDR4(&broker, PC_IP0, PC_IP1, PC_IP2, PC_IP3);   /* PC의 mosquitto */
  cli = mqtt_client_new();
  uint32_t retry = 0;
  for (;;)
  {
    char msg[32];
    int32_t mc = read_temp_mC();
    int n = snprintf(msg, sizeof msg, "{\"t\":%ld.%ld}", (long)(mc / 1000), (long)((mc % 1000) / 100));

    LOCK_TCPIP_CORE();
    if (mqtt_client_is_connected(cli))
    {
      mqtt_publish(cli, "lab/" MQTT_CLIENT_ID "/temp", msg, (u16_t)n, 0, 0, NULL, NULL);
    }
    else if (retry == 0U)                           /* 끊겨 있으면 5초마다 재접속 */
    {
      mqtt_client_connect(cli, &broker, BROKER_PORT, conn_cb, NULL, &ci);
    }
    UNLOCK_TCPIP_CORE();
    retry = (retry + 1U) % 5U;
    osDelay(1000);
  }
}
#endif /* APP_USE_MQTT */
