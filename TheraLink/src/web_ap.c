// Web AP com rotas:
//   /                -> Painel do profissional
//   /display         -> Espelho do OLED (sempre display)
//   /oled.json       -> JSON com as 4 linhas do OLED
//   /stats.json      -> Métricas agregadas (aceita ?color=verde|amarelo|vermelho)
//   /download.csv    -> CSV simples (fallback local)
//   /survey          -> Página única do questionário (10 perguntas sim/não)
//   /survey_submit   -> Endpoint de submissão (?ans=10 bits)
//   /survey_state.json -> {"mode":0|1} para o /display saber quando redirecionar

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

#define AP_SSID   "TheraLink"
#define AP_PASS   ""
#define HTTP_PORT 80

static dhcp_server_t s_dhcp;
static dns_server_t  s_dns;

/* ---------- Estado OLED (espelho) ---------- */
typedef struct {
    char l1[32], l2[32], l3[32], l4[32];
} oled_state_t;

static oled_state_t g_oled = { "", "", "", "" };

void web_display_set_lines(const char *l1, const char *l2, const char *l3, const char *l4) {
    snprintf(g_oled.l1, sizeof g_oled.l1, "%s", l1 ? l1 : "");
    snprintf(g_oled.l2, sizeof g_oled.l2, "%s", l2 ? l2 : "");
    snprintf(g_oled.l3, sizeof g_oled.l3, "%s", l3 ? l3 : "");
    snprintf(g_oled.l4, sizeof g_oled.l4, "%s", l4 ? l4 : "");
}

/* ---------- Estado do SURVEY ---------- */
static volatile bool s_survey_mode = false;  // 1 = deve abrir /survey no painel
static volatile bool s_survey_has  = false;  // 1 = novas respostas disponíveis
static char s_survey_ans[12] = "";          // 10 bits + '\0'

void web_survey_begin(void) {
    s_survey_mode = true;
    s_survey_has  = false;
    s_survey_ans[0] = '\0';
}

bool web_take_survey(char out_bits_10[11]) {
    if (!s_survey_has) return false;
    memcpy(out_bits_10, s_survey_ans, 11);
    s_survey_has  = false;
    s_survey_mode = false; // encerra modo survey ao consumir
    return true;
}

/* ---------- TX state (1 conexão por vez) ---------- */
typedef struct {
    const char *buf;
    u16_t len;
    u16_t off;
} http_tx_t;

static http_tx_t g_tx = {0};
static char g_resp[8192];

static bool http_send_chunk(struct tcp_pcb *tpcb) {
    while (g_tx.off < g_tx.len) {
        u16_t wnd = tcp_sndbuf(tpcb);
        if (!wnd) break;
        u16_t chunk = g_tx.len - g_tx.off;
        if (chunk > 1200) chunk = 1200;
        if (chunk > wnd)  chunk = wnd;
        err_t e = tcp_write(tpcb, g_tx.buf + g_tx.off, chunk, TCP_WRITE_FLAG_COPY);
        if (e == ERR_MEM) break;
        if (e != ERR_OK) { tcp_abort(tpcb); return true; }
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

/* ---------- helpers: parse color query ---------- */
static bool parse_color_query(const char *req, stat_color_t *out_color, bool *has_color) {
    *has_color = false;
    if (!req) return false;

    const char *q = strstr(req, "GET /stats.json");
    if (!q) return true;

    const char *p = strstr(q, "color=");
    if (!p) return true;

    p += 6;
    if (!strncmp(p, "verde", 5))      { *out_color = STAT_COLOR_VERDE; *has_color = true; return true; }
    if (!strncmp(p, "amarelo", 7))    { *out_color = STAT_COLOR_AMARELO; *has_color = true; return true; }
    if (!strncmp(p, "vermelho", 8))   { *out_color = STAT_COLOR_VERMELHO; *has_color = true; return true; }

    return true;
}

/* ---------- HTML: Painel Profissional (/) ---------- */
static void make_html_pro(char *out, size_t outsz) {
    const char *body =
        "<!doctype html><html lang=pt-br><head><meta charset=utf-8>"
        "<meta name=viewport content='width=device-width,initial-scale=1'>"
        "<title>TheraLink — Profissional</title>"
        "<style>"
        "body{font-family:system-ui,-apple-system,Segoe UI,Roboto,Arial,sans-serif;margin:16px;background:#f6f7fb;color:#111}"
        "nav{display:flex;gap:12px;margin-bottom:12px;flex-wrap:wrap}"
        "nav a{padding:8px 12px;border:1px solid #ddd;background:#fff;border-radius:10px;text-decoration:none;color:#111}"
        ".grid{display:grid;grid-template-columns:1fr;gap:12px}@media(min-width:760px){.grid{grid-template-columns:1fr 1fr}}"
        ".card{border:1px solid #e6e6e9;border-radius:14px;padding:14px;background:#fff;box-shadow:0 4px 20px rgba(0,0,0,.04)}"
        ".title{margin:0 0 8px;font-size:18px}.kpi{font-size:34px;font-weight:800;margin:6px 0}"
        ".row{display:flex;gap:12px;align-items:flex-start;flex-wrap:wrap}"
        "canvas{width:100%;height:220px;background:#fafafa;border:1px solid #eee;border-radius:10px}"
        "button{padding:8px 10px;border-radius:10px;border:1px solid #ddd;background:#fff}"
        ".mini .kpi{font-size:26px;margin:4px 0}"
        ".chips{display:flex;gap:8px;flex-wrap:wrap;margin:6px 0 10px}"
        ".chip{display:inline-flex;align-items:center;gap:8px;padding:8px 12px;border:1px solid #e2e2e6;border-radius:999px;background:#fff;cursor:pointer;user-select:none}"
        ".chip .dot{width:10px;height:10px;border-radius:999px;display:inline-block}"
        ".chip[data-c='all'] .dot{background:linear-gradient(90deg,#12b886,#fab005,#fa5252)}"
        ".chip[data-c='verde'] .dot{background:#12b886}"
        ".chip[data-c='amarelo'] .dot{background:#fab005}"
        ".chip[data-c='vermelho'] .dot{background:#fa5252}"
        ".chip.active{box-shadow:0 0 0 2px rgba(17,17,17,.08);border-color:#c9c9cf}"
        ".hint{font-size:12px;color:#666}"
        "</style></head><body>"
        "<nav>"
          "<a href='/'>Profissional</a>"
          "<a href='/display'>Display</a>"
          "<a href='/download.csv'>Baixar CSV</a>"
        "</nav>"
        "<h1 style='font-size:20px;margin:6px 0 8px'>Painel — Profissional</h1>"
        "<div class='chips' id='chips'>"
          "<div class='chip active' data-c='all'><span class='dot'></span><span>Todos</span></div>"
          "<div class='chip' data-c='verde'><span class='dot'></span><span>Grupo Verde</span></div>"
          "<div class='chip' data-c='amarelo'><span class='dot'></span><span>Grupo Amarelo</span></div>"
          "<div class='chip' data-c='vermelho'><span class='dot'></span><span>Grupo Vermelho</span></div>"
        "</div>"
        "<div class='hint'>Filtre para analisar apenas um grupo de pulseira.</div>"
        "<div class=grid>"
          "<div class=card>"
            "<div class=title>BPM (ao vivo / m&eacute;dia)</div>"
            "<div id=bigBpm class=kpi>--</div>"
            "<canvas id=line></canvas>"
            "<div class=row>"
              "<span id=nchip class='hint'>n=0</span>"
              "<span id=ts class='hint'>--</span>"
              "<button onclick='hist=[];drawLine()'>Limpar</button>"
            "</div>"
          "</div>"
          "<div class=card>"
            "<div class=title>Resumo</div>"
            "<div class=row>"
              "<div class=mini style='min-width:180px'>"
                "<div style='font-size:12px;color:#666'>Ansiedade m&eacute;dia</div>"
                "<div id=ans class=kpi>--</div>"
                "<div style='font-size:12px;color:#666;margin-top:4px'>Energia m&eacute;dia</div>"
                "<div id=ene class=kpi>--</div>"
                "<div style='font-size:12px;color:#666;margin-top:4px'>Humor m&eacute;dio</div>"
                "<div id=hum class=kpi>--</div>"
              "</div>"
              "<div style='flex:1'><canvas id=bars></canvas></div>"
            "</div>"
          "</div>"
        "</div>"
        "<script>"
        "let hist=[];const maxPts=180;let flt='all';"
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
        "function sel(c){flt=c;document.querySelectorAll('.chip').forEach(el=>{el.classList.toggle('active',el.dataset.c===c);});hist=[];drawLine();tick();}"
        "document.getElementById('chips').addEventListener('click',e=>{const el=e.target.closest('.chip');if(!el)return;sel(el.dataset.c)});"
        "async function tick(){try{let url='/stats.json';if(flt!=='all'){url+='?color='+flt;}const r=await fetch(url,{cache:'no-store'});const s=await r.json();"
            "const live=(s.bpm_live&&s.bpm_live>=20&&s.bpm_live<=250)?s.bpm_live:0;"
            "const main=live||s.bpm_mean||0;"
            "document.getElementById('bigBpm').textContent=main?main.toFixed(1):'--';"
            "document.getElementById('ans').textContent=(s.ans_mean>0)?s.ans_mean.toFixed(2):'--';"
            "document.getElementById('ene').textContent=(s.energy_mean>0)?s.energy_mean.toFixed(2):'--';"
            "document.getElementById('hum').textContent=(s.humor_mean>0)?s.humor_mean.toFixed(2):'--';"
            "document.getElementById('nchip').textContent='n='+s.bpm_n;document.getElementById('ts').textContent=ts();"
            "const plotted=live||s.bpm_mean||0; if(plotted){hist.push(plotted); if(hist.length>maxPts)hist.shift(); drawLine();}"
            "drawBars(s.cores.verde||0,s.cores.amarelo||0,s.cores.vermelho||0);"
          "}catch(e){}}"
        "setInterval(tick,1000); tick();"
        "</script></body></html>";

    snprintf(out, outsz,
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html; charset=UTF-8\r\n"
        "Cache-Control: no-store\r\n"
        "Connection: close\r\n\r\n%s", body);
}

/* ---------- HTML: Display (espelho do OLED) ---------- */
static void make_html_display(char *out, size_t outsz) {
    const char *body =
        "<!doctype html><html lang=pt-br><head><meta charset=utf-8>"
        "<meta name=viewport content='width=device-width,initial-scale=1'>"
        "<title>TheraLink — Display</title>"
        "<style>"
        "html,body{height:100%;margin:0}"
        "body{font-family:system-ui,-apple-system,Segoe UI,Roboto,Arial,sans-serif;"
              "background:radial-gradient(60% 80% at 50% 10%,#171a20,#0e1014);color:#f2f4f8;"
              "display:flex;align-items:center;justify-content:center}"
        ".panel{width:min(960px,94vw);padding:24px 22px;border-radius:20px;"
               "background:linear-gradient(180deg,#141821,#101218);"
               "box-shadow:0 12px 40px rgba(0,0,0,.45),inset 0 1px rgba(255,255,255,.05)}"
        ".hdr{display:flex;justify-content:space-between;align-items:center;margin-bottom:8px;opacity:.9}"
        ".hdr .brand{font-weight:700;letter-spacing:.3px}"
        ".btn{font-size:12px;padding:6px 10px;border-radius:10px;border:1px solid #303440;background:#1a1f2b;color:#f2f4f8}"
        ".lines{display:grid;gap:6px;margin-top:8px}"
        ".line{min-height:1lh;font-weight:800;letter-spacing:.5px;text-shadow:0 2px 10px rgba(0,0,0,.25);"
               "padding:2px 4px;border-radius:8px}"
        "#l1{font-size:clamp(20px,6.2vh,36px)}"
        "#l2{font-size:clamp(22px,7.2vh,44px)}"
        "#l3{font-size:clamp(22px,7.2vh,44px)}"
        "#l4{font-size:clamp(22px,7.2vh,44px)}"
        ".fade{animation:fade .22s ease}"
        "@keyframes fade{from{opacity:.45;transform:translateY(1px)}to{opacity:1;transform:none}}"
        ".tag{font-weight:900}.tag.green{color:#12b886}.tag.yellow{color:#fab005}.tag.red{color:#fa5252}"
        "</style></head><body>"
        "<div class=panel>"
          "<div class=hdr>"
            "<div class=brand>TheraLink — Display</div>"
            "<button class=btn onclick='fs()'>Tela cheia</button>"
          "</div>"
          "<div class=lines>"
            "<div id=l1 class='line'>&nbsp;</div>"
            "<div id=l2 class='line'>&nbsp;</div>"
            "<div id=l3 class='line'>&nbsp;</div>"
            "<div id=l4 class='line'>&nbsp;</div>"
          "</div>"
        "</div>"
        "<script>"
        "function fs(){const d=document.documentElement; if(d.requestFullscreen) d.requestFullscreen();}"
        "let last=['','','',''];"
        "function esc(t){return (t||'').replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;');}"
        "function colorize(t){let x=esc(t||'');"
          "x=x.replace(/\\b(VERDE|Verde)\\b/g,'<span class=\"tag green\">$1</span>');"
          "x=x.replace(/\\b(AMARELA|Amarela|AMARELO|Amarelo)\\b/g,'<span class=\"tag yellow\">$1</span>');"
          "x=x.replace(/\\b(VERMELHA|Vermelha|VERMELHO|Vermelho)\\b/g,'<span class=\"tag red\">$1</span>');"
          "return x;}"
        "async function pollSurvey(){try{const r=await fetch('/survey_state.json',{cache:'no-store'});const s=await r.json();if(s&&s.mode===1){location.replace('/survey');}}catch(e){}}"
        "async function tick(){try{const r=await fetch('/oled.json',{cache:'no-store'});const s=await r.json();const arr=[s.l1||'',s.l2||'',s.l3||'',s.l4||''];"
          "for(let i=0;i<4;i++){if(arr[i]!==last[i]){last[i]=arr[i];const id='l'+(i+1),el=document.getElementById(id);"
            "el.classList.remove('fade');el.innerHTML=colorize(arr[i])||'&nbsp;';void el.offsetWidth; el.classList.add('fade');}}"
        "}catch(e){}}"
        "setInterval(pollSurvey,500); pollSurvey();"
        "setInterval(tick,500); tick();"
        "</script></body></html>";

    snprintf(out, outsz,
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html; charset=UTF-8\r\n"
        "Cache-Control: no-store\r\n"
        "Connection: close\r\n\r\n%s", body);
}

/* ---------- HTML: Survey (/survey) ---------- */
static void make_html_survey(char *out, size_t outsz) {
    // Se modo survey estiver desligado, oferece um "voltar" para o display
    const char *body_prefix =
        "<!doctype html><html lang=pt-br><head><meta charset=utf-8>"
        "<meta name=viewport content='width=device-width,initial-scale=1'>"
        "<title>TheraLink — Survey</title>"
        "<style>"
        "body{font-family:system-ui,-apple-system,Segoe UI,Roboto,Arial,sans-serif;margin:18px;background:#0f1220;color:#eef1f6}"
        ".wrap{max-width:920px;margin:0 auto}"
        "h1{font-size:22px;margin:0 0 12px}"
        ".card{background:#13172a;border:1px solid #252b45;border-radius:14px;padding:16px;margin:12px 0}"
        ".q{display:flex;justify-content:space-between;align-items:center;padding:12px 10px;border-bottom:1px solid #1e2440}"
        ".q:last-child{border-bottom:none}"
        ".lbl{max-width:74%;line-height:1.35}"
        ".btns{display:flex;gap:8px}"
        "button,.chip{padding:10px 12px;border-radius:12px;border:1px solid #2b3358;background:#0f1428;color:#eef1f6;cursor:pointer}"
        ".primary{background:#2d6cdf;border-color:#2d6cdf}"
        ".muted{opacity:.8}"
        ".row{display:flex;gap:10px;flex-wrap:wrap}"
        "a{color:#cfe1ff;text-decoration:none}"
        "</style></head><body><div class=wrap>"
        "<h1>Questionario rapido (10 perguntas)</h1>";

    const char *body_main =
        "<div id=content class=card>"
        "<div class=q><div class=lbl>Teve dor forte hoje?</div><div class=btns><span class=chip data-v='1'>Sim</span><span class=chip data-v='0'>Nao</span></div></div>"
        "<div class=q><div class=lbl>Comeu nas ultimas horas?</div><div class=btns><span class=chip data-v='1'>Sim</span><span class=chip data-v='0'>Nao</span></div></div>"
        "<div class=q><div class=lbl>Dormiu bem?</div><div class=btns><span class=chip data-v='1'>Sim</span><span class=chip data-v='0'>Nao</span></div></div>"
        "<div class=q><div class=lbl>Sente fadiga forte agora?</div><div class=btns><span class=chip data-v='1'>Sim</span><span class=chip data-v='0'>Nao</span></div></div>"
        "<div class=q><div class=lbl>Teve conflito forte com alguem?</div><div class=btns><span class=chip data-v='1'>Sim</span><span class=chip data-v='0'>Nao</span></div></div>"
        "<div class=q><div class=lbl>Se sentiu muito nervoso(a) hoje?</div><div class=btns><span class=chip data-v='1'>Sim</span><span class=chip data-v='0'>Nao</span></div></div>"
        "<div class=q><div class=lbl>Teve dificuldade de concentrar nas ultimas horas?</div><div class=btns><span class=chip data-v='1'>Sim</span><span class=chip data-v='0'>Nao</span></div></div>"
        "<div class=q><div class=lbl>Sente risco de ter uma crise agora?</div><div class=btns><span class=chip data-v='1'>Sim</span><span class=chip data-v='0'>Nao</span></div></div>"
        "<div class=q><div class=lbl>Esta evitando estar com o grupo hoje?</div><div class=btns><span class=chip data-v='1'>Sim</span><span class=chip data-v='0'>Nao</span></div></div>"
        "<div class=q><div class=lbl>Quer falar com um adulto apos o check-in?</div><div class=btns><span class=chip data-v='1'>Sim</span><span class=chip data-v='0'>Nao</span></div></div>"
        "</div>"
        "<div class=row>"
        "<button id=send class=primary>Enviar respostas</button>"
        "<a class=muted href='/display' id=back>Voltar ao display</a>"
        "</div>"
        "<p class=muted style='margin-top:8px'>Se a tela do display estiver nesta pagina, volte para <a href='/display'>/display</a>.</p>"
        "<script>"
        "const chips=[...document.querySelectorAll('.chip')];"
        "const vals=new Array(10).fill(-1);"
        "chips.forEach((c,idx)=>{"
          "c.addEventListener('click',()=>{"
            "const v=c.dataset.v; const q=Math.floor(chips.indexOf?chips.indexOf(c)/2:idx/2);"
            "const qi=Math.floor(idx/2);"
            "vals[qi]=Number(v);"
            "const sibs=c.parentElement.querySelectorAll('.chip');"
            "sibs.forEach(s=>s.style.outline='');"
            "c.style.outline='2px solid #2d6cdf';"
          "});"
        "});"
        "document.getElementById('send').onclick=()=>{"
          "if(vals.some(v=>v<0)){alert('Responda todas as perguntas.');return;}"
          "const bits=vals.map(v=>v?1:0).join('');"
          "location.replace('/survey_submit?ans='+bits);"
        "};"
        "</script>";

    const char *body_closed =
        "<div class=card><p>Questionario encerrado.</p>"
        "<p><a href='/display'>Voltar ao display</a></p></div>";

    const char *end = "</div></body></html>";

    if (s_survey_mode) {
        snprintf(out, outsz,
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html; charset=UTF-8\r\n"
            "Cache-Control: no-store\r\n"
            "Connection: close\r\n\r\n%s%s%s", body_prefix, body_main, end);
    } else {
        snprintf(out, outsz,
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html; charset=UTF-8\r\n"
            "Cache-Control: no-store\r\n"
            "Connection: close\r\n\r\n%s%s%s", body_prefix, body_closed, end);
    }
}

/* ---------- JSON: survey_state (/survey_state.json) ---------- */
static void make_json_survey_state(char *out, size_t outsz) {
    snprintf(out, outsz,
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json; charset=UTF-8\r\n"
        "Cache-Control: no-store\r\n"
        "Connection: close\r\n\r\n"
        "{\"mode\":%d}", s_survey_mode ? 1 : 0);
}

/* ---------- JSON: stats (/stats.json[?color=...]) ---------- */
static void make_json_stats(char *out, size_t outsz, const char *req_line) {
    stats_snapshot_t s;
    stat_color_t col = STAT_COLOR_VERDE; bool has = false;
    parse_color_query(req_line, &col, &has);

    if (has) stats_get_snapshot_by_color(col, &s);
    else     stats_get_snapshot(&s);

    float bpm_mean = isnan(s.bpm_mean_trimmed) ? 0.f : s.bpm_mean_trimmed;
    float ans_mean = isnan(s.ans_mean) ? 0.f : s.ans_mean;
    float ene_mean = isnan(s.energy_mean) ? 0.f : s.energy_mean;
    float hum_mean = isnan(s.humor_mean) ? 0.f : s.humor_mean;

    const float bpm_live = 0.f;

    snprintf(out, outsz,
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json; charset=UTF-8\r\n"
        "Cache-Control: no-store\r\n"
        "Connection: close\r\n\r\n"
        "{"
          "\"bpm_live\":%.3f,"
          "\"bpm_mean\":%.3f,\"bpm_n\":%lu,"
          "\"cores\":{\"verde\":%lu,\"amarelo\":%lu,\"vermelho\":%lu},"
          "\"ans_mean\":%.3f,\"ans_n\":%lu,"
          "\"energy_mean\":%.3f,\"energy_n\":%lu,"
          "\"humor_mean\":%.3f,\"humor_n\":%lu"
        "}",
        bpm_live,
        bpm_mean, (unsigned long)s.bpm_count,
        (unsigned long)s.cor_verde, (unsigned long)s.cor_amarelo, (unsigned long)s.cor_vermelho,
        ans_mean, (unsigned long)s.ans_count,
        ene_mean, (unsigned long)s.energy_count,
        hum_mean, (unsigned long)s.humor_count
    );
}

/* ---------- JSON: OLED (/oled.json) ---------- */
static void make_json_oled(char *out, size_t outsz) {
    snprintf(out, outsz,
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json; charset=UTF-8\r\n"
        "Cache-Control: no-store\r\n"
        "Connection: close\r\n\r\n"
        "{"
          "\"l1\":\"%s\","
          "\"l2\":\"%s\","
          "\"l3\":\"%s\","
          "\"l4\":\"%s\""
        "}",
        g_oled.l1, g_oled.l2, g_oled.l3, g_oled.l4
    );
}

/* ---------- CSV fallback (antigo) ---------- */
static size_t dump_csv_fallback(char *out, size_t maxlen) {
    stats_snapshot_t s; stats_get_snapshot(&s);
    double bpm_mean = isnan(s.bpm_mean_trimmed) ? 0.0 : s.bpm_mean_trimmed;
    double ans_mean = isnan(s.ans_mean)         ? 0.0 : s.ans_mean;
    double ene_mean = isnan(s.energy_mean)      ? 0.0 : s.energy_mean;
    double hum_mean = isnan(s.humor_mean)       ? 0.0 : s.humor_mean;

    return snprintf(out, maxlen,
        "bpm_mean,bpm_n,ans_mean,ans_n,energy_mean,energy_n,humor_mean,humor_n,verde,amarelo,vermelho\r\n"
        "%.3f,%lu,%.3f,%lu,%.3f,%lu,%.3f,%lu,%lu,%lu,%lu\r\n",
        bpm_mean,  (unsigned long)s.bpm_count,
        ans_mean,  (unsigned long)s.ans_count,
        ene_mean,  (unsigned long)s.energy_count,
        hum_mean,  (unsigned long)s.humor_count,
        (unsigned long)s.cor_verde,
        (unsigned long)s.cor_amarelo,
        (unsigned long)s.cor_vermelho
    );
}

/* ---------- CSV (download.csv) ---------- */
static void make_csv(char *out, size_t outsz) {
    size_t hdr_len = snprintf(out, outsz,
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/csv; charset=UTF-8\r\n"
        "Content-Disposition: attachment; filename=\"theralink_dados.csv\"\r\n"
        "Cache-Control: no-store\r\n"
        "Connection: close\r\n\r\n");
    if (hdr_len >= outsz) return;

    size_t body_max = outsz - hdr_len;
    size_t csv_len  = stats_dump_csv(out + hdr_len, body_max);

    size_t total = hdr_len + csv_len;
    if (total >= outsz) total = outsz - 1;
    out[total] = '\0';
}

/* ---------- HTTP ---------- */
static void make_redirect_display(char *out, size_t outsz) {
    // 303 + fallback HTML (melhor comportamento em navegadores embarcados)
    snprintf(out, outsz,
        "HTTP/1.1 303 See Other\r\n"
        "Location: /display\r\n"
        "Cache-Control: no-store\r\n"
        "Connection: close\r\n\r\n"
        "<!doctype html><meta http-equiv='refresh' content='0;url=/display'>OK");
}

static err_t http_recv_cb(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    (void)arg; (void)err;
    if (!p) { tcp_close(tpcb); return ERR_OK; }

    char req[256]={0};
    size_t n = p->tot_len < sizeof(req)-1 ? p->tot_len : sizeof(req)-1;
    pbuf_copy_partial(p, req, n, 0);
    tcp_recved(tpcb, p->tot_len);
    pbuf_free(p);

    // Checagens de rota (use memcmp com tamanho EXATO da "prefix string")
    bool want_stats        = (memcmp(req, "GET /stats.json",   15) == 0);
    bool want_oled         = (memcmp(req, "GET /oled.json",    14) == 0);
    bool want_display      = (memcmp(req, "GET /display",      12) == 0);
    bool want_csv          = (memcmp(req, "GET /download.csv", 17) == 0);
    bool want_survey       = (memcmp(req, "GET /survey",       11) == 0); // vale também para "/survey HTTP/1.1"
    bool want_survey_state = (memcmp(req, "GET /survey_state.json", 22) == 0);
    bool want_submit       = (memcmp(req, "GET /survey_submit", 18) == 0); // *** conserta o bug do 19 vs 18 ***

    if (want_submit) {
        // Parse ans=########## (10 bits)
        const char *a = strstr(req, "ans=");
        char tmp[12] = {0};
        if (a) {
            a += 4;
            size_t i = 0;
            while (i < 10 && (a[i] == '0' || a[i] == '1')) { tmp[i] = a[i]; i++; }
            tmp[i] = '\0';
        }
        if (tmp[0]) {
            strncpy((char*)s_survey_ans, tmp, sizeof s_survey_ans - 1);
            s_survey_ans[10] = '\0';
            s_survey_has  = true;
            s_survey_mode = false; // encerra o modo survey
        }
        make_redirect_display(g_resp, sizeof g_resp);
    }
    else if (want_survey_state) {
        make_json_survey_state(g_resp, sizeof g_resp);
    }
    else if (want_survey) {
        make_html_survey(g_resp, sizeof g_resp);
    }
    else if (want_stats) {
        make_json_stats(g_resp, sizeof g_resp, req);
    }
    else if (want_oled) {
        make_json_oled(g_resp, sizeof g_resp);
    }
    else if (want_display) {
        make_html_display(g_resp, sizeof g_resp);
    }
    else if (want_csv) {
        make_csv(g_resp, sizeof g_resp);
    }
    else {
        make_html_pro(g_resp, sizeof g_resp);
    }

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

    cyw43_arch_enable_ap_mode(AP_SSID, NULL, CYW43_AUTH_OPEN);
    printf("AP SSID=%s (aberto, sem senha)\n", AP_SSID);

    ip4_addr_t gw, mask;
    IP4_ADDR(ip_2_ip4(&gw),   192,168,4,1);
    IP4_ADDR(ip_2_ip4(&mask), 255,255,255,0);
    dhcp_server_init(&s_dhcp, &gw, &mask);
    dns_server_init(&s_dns, &gw);

    http_start();
    printf("AP/DHCP/DNS/HTTP prontos em 192.168.4.1\n");
}
