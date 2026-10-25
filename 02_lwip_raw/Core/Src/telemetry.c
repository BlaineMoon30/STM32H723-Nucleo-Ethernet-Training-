/**
  * telemetry.c — Step 3: UDP 텔레메트리 송신기 (RAW API)
  *   12 B 페이로드 = { seq, temp_mC, uptime_ms } 리틀엔디언 → PC recv_telemetry.py (struct '<IiI')
  */
#include "main.h"
#include "app_net.h"
#include "lwip/udp.h"
#include "lwip/netif.h"
#include <string.h>

typedef struct { uint32_t seq; int32_t temp_mC; uint32_t uptime_ms; } telemetry_t;

static struct udp_pcb *tx_pcb;
static ip_addr_t dest;
static uint32_t seq;

void telemetry_init(void)
{
  tx_pcb = udp_new();
  IP_ADDR4(&dest, PC_IP0, PC_IP1, PC_IP2, PC_IP3);      /* 수신 PC */
  udp_bind(tx_pcb, IP_ADDR_ANY, TELEMETRY_PORT);        /* 출발 포트 고정 */
}

void telemetry_send(void)                               /* 200 ms마다 main 루프가 호출 */
{
  if ((tx_pcb == NULL) || !netif_is_link_up(netif_default)) { return; }

  telemetry_t t = { seq++, read_temp_mC(), HAL_GetTick() };
  /* PBUF_RAM = lwIP 힙(LWIP_RAM_HEAP_POINTER 0x3000 5400, D2 non-cacheable) → DMA 전 Clean 불필요 */
  struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, sizeof t, PBUF_RAM);
  if (p == NULL) { return; }                            /* 힙 고갈 → 이번 샘플 포기 */
  memcpy(p->payload, &t, sizeof t);
  udp_sendto(tx_pcb, p, &dest, TELEMETRY_PORT);         /* UDP+IP+ETH 헤더는 lwIP가 (첫 전송 전 ARP) */
  pbuf_free(p);                                         /* 송신 후 참조 해제 (호출자 책임) */
}
