// main.c — Fluxo: pergunta A/B -> cor (opcional) -> oxímetro -> ansiedade (joystick)
// Publica dados via AP (DHCP/DNS/HTTP) para acesso no celular/tablet da profissional.

#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/adc.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>

#include "src/ssd1306.h"
#include "src/ssd1306_i2c.h"
#include "src/ssd1306_font.h"

#include "src/cor.h"         // cor_init, cor_read_rgb_norm, cor_classify, cor_class_to_str
#include "src/oximetro.h"    // oxi_init, oxi_start, oxi_poll, oxi_get_state, ...
#include "src/stats.h"       // stats_init, stats_add_bpm, stats_inc_color, stats_add_anxiety, stats_get_snapshot
#include "src/web_ap.h"      // web_ap_start (AP + DHCP/DNS + HTTP)

// ==== OLED em I2C1 (BitDog) ====
#define OLED_I2C   i2c1
#define OLED_SDA   14
#define OLED_SCL   15
#define OLED_ADDR  0x3C

// ==== SENSORES no I2C0 (EXTENSOR) ====
#define COL_I2C    i2c0   // TCS34725
#define COL_SDA    0
#define COL_SCL    1

#define OXI_I2C    i2c0   // MAX3010x
#define OXI_SDA    0
#define OXI_SCL    1

// Botões BitDog
#define BUTTON_A   5
#define BUTTON_B   6

// Joystick BitDog (ADC e botão)
#define JOY_ADC_Y   26   // ADC0
#define JOY_ADC_X   27   // ADC1
#define JOY_BTN     22   // botão do joystick (pull-up)

// Limiares para detectar esquerda/direita no eixo X
#define JOY_LEFT_THR   1200
#define JOY_RIGHT_THR  3000
#define JOY_ADC_MAX    4095

static ssd1306_t oled;
static bool oled_ok = false;

// contagem por classe de cor (para uso local; stats.c guarda o agregado usado no relatório)
static uint32_t g_cor_counts[COR_CLASS_COUNT] = {0};

// ---------- I2C / OLED helpers ----------
static void i2c_setup(i2c_inst_t *i2c, uint sda, uint scl, uint hz) {
    i2c_init(i2c, hz);
    gpio_set_function(sda, GPIO_FUNC_I2C);
    gpio_set_function(scl, GPIO_FUNC_I2C);
    gpio_pull_up(sda);
    gpio_pull_up(scl);
}

static void oled_lines(const char *l1, const char *l2, const char *l3, const char *l4) {
    if (!oled_ok) return;
    ssd1306_clear(&oled);
    if (l1) ssd1306_draw_string(&oled, 0,  0, 1, l1);
    if (l2) ssd1306_draw_string(&oled, 0, 16, 1, l2);
    if (l3) ssd1306_draw_string(&oled, 0, 32, 1, l3);
    if (l4) ssd1306_draw_string(&oled, 0, 48, 1, l4);
    ssd1306_show(&oled);
}

// ---------- Botões (edge) ----------
static bool edge_press(bool now, bool *prev) {
    bool fired = (now && !*prev);
    *prev = now;
    return fired;
}

// ---------- Joystick ----------
static void joystick_init(void) {
    adc_init();
    adc_gpio_init(JOY_ADC_Y); // ADC0
    adc_gpio_init(JOY_ADC_X); // ADC1

    gpio_init(JOY_BTN);
    gpio_set_dir(JOY_BTN, GPIO_IN);
    gpio_pull_up(JOY_BTN);
}

static uint16_t adc_read_channel(uint chan) {
    adc_select_input(chan);
    return adc_read(); // 12-bit (0..4095)
}

typedef struct {
    bool left_edge;
    bool right_edge;
    bool btn_edge;
} joy_events_t;

static joy_events_t joystick_poll(void) {
    static bool left_prev=false, right_prev=false, btn_prev=false;
    joy_events_t ev = (joy_events_t){0};

    uint16_t x = adc_read_channel(1); // ADC1 = X
    bool left_now  = (x < JOY_LEFT_THR);
    bool right_now = (x > JOY_RIGHT_THR);

    bool btn_now = !gpio_get(JOY_BTN); // ativo baixo

    ev.left_edge  = (!left_prev  && left_now);
    ev.right_edge = (!right_prev && right_now);
    ev.btn_edge   = edge_press(btn_now, &btn_prev);

    left_prev = left_now;
    right_prev = right_now;
    return ev;
}

// ---------- Estados ----------
typedef enum {
    ST_ASK = 0,        // Pergunta inicial
    ST_COLOR_PREP,     // Mensagem de instrução para cor
    ST_COLOR_LOOP,     // Loop de leitura de cor (A captura)
    ST_OXI_PREP,       // Inicializa/ativa oxímetro
    ST_OXI_RUN,        // Oxímetro rodando
    ST_SHOW_BPM,       // Mostra BPM final por ~3s
    ST_ANS_ASK,        // Pergunta ansiedade 1..4 (joystick)
    ST_ANS_SAVED,      // Confirma nível salvo por ~3s
    ST_REPORT          // Tela de relatório (botão do joystick para entrar/sair)
} state_t;

int main(void) {
    stdio_init_all();
    sleep_ms(300);

    // OLED em I2C1 (14/15)
    i2c_setup(OLED_I2C, OLED_SDA, OLED_SCL, 400000);
    oled.external_vcc = false;
    oled_ok = ssd1306_init(&oled, 128, 64, OLED_ADDR, OLED_I2C);

    // Botões
    gpio_init(BUTTON_A); gpio_set_dir(BUTTON_A, GPIO_IN); gpio_pull_up(BUTTON_A);
    gpio_init(BUTTON_B); gpio_set_dir(BUTTON_B, GPIO_IN); gpio_pull_up(BUTTON_B);
    bool a_prev = false, b_prev = false;

    // Joystick
    joystick_init();

    // Rede/AP + agregados
    stats_init();
    web_ap_start(); // Sobe AP (SSID: MirrorDuo / 12345678), DHCP/DNS e HTTP (/ e /stats.json)

    // Sensores (no I2C0 via extensor)
    bool cor_ready = false;
    bool oxi_inited = false;

    // Estado
    state_t st = ST_ASK;
    state_t last_st = (state_t)-1;
    uint32_t t_last = 0;
    uint32_t show_until_ms = 0; // para telas temporárias (3s)
    int ans_level = 2;          // nível de ansiedade padrão (1..4)

    while (true) {
        uint32_t now_ms = to_ms_since_boot(get_absolute_time());

        // botões
        bool a_now = !gpio_get(BUTTON_A);
        bool b_now = !gpio_get(BUTTON_B);
        bool a_edge = edge_press(a_now, &a_prev);
        bool b_edge = edge_press(b_now, &b_prev);

        // joystick
        joy_events_t jev = joystick_poll();
        bool joy_btn_edge = jev.btn_edge;

        // desenha a tela só quando muda de estado
        if (st != last_st) {
            switch (st) {
                case ST_ASK:
                    oled_lines("Fazer leitura de cor?", "(A) Sim   (B) Nao", "Botao Joy: Relatorio", "");
                    break;
                case ST_COLOR_PREP:
                    oled_lines("Lendo Cor...", "Aperte A p/ capturar", "Posicione o cartao", "");
                    break;
                case ST_COLOR_LOOP:
                    break;
                case ST_OXI_PREP:
                    oled_lines("Oximetro ativando...", "Posicione o dedo", "", "");
                    break;
                case ST_OXI_RUN:
                    break;
                case ST_SHOW_BPM:
                    break;
                case ST_ANS_ASK: {
                    char l2[22]; snprintf(l2, sizeof l2, "Nivel: %d (1..4)", ans_level);
                    oled_lines("Ansiedade", l2, "Joy<- ->   A=OK", "");
                    break;
                }
                case ST_ANS_SAVED:
                    break;
                case ST_REPORT:
                    break;
            }
            last_st = st;
        }

        switch (st) {
        case ST_ASK:
            if (a_edge) {
                // Inicia etapa de cor
                if (!cor_ready) {
                    i2c_setup(COL_I2C, COL_SDA, COL_SCL, 100000);          // I2C0 via extensor
                    cor_ready = cor_init(COL_I2C, COL_SDA, COL_SCL);
                }
                if (!cor_ready) {
                    oled_lines("TCS34725 nao encontrado", "Pulando etapa de cor", "", "");
                    sleep_ms(900);
                    st = ST_OXI_PREP;
                } else {
                    st = ST_COLOR_PREP;
                }
            } else if (b_edge) {
                // Pula direto para oxímetro
                st = ST_OXI_PREP;
            } else if (joy_btn_edge) {
                st = ST_REPORT;
                t_last = now_ms; // força atualização imediata
            }
            break;

        case ST_COLOR_PREP:
            if (now_ms - t_last > 250) { t_last = now_ms; st = ST_COLOR_LOOP; }
            break;

        case ST_COLOR_LOOP: {
            // Atualiza leitura ~5 Hz
            if (now_ms - t_last > 200) {
                t_last = now_ms;

                float rf, gf, bf, cf;
                if (cor_read_rgb_norm(&rf, &gf, &bf, &cf)) {
                    cor_class_t cls = cor_classify(rf, gf, bf, cf);
                    const char *nome = cor_class_to_str(cls);

                    char l3[24], l4[24];
                    int R = (int)(rf * 255.0f + 0.5f);
                    int G = (int)(gf * 255.0f + 0.5f);
                    int B = (int)(bf * 255.0f + 0.5f);

                    snprintf(l3, sizeof l3, "Cor: %s", nome);
                    snprintf(l4, sizeof l4, "R%3d G%3d B%3d", R, G, B);
                    oled_lines("Lendo Cor...", "Aperte A p/ capturar", l3, l4);
                } else {
                    oled_lines("Lendo Cor...", "Aperte A p/ capturar", "Sem leitura", "");
                }
            }

            if (a_edge) {
                // Captura a cor atual e avança
                float rf, gf, bf, cf;
                if (cor_read_rgb_norm(&rf, &gf, &bf, &cf)) {
                    cor_class_t cls = cor_classify(rf, gf, bf, cf);
                    g_cor_counts[cls]++;

                    // Mapeia somente se for uma das 3 cores de interesse
                    stat_color_t sc = (stat_color_t)0;
                    bool ok = true;
                    switch (cls) {
                        case COR_VERDE:    sc = STAT_COLOR_VERDE;    break;
                        case COR_AMARELO:  sc = STAT_COLOR_AMARELO;  break;
                        case COR_VERMELHO: sc = STAT_COLOR_VERMELHO; break;
                        default: ok = false; break; // ignora outras
                    }
                    if (ok) stats_inc_color(sc);

                    const char *nome = cor_class_to_str(cls);
                    char msg[26];
                    snprintf(msg, sizeof msg, "Cor %s captada!", nome);
                    oled_lines(msg, "", "", "");
                    show_until_ms = now_ms + 3000;  // ~3s na tela
                    st = ST_OXI_PREP;               // segue para oxímetro
                } else {
                    oled_lines("Falha na leitura", "Tente novamente", "", "");
                    sleep_ms(800);
                }
            }
            break;
        }

        case ST_OXI_PREP: {
            // Inicializa oxímetro (I2C0 via extensor) e inicia
            if (!oxi_inited) {
                i2c_setup(OXI_I2C, OXI_SDA, OXI_SCL, 100000);              // I2C0 via extensor
                oxi_inited = oxi_init(OXI_I2C, OXI_SDA, OXI_SCL);
            }
            if (!oxi_inited) {
                oled_lines("MAX3010x nao encontrado", "Verifique cabos", "Voltando ao menu", "");
                sleep_ms(1200);
                st = ST_ASK;
                break;
            }
            oxi_start();                                                   // liga LED e zera filtros
            t_last = now_ms;
            st = ST_OXI_RUN;
            break;
        }

        case ST_OXI_RUN: {
            // permite abortar com B
            if (b_edge) {
                oled_lines("Oximetro cancelado", "Voltando ao menu...", "", "");
                sleep_ms(700);
                st = ST_ASK;
                break;
            }

            oxi_poll(now_ms);

            // atualiza a tela a cada ~200 ms
            if (now_ms - t_last > 200) {
                t_last = now_ms;

                oxi_state_t s = oxi_get_state();
                if (s == OXI_WAIT_FINGER) {
                    oled_lines("Oximetro ativo", "Posicione o dedo", "Aguardando...", "(B) Voltar");
                } else if (s == OXI_SETTLE) {
                    oled_lines("Oximetro ativo", "Calibrando...", "Mantenha o dedo", "(B) Voltar");
                } else if (s == OXI_RUN) {
                    int n, tgt; oxi_get_progress(&n, &tgt);
                    float live = oxi_get_bpm_live();
                    char l2[22], l3[22];
                    snprintf(l2, sizeof l2, "BPM~ %.1f", live);
                    snprintf(l3, sizeof l3, "Validas: %d/%d", n, tgt);
                    oled_lines("Medindo...", l2, l3, "(B) Voltar");
                } else if (s == OXI_DONE) {
                    float final_bpm = oxi_get_bpm_final();
                    stats_add_bpm(final_bpm); // agrega para o relatório

                    char l2[22]; snprintf(l2, sizeof l2, "BPM FINAL: %.1f", final_bpm);
                    oled_lines("Concluido!", l2, "", "");
                    show_until_ms = now_ms + 3000; // ~3s
                    st = ST_SHOW_BPM;
                } else if (s == OXI_ERROR) {
                    oled_lines("ERRO no oximetro", "Cheque conexoes", "", "");
                    sleep_ms(1500);
                    st = ST_ASK;
                }
            }
            break;
        }

        case ST_SHOW_BPM:
            if ((int32_t)(show_until_ms - now_ms) <= 0) {
                ans_level = 2;
                char l2[22]; snprintf(l2, sizeof l2, "Nivel: %d (1..4)", ans_level);
                oled_lines("Ansiedade", l2, "Joy<- ->   A=OK", "");
                st = ST_ANS_ASK;
            }
            break;

        case ST_ANS_ASK: {
            // joystick muda valor 1..4, A confirma
            bool changed = false;
            if (jev.left_edge && ans_level > 1)  { ans_level--; changed = true; }
            if (jev.right_edge && ans_level < 4) { ans_level++; changed = true; }
            if (changed) {
                char l2[22]; snprintf(l2, sizeof l2, "Nivel: %d (1..4)", ans_level);
                oled_lines("Ansiedade", l2, "Joy<- ->   A=OK", "");
            }
            if (a_edge) {
                stats_add_anxiety((uint8_t)ans_level);
                char msg[24]; snprintf(msg, sizeof msg, "Nivel %d registrado", ans_level);
                oled_lines(msg, "", "", "");
                show_until_ms = now_ms + 3000; // 3s
                st = ST_ANS_SAVED;
            }
            break;
        }

        case ST_ANS_SAVED:
            if ((int32_t)(show_until_ms - now_ms) <= 0) {
                st = ST_ASK;
            }
            break;

        case ST_REPORT: {
        // Atualiza a cada ~1s
        if (now_ms - t_last > 1000) {
            t_last = now_ms;
            stats_snapshot_t snap; stats_get_snapshot(&snap);

            char l1[22], l2[22], l3[22];
            float bpm = snap.bpm_mean_trimmed;
            float ans = snap.ans_mean;

            if (isnan(bpm)) snprintf(l1, sizeof l1, "BPM: --");
            else            snprintf(l1, sizeof l1, "BPM: %.1f (n=%lu)",
                                    bpm, (unsigned long)snap.bpm_count);

            snprintf(l2, sizeof l2, "V:%lu A:%lu R:%lu",
                    (unsigned long)snap.cor_verde,
                    (unsigned long)snap.cor_amarelo,
                    (unsigned long)snap.cor_vermelho);

            if (isnan(ans)) snprintf(l3, sizeof l3, "Ans: --  Joy=sair");
            else            snprintf(l3, sizeof l3, "Ans: %.2f (n=%lu)",
                                    ans, (unsigned long)snap.ans_count);

            // desenha exatamente 4 linhas (sem draw extra)
            oled_lines("Relatorio Grupo", l1, l2, l3);
        }
        if (joy_btn_edge) {
            st = ST_ASK;
        }
        break;
    }

        }

        sleep_ms(10);
    }
}
