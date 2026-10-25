/**
  * web_app.c — Step 2: HTTP 웹서버 (lwIP httpd, RAW API로 tcpip_thread 안에서 동작)
  *   페이지: fs/index.shtml → tools/makefsdata.py → LWIP/App/fsdata_custom.c
  *   SSI 태그 <!--#ip--> <!--#link--> <!--#temp-->,  CGI /led.cgi?v=1|0
  */
#include "main.h"
#include "app_rtos.h"
#include "lwip/apps/httpd.h"
#include "lwip/tcpip.h"
#include "lwip/netif.h"
#include <stdio.h>
#include <stdlib.h>

static const char *ssi_tags[] = { "ip", "link", "temp" };

/* SSI 핸들러: 태그 번호별로 삽입할 문자열을 out에 쓰고 길이를 돌려준다 (tcpip_thread 문맥) */
static u16_t ssi_cb(int idx, char *out, int len)
{
  switch (idx)
  {
    case 0: return (u16_t)snprintf(out, len, "%s", ip4addr_ntoa(netif_ip4_addr(netif_default)));
    case 1: return (u16_t)app_link_str(out, (size_t)len);
    case 2:
    {
      int32_t mc = read_temp_mC();                 /* printf %f 없이 정수로: 31.0 */
      return (u16_t)snprintf(out, len, "%ld.%ld", (long)(mc / 1000), (long)((mc % 1000) / 100));
    }
    default: return 0;
  }
}

/* CGI 핸들러: /led.cgi?v=1 → LD1 ON, 응답으로 보낼 페이지 경로를 돌려준다 */
static const char *led_cgi(int i, int n, char *k[], char *v[])
{
  (void)i;
  for (int j = 0; j < n; j++)
  {
    if ((k[j][0] == 'v') && (k[j][1] == '\0')) { app_led((uint8_t)(atoi(v[j]) != 0)); }
  }
  return "/index.shtml";
}

static const tCGI cgi_tbl[] = { { "/led.cgi", led_cgi } };

void web_init(void)
{
  LOCK_TCPIP_CORE();                               /* RAW API는 코어 잠금 안에서만 */
  httpd_init();
  http_set_ssi_handler(ssi_cb, ssi_tags, LWIP_ARRAYSIZE(ssi_tags));
  http_set_cgi_handlers(cgi_tbl, LWIP_ARRAYSIZE(cgi_tbl));
  UNLOCK_TCPIP_CORE();
}
