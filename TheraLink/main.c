// main.c — Triagem -> recomenda pulseira -> valida cor -> registra métricas
// Publica dados via AP (DHCP/DNS/HTTP). /display espelha o OLED. /survey no painel.

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

#include "src/cor.h"
#include "src/oximetro.h"
#include "src/stats.h"
#include "src/web_ap.h"

// ==== OLED em I2C1 (BitDog) ====
#define OLED_I2C   i2c1
#define OLED_SDA   14
#define OLED_SCL   15
#define OLED_ADDR  0x3C

// ==== SENSORES no I2C0 ====
#define COL_I2C    i2c0   // TCS34725
#define COL_SDA    0
#define COL_SCL    1

#define OXI_I2C    i2c0   // MAX3010x
#define OXI_SDA    0
#define OXI_SCL    1

// Botões
#define BUTTON_A   5
#define BUTTON_B   6

// Joystick
#define JOY_ADC_Y   26   // ADC0
#define JOY_ADC_X   27   // ADC1
#define JOY_BTN     22

static ssd1306_t oled;
static bool oled_ok = false;

// --- I2C helper ---
static void i2c_setup(i2c_inst_t *i2c, uint sda, uint scl, uint hz) {
    i2c_init(i2c, hz);
    gpio_set_function(sda, GPIO_FUNC_I2C);
    gpio_set_function(scl, GPIO_FUNC_I2C);
    gpio_pull_up(sda);
    gpio_pull_up(scl);
}

// --- OLED + espelho web (/display)
static void oled_lines(const char *l1, const char *l2, const char *l3, const char *l4) {
    web_display_set_lines(l1, l2, l3, l4);
    if (!oled_ok) return;
    ssd1306_clear(&oled);
    if (l1) ssd1306_draw_string(&oled, 0,  0, 1, l1);
    if (l2) ssd1306_draw_string(&oled, 0, 16, 1, l2);
    if (l3) ssd1306_draw_string(&oled, 0, 32, 1, l3);
    if (l4) ssd1306_draw_string(&oled, 0, 48, 1, l4);
    ssd1306_show(&oled);
}

static bool edge_press(bool now, bool *prev) {
    bool fired = (now && !*prev);
    *prev = now;
    return fired;
}

/* ---------- Estados ---------- */
typedef enum {
    ST_ASK = 0,
    ST_OXI_RUN,
    ST_SHOW_BPM,
    ST_SURVEY_WAIT,    // aguarda respostas do /survey
    ST_TRIAGE_RESULT,
    ST_COLOR_INTRO,
    ST_COLOR_LOOP,
    ST_SAVE_AND_DONE,
    ST_REPORT
} state_t;

static const char* cor_nome(stat_color_t c) {
    switch (c) {
        case STAT_COLOR_VERDE:   return "VERDE";
        case STAT_COLOR_AMARELO: return "AMARELA";
        case STAT_COLOR_VERMELHO:return "VERMELHA";
        default: return "?";
    }
}

/* ---------- Score por survey + BPM ---------- */
static stat_color_t triage_from_survey(float bpm, const char bits[11]) {
    // bits[0..9] = 10 respostas 0/1
    int pos = 0;
    for (int i=0;i<10;i++) if (bits[i]=='1') pos++;

    int score = pos;

    if (bpm >= 100.f) score += 2;
    else if (bpm >= 90.f) score += 1;

    // pesos extras para risco imediato
    if (bits[8]=='1') score += 2;  // "evitando o grupo"
    if (bits[7]=='1') score += 2;  // "risco de crise agora"
    if (bits[9]=='1') score += 1;  // "quer falar com adulto"

    if (score >= 6) return STAT_COLOR_VERMELHO;
    if (score >= 3) return STAT_COLOR_AMARELO;
    return STAT_COLOR_VERDE;
}

int main(void) {
    stdio_init_all();
    sleep_ms(300);

    // OLED em I2C1
    i2c_setup(OLED_I2C, OLED_SDA, OLED_SCL, 400000);
    oled.external_vcc = false;
    oled_ok = ssd1306_init(&oled, 128, 64, OLED_ADDR, OLED_I2C);

    // Botões
    gpio_init(BUTTON_A); gpio_set_dir(BUTTON_A, GPIO_IN); gpio_pull_up(BUTTON_A);
    gpio_init(BUTTON_B); gpio_set_dir(BUTTON_B, GPIO_IN); gpio_pull_up(BUTTON_B);
    bool a_prev = false, b_prev = false;

    // AP / HTTP
    stats_init();
    web_ap_start();

    // Sensores
    bool cor_ready = false;
    bool oxi_inited = false;

    // Estado
    state_t st = ST_ASK, last_st = (state_t)-1;
    uint32_t now_ms = 0, t_last = 0, show_until_ms = 0;

    float bpm_final_buf = NAN;
    stat_color_t cor_recomendada = STAT_COLOR_VERDE;

    while (true) {
        now_ms = to_ms_since_boot(get_absolute_time());

        bool a_now = !gpio_get(BUTTON_A);
        bool b_now = !gpio_get(BUTTON_B);
        bool a_edge = edge_press(a_now, &a_prev);
        bool b_edge = edge_press(b_now, &b_prev);

        if (st != last_st) {
            switch (st) {
            case ST_ASK:
                oled_lines("Iniciar triagem?", "(A) Sim   (B) Nao", "", "");
                break;
            case ST_OXI_RUN:
                oled_lines("Oximetro", "Posicione o dedo", "(B) Voltar", "");
                break;
            case ST_SHOW_BPM:
                break;
            case ST_SURVEY_WAIT:
                oled_lines("Aguardando respostas", "Abra /display", "Celular ira abrir /survey", "");
                break;
            case ST_TRIAGE_RESULT: {
                char l2[26]; snprintf(l2, sizeof l2, "Pegue a pulseira");
                char l3[26]; snprintf(l3, sizeof l3, "%s", cor_nome(cor_recomendada));
                oled_lines("Recomendacao:", l2, l3, "Validaremos no sensor");
                show_until_ms = now_ms + 2000;
                break;
            }
            case ST_COLOR_INTRO:
                oled_lines("Validar pulseira", "Aproxime a pulseira", "no sensor de cor", "");
                show_until_ms = now_ms + 1200;
                break;
            case ST_COLOR_LOOP:
                break;
            case ST_SAVE_AND_DONE:
                break;
            case ST_REPORT:
                oled_lines("Relatorio", "A=voltar", "", "");
                break;
            default: break;
            }
            last_st = st;
        }

        switch (st) {
        case ST_ASK:
            if (a_edge) {
                if (!oxi_inited) {
                    bool ok = false;
                    for (int tries=0; tries<3 && !ok; tries++) {
                        i2c_setup(OXI_I2C, OXI_SDA, OXI_SCL, 100000);
                        ok = oxi_init(OXI_I2C, OXI_SDA, OXI_SCL);
                        if (!ok) sleep_ms(200);
                    }
                    oxi_inited = ok;
                }
                if (!oxi_inited) {
                    oled_lines("MAX3010x nao encontrado", "Verifique cabos", "Voltando...", "");
                    sleep_ms(1000);
                    break;
                }
                bpm_final_buf = NAN;
                oxi_start();
                t_last = now_ms;
                st = ST_OXI_RUN;
            } else if (b_edge) {
                // idle
            }
            break;

        case ST_OXI_RUN: {
            if (b_edge) { st = ST_ASK; break; }
            oxi_poll(now_ms);
            if (now_ms - t_last > 180) {
                t_last = now_ms;
                oxi_state_t s = oxi_get_state();
                if (s == OXI_WAIT_FINGER) {
                    oled_lines("Oximetro", "Posicione o dedo", "(B) Voltar", "");
                } else if (s == OXI_SETTLE) {
                    oled_lines("Oximetro", "Calibrando...", "Mantenha o dedo", "");
                } else if (s == OXI_RUN) {
                    int n, tgt; oxi_get_progress(&n, &tgt);
                    float live = oxi_get_bpm_live();
                    char l2[22], l3[22];
                    snprintf(l2, sizeof l2, "BPM~ %.1f", live);
                    snprintf(l3, sizeof l3, "Validas: %d/%d", n, tgt);
                    oled_lines("Medindo...", l2, l3, "(B) Voltar");
                } else if (s == OXI_DONE) {
                    bpm_final_buf = oxi_get_bpm_final();
                    char l2[22]; snprintf(l2, sizeof l2, "BPM FINAL: %.1f", bpm_final_buf);
                    oled_lines("Concluido!", l2, "", "");
                    show_until_ms = now_ms + 1200;
                    st = ST_SHOW_BPM;
                } else if (s == OXI_ERROR) {
                    oled_lines("ERRO oximetro", "Cheque conexoes", "", "");
                    sleep_ms(1200);
                    st = ST_ASK;
                }
            }
            break;
        }

        case ST_SHOW_BPM:
            if ((int32_t)(show_until_ms - now_ms) <= 0) {
                // Inicia SURVEY (no painel)
                web_survey_begin();
                oled_lines("Aguardando respostas", "No celular abrir", "/display (vai abrir /survey)", "");
                st = ST_SURVEY_WAIT;
            }
            break;

        case ST_SURVEY_WAIT: {
            char bits[11] = {0};
            if (web_take_survey(bits)) {
                float bpm_ok = isnan(bpm_final_buf) ? 80.f : bpm_final_buf;
                cor_recomendada = triage_from_survey(bpm_ok, bits);
                st = ST_TRIAGE_RESULT;
            }
            break;
        }

        case ST_TRIAGE_RESULT:
            if ((int32_t)(show_until_ms - now_ms) <= 0) {
                if (!cor_ready) {
                    i2c_setup(COL_I2C, COL_SDA, COL_SCL, 100000);
                    cor_ready = cor_init(COL_I2C, COL_SDA, COL_SCL);
                }
                if (!cor_ready) {
                    oled_lines("TCS34725 nao encontrado", "Pulando validacao", "", "");
                    sleep_ms(800);
                    stats_set_current_color((stat_color_t)STAT_COLOR_NONE);
                    st = ST_SAVE_AND_DONE;
                } else {
                    st = ST_COLOR_INTRO;
                }
            }
            break;

        case ST_COLOR_INTRO:
            if ((int32_t)(show_until_ms - now_ms) <= 0) {
                t_last = now_ms;
                st = ST_COLOR_LOOP;
            }
            break;

        case ST_COLOR_LOOP: {
            static bool baseline_ok=false; static uint32_t until=0; static float c0=0; static int c0n=0;
            if (!baseline_ok) {
                if (until==0) { until = now_ms + 700; c0=0; c0n=0; }
                float rf,gf,bf,cf;
                if (cor_read_rgb_norm(&rf,&gf,&bf,&cf)) { c0 += cf; c0n++; }
                oled_lines("Validar pulseira", "Aproxime no sensor", cor_nome(cor_recomendada), "Medindo ambiente...");
                if ((int32_t)(until - now_ms) <= 0 && c0n>=3) { c0/= (float)c0n; baseline_ok=true; }
            } else {
                float rf,gf,bf,cf;
                if (cor_read_rgb_norm(&rf,&gf,&bf,&cf)) {
                    float maxc = fmaxf(rf,fmaxf(gf,bf));
                    float minc = fminf(rf,fminf(gf,bf));
                    float chroma = maxc - minc;
                    float deltaC = (c0>1e-6f)? fabsf(cf-c0)/c0 : 1.f;
                    bool ok = (cf>0.06f) && (deltaC>0.25f) && (chroma>0.14f);
                    if (ok) {
                        cor_class_t cls = cor_classify(rf,gf,bf,cf);
                        stat_color_t sc;
                        bool m = true;
                        switch (cls) {
                            case COR_VERDE:    sc=STAT_COLOR_VERDE; break;
                            case COR_AMARELO:  sc=STAT_COLOR_AMARELO; break;
                            case COR_VERMELHO: sc=STAT_COLOR_VERMELHO; break;
                            default: m=false; break;
                        }
                        if (m && sc == cor_recomendada) {
                            char msg[26]; snprintf(msg,sizeof msg,"Pulseira %s OK",cor_nome(sc));
                            oled_lines(msg,"","", "");
                            show_until_ms = now_ms + 700;
                            stats_set_current_color(sc);
                            st = ST_SAVE_AND_DONE;
                        } else {
                            oled_lines("Pulseira incorreta", "Pegue a pulseira:", cor_nome(cor_recomendada), "");
                            sleep_ms(900);
                        }
                    } else {
                        oled_lines("Aproxime melhor", "", cor_nome(cor_recomendada), "");
                        sleep_ms(400);
                    }
                } else {
                    oled_lines("Sem leitura", "Tente novamente", "", "");
                    sleep_ms(400);
                }
            }
            break;
        }

        case ST_SAVE_AND_DONE:
            if ((int32_t)(show_until_ms - now_ms) <= 0) {
                stats_inc_color(cor_recomendada);
                if (!isnan(bpm_final_buf)) stats_add_bpm(bpm_final_buf);
                // (não gravamos energia/humor/ans agora)
                oled_lines("Registro concluido", "Obrigado!", "", "");
                sleep_ms(800);
                stats_set_current_color((stat_color_t)STAT_COLOR_NONE);
                // reseta baseline do loop de cor na próxima vez
                st = ST_ASK;
            }
            break;

        case ST_REPORT:
            if (a_edge) st = ST_ASK;
            break;

        default: break;
        }

        sleep_ms(10);
    }
}
