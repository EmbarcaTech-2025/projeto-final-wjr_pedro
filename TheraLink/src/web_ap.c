// src/web_ap.c
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

#ifndef CYW43_AUTH_WPA2_AES_PSK
#define CYW43_AUTH_WPA2_AES_PSK 4
#endif

#define AP_SSID  "TheraLink"
#define AP_PASS  "theralink123"
#define HTTP_PORT 80

static dhcp_server_t s_dhcp;
static dns_server_t  s_dns;

/* --------- TX state (1 conexão por vez é suficiente p/ nosso uso) --------- */
typedef struct {
    const char *buf;
    u16_t len;
    u16_t off;
} http_tx_t;

static http_tx_t g_tx = {0};
static char g_resp[8192];   // resposta (HTML/JSON/CSV) fica aqui até terminar o envio

/* Envia o que couber no buffer TCP; retorna true quando terminou */
static bool http_send_chunk(struct tcp_pcb *tpcb) {
    while (g_tx.off < g_tx.len) {
        u16_t wnd = tcp_sndbuf(tpcb);
        if (wnd == 0) break;
        u16_t chunk = g_tx.len - g_tx.off;
        if (chunk > 1200) chunk = 1200;      // evita pacotes enormes
        if (chunk > wnd)  chunk = wnd;
        err_t e = tcp_write(tpcb, g_tx.buf + g_tx.off, chunk, TCP_WRITE_FLAG_COPY);
        if (e == ERR_MEM) break;             // buffer cheio; espera ACK (tcp_sent)
        if (e != ERR_OK)  { tcp_abort(tpcb); return true; } // erro fatal
        g_tx.off += chunk;
    }
    tcp_output(tpcb);
    return (g_tx.off >= g_tx.len);
}

static err_t http_sent_cb(void *arg, struct tcp_pcb *tpcb, u16_t len) {
    (void)arg; (void)len;
    if (http_send_chunk(tpcb)) {
        tcp_sent(tpcb, NULL);
        tcp_recv(tpcb, NULL);
        tcp_close(tpcb);
    }
    return ERR_OK;
}

/* -------------------- PÁGINA HTML -------------------- */
static void make_html(char *out, size_t outsz) {
    const char *body =
        "<!doctype html><html><head><meta charset=utf-8>"
        "<meta name=viewport content='width=device-width,initial-scale=1'>"
        "<title>TheraLink</title>"
        "<style>"
        "body{font-family:system-ui,-apple-system,Segoe UI,Roboto,Arial,sans-serif;margin:16px}"
        ".grid{display:grid;grid-template-columns:1fr;gap:12px}@media(min-width:760px){.grid{grid-template-columns:1fr 1fr}}"
        ".card{border:1px solid #ddd;border-radius:12px;padding:14px;box-shadow:0 1px 3px rgba(0,0,0,.06)}"
        ".title{margin:0 0 8px;font-size:18px}.kpi{font-size:34px;font-weight:700;margin:6px 0}"
        ".row{display:flex;gap:8px;align-items:center;flex-wrap:wrap}.chip{font:12px monospace;background:#f3f3f3;border-radius:999px;padding:4px 8px}"
        "canvas{width:100%;height:220px;background:#fafafa;border:1px solid #eee;border-radius:8px}"
        "button{padding:8px 10px;border-radius:8px;border:1px solid #ddd;background:#fff}"
        "a.btn{display:inline-block;padding:8px 10px;border-radius:8px;border:1px solid #ddd;background:#fff;text-decoration:none;color:#000}"
        "</style></head><body>"
        "<h1 style='font-size:20px;margin:0 0 12px'>TheraLink</h1>"
        "<div class=grid>"
          "<div class=card>"
            "<div class=title>BPM (ao vivo / m&eacute;dia)</div>"
            "<div id=bigBpm class=kpi>--</div>"
            "<canvas id=line></canvas>"
            "<div class=row>"
              "<span class=chip id=nchip>n=0</span>"
              "<span class=chip id=ts>--</span>"
              "<button onclick='hist=[];drawLine()'>Limpar</button>"
              "<a class='btn' href='/session.csv'>Baixar CSV</a>"
            "</div>"
          "</div>"
          "<div class=card>"
            "<div class=title>Resumo</div>"
            "<div class=row style='gap:20px'>"
              "<div><div style='font-size:12px;color:#666'>Ansiedade m&eacute;dia</div>"
              "<div id=ans class=kpi>--</div></div>"
              "<div style='flex:1'><canvas id=bars></canvas></div>"
            "</div>"
          "</div>"
        "</div>"
        "<script>"
        "let hist=[];const maxPts=180;"
        "const L=document.getElementById('line'),B=document.getElementById('bars');"
        "const lc=L.getContext('2d'),bc=B.getContext('2d');"
        "function drawLine(){const w=L.clientWidth,h=L.clientHeight;L.width=w;L.height=h;"
          "lc.clearRect(0,0,w,h);if(hist.length<2)return;"
          "let mn=200,mx=40;for(const v of hist){if(v>0){mn=Math.min(mn,v);mx=Math.max(mx,v);}}"
          "if(!isFinite(mn)||!isFinite(mx))return;if(mx-mn<5){mn=Math.max(20,mn-3);mx=mn+5;}"
          "lc.beginPath();for(let i=0;i<hist.length;i++){const v=hist[i];if(v<=0)continue;"
            "const x=i*(w-8)/(hist.length-1)+4;const y=h-4-(v-mn)/(mx-mn)*(h-8);"
            "i?lc.lineTo(x,y):lc.moveTo(x,y);} lc.stroke();}"
        "function drawBars(v,a,rm){const w=B.clientWidth,h=B.clientHeight;B.width=w;B.height=h;bc.clearRect(0,0,w,h);"
          "const data=[v,a,rm],lbl=['Verde','Amarelo','Vermelho'];"
          "const bw=Math.min(60,(w-40)/3),gap=(w-3*bw)/4;let x=gap;const M=Math.max(...data,1);"
          "for(let i=0;i<3;i++){const val=data[i];const y=h-20;const bh=(val/M)*(h-50);"
            "bc.fillRect(x,y-bh,bw,bh);bc.fillText(lbl[i],x,y+12);bc.fillText(String(val),x+bw/2-6,y-bh-6);x+=bw+gap;}}"
        "function ts(){return new Date().toLocaleTimeString();}"
        "async function tick(){try{const r=await fetch('/stats.json',{cache:'no-store'});const s=await r.json();"
            "const live=(s.bpm_live&&s.bpm_live>=20&&s.bpm_live<=250)?s.bpm_live:0;"
            "const main=live||s.bpm_mean||0;"
            "document.getElementById('bigBpm').textContent=main?main.toFixed(1):'--';"
            "document.getElementById('ans').textContent=(s.ans_mean>0)?s.ans_mean.toFixed(2):'--';"
            "document.getElementById('nchip').textContent='n='+s.bpm_n;document.getElementById('ts').textContent=ts();"
            "const plotted=live||s.bpm_mean||0; if(plotted){hist.push(plotted); if(hist.length>maxPts)hist.shift(); drawLine();}"
            "drawBars(s.cores.verde||0,s.cores.amarelo||0,s.cores.vermelho||0);"
          "}catch(e){/* ok ficar quieto se desconectar */}}"
        "setInterval(tick,1000); tick();"
        "</script></body></html>";

    static char page[7600];
    snprintf(page, sizeof page, "%s", body);

    snprintf(out, outsz,
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html; charset=UTF-8\r\n"
        "Cache-Control: no-store\r\n"
        "Connection: close\r\n\r\n%s", page);
}

/* -------------------- JSON (resumo) -------------------- */
static void make_json(char *out, size_t outsz) {
    stats_snapshot_t s; stats_get_snapshot(&s);
    float bpm_mean = isnan(s.bpm_mean_trimmed) ? 0.f : s.bpm_mean_trimmed;
    float ans_mean = isnan(s.ans_mean) ? 0.f : s.ans_mean;
    const float bpm_live = 0.f; // se quiser “ao vivo”, exponha em stats.c/h

    snprintf(out, outsz,
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json; charset=UTF-8\r\n"
        "Cache-Control: no-store\r\n"
        "Connection: close\r\n\r\n"
        "{"
          "\"bpm_live\":%.3f,"
          "\"bpm_mean\":%.3f,\"bpm_n\":%lu,"
          "\"cores\":{\"verde\":%lu,\"amarelo\":%lu,\"vermelho\":%lu},"
          "\"ans_mean\":%.3f,\"ans_n\":%lu"
        "}",
        bpm_live,
        bpm_mean, (unsigned long)s.bpm_count,
        (unsigned long)s.cor_verde, (unsigned long)s.cor_amarelo, (unsigned long)s.cor_vermelho,
        ans_mean, (unsigned long)s.ans_count
    );
}

/* -------------------- CSV (download) -------------------- */
static void make_csv(char *out, size_t outsz) {
    stats_snapshot_t s; stats_get_snapshot(&s);
    float bpm_mean = isnan(s.bpm_mean_trimmed) ? 0.f : s.bpm_mean_trimmed;
    float ans_mean = isnan(s.ans_mean) ? 0.f : s.ans_mean;

    // Cabeçalho HTTP com Content-Disposition para forçar download
    int n = snprintf(out, outsz,
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/csv; charset=UTF-8\r\n"
        "Cache-Control: no-store\r\n"
        "Content-Disposition: attachment; filename=\"theralink_session.csv\"\r\n"
        "Connection: close\r\n\r\n"
        "metric,value\r\n"
        "bpm_mean,%.3f\r\n"
        "bpm_n,%lu\r\n"
        "ans_mean,%.3f\r\n"
        "ans_n,%lu\r\n"
        "cores_verde,%lu\r\n"
        "cores_amarelo,%lu\r\n"
        "cores_vermelho,%lu\r\n",
        bpm_mean, (unsigned long)s.bpm_count,
        ans_mean, (unsigned long)s.ans_count,
        (unsigned long)s.cor_verde, (unsigned long)s.cor_amarelo, (unsigned long)s.cor_vermelho
    );
    (void)n;
}

/* -------------------- HTTP -------------------- */
static err_t http_recv_cb(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    (void)arg; (void)err;
    if (!p) { tcp_close(tpcb); return ERR_OK; }

    char req[256]={0};
    size_t n = p->tot_len < sizeof(req)-1 ? p->tot_len : sizeof(req)-1;
    pbuf_copy_partial(p, req, n, 0);
    tcp_recved(tpcb, p->tot_len);
    pbuf_free(p);

    bool want_json = (strncmp(req, "GET /stats.json", 15) == 0);
    bool want_csv  = (strncmp(req, "GET /session.csv", 16) == 0);

    if (want_csv)      make_csv (g_resp, sizeof(g_resp));
    else if (want_json) make_json(g_resp, sizeof(g_resp));
    else                make_html(g_resp, sizeof(g_resp));

    g_tx.buf = g_resp;
    g_tx.len = (u16_t)strlen(g_resp);
    g_tx.off = 0;

    tcp_sent(tpcb, http_sent_cb);
    if (http_send_chunk(tpcb)) {
        tcp_sent(tpcb, NULL);
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
    stats_init();

    if (cyw43_arch_init()) { printf("WiFi init falhou\n"); return; }
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);

    cyw43_arch_enable_ap_mode(AP_SSID, AP_PASS, CYW43_AUTH_WPA2_AES_PSK);
    printf("AP SSID=%s PASS=%s\n", AP_SSID, AP_PASS);

    ip4_addr_t gw, mask;
    IP4_ADDR(ip_2_ip4(&gw),   192,168,4,1);
    IP4_ADDR(ip_2_ip4(&mask), 255,255,255,0);
    dhcp_server_init(&s_dhcp, &gw, &mask);
    dns_server_init(&s_dns, &gw);

    http_start();
    printf("AP/DHCP/DNS/HTTP prontos em 192.168.4.1\n");
}
