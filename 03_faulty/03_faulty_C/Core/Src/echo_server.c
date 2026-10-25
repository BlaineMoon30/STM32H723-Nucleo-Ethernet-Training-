/**
  * echo_server.c — Step 1: netconn 에코 서버 (포트 7) — 핸즈온 2의 콜백 코드와 비교
  *   블로킹 API: 위에서 아래로 읽히는 순차 코드. lwIP 코어는 tcpip_thread가 만지고,
  *   이 태스크는 mbox로 요청만 보낸다.
  */
#include "main.h"
#include "app_rtos.h"
#include "lwip/api.h"

void server_task(void *arg)
{
  (void)arg;
  struct netconn *ln = netconn_new(NETCONN_TCP);
  netconn_bind(ln, IP_ADDR_ANY, ECHO_PORT);
  netconn_listen(ln);
  for (;;)
  {
    struct netconn *c;
    if (netconn_accept(ln, &c) != ERR_OK) { continue; }       /* 블로킹 */
    struct netbuf *nb;
    while (netconn_recv(c, &nb) == ERR_OK)                     /* 블로킹 */
    {
      void *data; u16_t len;
      do
      {
        netbuf_data(nb, &data, &len);
        netconn_write(c, data, len, NETCONN_COPY);             /* COPY → lwIP 힙(D2, non-cacheable) */
      } while (netbuf_next(nb) >= 0);
      /* netbuf_delete(nb);   ... 정리 코드 리팩터링 중 빠짐 (결함 C) */
    }
    netconn_close(c);
    netconn_delete(c);
  }
}
