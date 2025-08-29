#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/tcp.h"
#include "lwip/inet.h"

#include "dhcpserver/dhcpserver.h"
#include "dnsserver/dnsserver.h"

#include "src/stats.h"

// ===== Config do AP =====
#ifndef CYW43_AUTH_WPA2_AES_PSK
#define CYW43_AUTH_WPA2_AES_PSK 4
#endif

#define AP_SSID  "TheraLink"
#define AP_PASS  "theralink123"
#define HTTP_PORT 80

// ===== Servidores auxiliares =====
static dhcp_server_t s_dhcp;
static dns_server_t  s_dns;

// ===== HTTP (TCP RAW) =====
static err_t http_sent_cb(void *arg, struct tcp_pcb *tpcb, u16_t len) {
    (void)arg; (void)len;
    tcp_arg(tpcb, NULL);
    tcp_recv(tpcb, NULL);
    tcp_sent(tpcb, NULL);
    err_t e = tcp_close(tpcb);
    if (e != ERR_OK) tcp_abort(tpcb);
    return ERR_OK;
}

static void html_escape(char *dst, size_t dstsz, const char *src) {
    
    strncpy(dst, src, dstsz-1);
    dst[dstsz-1] = 0;
}

static void make_html(char *out, size_t outsz) {
    stats_snapshot_t s; stats_get_snapshot(&s);

    char bpm_str[32] = "--";
    if (!isnan(s.bpm_mean_trimmed)) {
        snprintf(bpm_str, sizeof(bpm_str), "%.1f", s.bpm_mean_trimmed);
    }
    char ans_str[32] = "--";
    if (!isnan(s.ans_mean)) {
        snprintf(ans_str, sizeof(ans_str), "%.2f", s.ans_mean);
    }

    char body[1536];
    snprintf(body, sizeof(body),
        "<!DOCTYPE html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>TheraLink</title>"
        "<style>body{font-family:sans-serif;margin:18px} .card{border:1px solid #ccc;border-radius:10px;padding:14px;margin:10px 0}"
        "h1{font-size:20px;margin:4px 0 12px} .mono{font-family:monospace}</style>"
        "</head><body>"
        "<h1>TheraLink — Dados do Grupo</h1>"
        "<div class='card'><b>BPM m&eacute;dio (robusto):</b> <span class='mono'>%s</span> <small>(n=%lu)</small></div>"
        "<div class='card'><b>Cores:</b><br>"
        "Verde: <b>%lu</b><br>"
        "Amarelo: <b>%lu</b><br>"
        "Vermelho: <b>%lu</b></div>"
        "<div class='card'><b>Ansiedade m&eacute;dia (1..4):</b> <span class='mono'>%s</span> <small>(n=%lu)</small></div>"
        "<div><button onclick='location.reload()'>Atualizar</button></div>"
        "</body></html>",
        bpm_str, (unsigned long)s.bpm_count,
        (unsigned long)s.cor_verde, (unsigned long)s.cor_amarelo, (unsigned long)s.cor_vermelho,
        ans_str, (unsigned long)s.ans_count
    );

    snprintf(out, outsz,
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html; charset=UTF-8\r\n"
        "Cache-Control: no-store\r\n"
        "Connection: close\r\n\r\n%s", body);
}

static void make_json(char *out, size_t outsz) {
    stats_snapshot_t s; stats_get_snapshot(&s);
    float bpm  = isnan(s.bpm_mean_trimmed) ? 0.f : s.bpm_mean_trimmed;
    float anm  = isnan(s.ans_mean) ? 0.f : s.ans_mean;

    snprintf(out, outsz,
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json; charset=UTF-8\r\n"
        "Cache-Control: no-store\r\n"
        "Connection: close\r\n\r\n"
        "{"
        "\"bpm_mean\":%.3f,\"bpm_n\":%lu,"
        "\"cores\":{\"verde\":%lu,\"amarelo\":%lu,\"vermelho\":%lu},"
        "\"ans_mean\":%.3f,\"ans_n\":%lu"
        "}",
        bpm, (unsigned long)s.bpm_count,
        (unsigned long)s.cor_verde, (unsigned long)s.cor_amarelo, (unsigned long)s.cor_vermelho,
        anm, (unsigned long)s.ans_count
    );
}

static err_t http_recv_cb(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    (void)arg; (void)err;
    if (!p) { tcp_close(tpcb); return ERR_OK; }

    char req[512]={0};
    size_t n = p->tot_len < sizeof(req)-1 ? p->tot_len : sizeof(req)-1;
    pbuf_copy_partial(p, req, n, 0);
    tcp_recved(tpcb, p->tot_len);
    pbuf_free(p);

    // rota simples
    bool json = (strstr(req, "GET /stats.json") == req) || strstr(req, "GET /stats.json?");
    char resp[2048];
    if (json) make_json(resp, sizeof(resp));
    else      make_html(resp, sizeof(resp));

    if (tcp_write(tpcb, resp, strlen(resp), TCP_WRITE_FLAG_COPY) == ERR_OK) {
        tcp_sent(tpcb, http_sent_cb);
        tcp_output(tpcb);
    } else {
        tcp_close(tpcb);
    }
    return ERR_OK;
}

static err_t http_accept_cb(void *arg, struct tcp_pcb *newpcb, err_t err) {
    (void)arg; (void)err;
    tcp_recv(newpcb, http_recv_cb);
    return ERR_OK;
}

static void http_start(void) {
    struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
    if (!pcb) return;
    if (tcp_bind(pcb, IP_ANY_TYPE, HTTP_PORT) != ERR_OK) return;
    pcb = tcp_listen(pcb);
    tcp_accept(pcb, http_accept_cb);
    printf("HTTP em %d\n", HTTP_PORT);
}

void web_ap_start(void) {
    // Inicializa stats
    stats_init();

    if (cyw43_arch_init()) {
        printf("WiFi init falhou\n");
        return;
    }
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);

    // AP
    cyw43_arch_enable_ap_mode(AP_SSID, AP_PASS, CYW43_AUTH_WPA2_AES_PSK);
    printf("AP SSID=%s PASS=%s\n", AP_SSID, AP_PASS);

    // IP do AP + servidores DHCP/DNS
    ip4_addr_t gw, mask;
    IP4_ADDR(ip_2_ip4(&gw),   192,168,4,1);
    IP4_ADDR(ip_2_ip4(&mask), 255,255,255,0);
    dhcp_server_init(&s_dhcp, &gw, &mask);
    dns_server_init(&s_dns, &gw);

    // HTTP
    http_start();

    // (usando pico_cyw43_arch_lwip_threadsafe_background)
    printf("AP/DHCP/DNS/HTTP prontos em 192.168.4.1\n");
}
