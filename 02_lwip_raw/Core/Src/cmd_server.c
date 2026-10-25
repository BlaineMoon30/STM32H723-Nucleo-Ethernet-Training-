/**
  * cmd_server.c — Step 4: TCP 명령 서버 (RAW API, 포트 23)
  *   telnet / PuTTY(Raw)로 접속 → "READY" → 한 줄 명령:
  *     LED ON | LED OFF | STATUS | TEMP | HELP
  *   telnet은 글자 하나씩 보내기도 하므로 연결마다 줄 버퍼를 두고 CR/LF에서 실행한다.
  */
#include "main.h"
#include "app_net.h"
#include "lwip/tcp.h"
#include "lwip/netif.h"
#include "lwip/mem.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#define LINE_MAX_LEN  40U

typedef struct
{
  char    line[LINE_MAX_LEN + 1U];
  uint8_t len;
  uint8_t iac_skip;            /* telnet 옵션 협상(IAC xx yy) 바이트 건너뛰기 */
} cmd_conn_t;

static err_t accept_cb(void *arg, struct tcp_pcb *np, err_t err);
static err_t recv_cb(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err);
static void  err_cb(void *arg, err_t err);

void cmd_server_init(void)
{
  struct tcp_pcb *pcb = tcp_new();
  tcp_bind(pcb, IP_ADDR_ANY, CMD_PORT);
  pcb = tcp_listen(pcb);                     /* LISTEN 전용 pcb로 교체 (원래 pcb는 해제됨) */
  tcp_accept(pcb, accept_cb);
}

/* 명령 한 줄 실행 → reply에 응답 문자열, 길이 반환 (완성본 제공 부분) */
static int exec_cmd(const char *cmd, char *reply, int size)
{
  char up[LINE_MAX_LEN + 1U];
  size_t i;
  for (i = 0; (cmd[i] != '\0') && (i < LINE_MAX_LEN); i++) { up[i] = (char)toupper((unsigned char)cmd[i]); }
  up[i] = '\0';

  if (strcmp(up, "LED ON") == 0)       { app_led(1); return snprintf(reply, size, "OK\r\n"); }
  if (strcmp(up, "LED OFF") == 0)      { app_led(0); return snprintf(reply, size, "OK\r\n"); }
  if (strcmp(up, "TEMP") == 0)         { return snprintf(reply, size, "TEMP=%ld mC\r\n", (long)read_temp_mC()); }
  if (strcmp(up, "STATUS") == 0)
  {
    char link[8];
    app_link_str(link, sizeof link);
    return snprintf(reply, size, "LINK=%s IP=%s UP=%lus\r\n", link,
                    ip4addr_ntoa(netif_ip4_addr(netif_default)), (unsigned long)(HAL_GetTick() / 1000U));
  }
  if ((strcmp(up, "HELP") == 0) || (up[0] == '?')) { return snprintf(reply, size, "LED ON|LED OFF|STATUS|TEMP\r\n"); }
  return snprintf(reply, size, "ERR unknown '%s'\r\n", cmd);
}

static err_t accept_cb(void *arg, struct tcp_pcb *np, err_t err)
{
  (void)arg;
  if ((err != ERR_OK) || (np == NULL)) { return ERR_VAL; }
  cmd_conn_t *cs = (cmd_conn_t *)mem_malloc(sizeof(cmd_conn_t));
  if (cs == NULL) { tcp_abort(np); return ERR_ABRT; }
  memset(cs, 0, sizeof(*cs));
  tcp_arg(np, cs);
  tcp_recv(np, recv_cb);
  tcp_err(np, err_cb);
  tcp_write(np, "READY\r\n", 7, TCP_WRITE_FLAG_COPY);
  tcp_output(np);
  return ERR_OK;
}

static err_t recv_cb(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
  cmd_conn_t *cs = (cmd_conn_t *)arg;
  (void)err;

  if (p == NULL)                                        /* 상대가 FIN → 우리도 닫기 */
  {
    tcp_arg(pcb, NULL);
    mem_free(cs);
    tcp_close(pcb);
    return ERR_OK;
  }

  /* pbuf는 체인일 수 있다 → q->next 따라가며 한 바이트씩 줄 버퍼로 */
  for (struct pbuf *q = p; q != NULL; q = q->next)
  {
    const uint8_t *b = (const uint8_t *)q->payload;
    for (u16_t i = 0; i < q->len; i++)
    {
      uint8_t c = b[i];
      if (cs->iac_skip) { cs->iac_skip--; continue; }
      if (c == 0xFFU)   { cs->iac_skip = 2U; continue; }       /* IAC WILL/DO/.. opt */
      if ((c == '\r') || (c == '\n'))
      {
        if (cs->len > 0U)
        {
          char reply[64];
          cs->line[cs->len] = '\0';
          int n = exec_cmd(cs->line, reply, sizeof reply);
          cs->len = 0U;
          if (n > 0) { tcp_write(pcb, reply, (u16_t)n, TCP_WRITE_FLAG_COPY); }  /* '버퍼에 넣음' */
        }
      }
      else if ((c >= 0x20U) && (c < 0x7FU) && (cs->len < LINE_MAX_LEN))
      {
        cs->line[cs->len++] = (char)c;
      }
      else if ((c == 0x08U) && (cs->len > 0U))
      {
        cs->len--;                                             /* Backspace */
      }
    }
  }

  tcp_recved(pcb, p->tot_len);     /* ★ 수신 윈도 반환 — 잊으면 몇 KB 뒤 얼어붙음 */
  pbuf_free(p);
  tcp_output(pcb);                 /* 쌓인 응답을 지금 보냄 */
  return ERR_OK;
}

static void err_cb(void *arg, err_t err)
{
  (void)err;                       /* pcb는 이미 lwIP가 해제함 — 내 상태만 정리 */
  if (arg != NULL) { mem_free(arg); }
}
