// main.c — Fluxo: triagem -> recomenda pulseira -> valida cor -> registra métricas
// Publica dados via AP (DHCP/DNS/HTTP) para acesso no celular/tablet da profissional.
// A página /display apenas espelha as 4 linhas do OLED (ampliadas).

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
#include "src/stats.h"       // stats_init, stats_add_*, stats_set_current_color, stats_get_snapshot
#include "src/web_ap.h"      // web_ap_start + web_display_set_lines (espelho do OLED)

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

// --- Detecção robusta de cor: baseline (ambiente) + portas de validade ---
static bool     color_baseline_ready = false;
static uint32_t color_baseline_until = 0;
static float    c0_r=0.f, c0_g=0.f, c0_b=0.f, c0_c=0.f;
static uint32_t c0_n = 0;

// Limiares (ajuste fino conforme ambiente)
#define C_MIN        0.06f   // luz mínima absoluta (0..1)
#define CHROMA_MIN   0.14f   // diferença mínima entre canais (max-min)
#define DELTA_C_MIN  0.25f   // variação mínima de C vs ambiente (25%)

// ---------- I2C / OLED helpers ----------
static void i2c_setup(i2c_inst_t *i2c, uint sda, uint scl, uint hz) {
    i2c_init(i2c, hz);
    gpio_set_function(sda, GPIO_FUNC_I2C);
    gpio_set_function(scl, GPIO_FUNC_I2C);
    gpio_pull_up(sda);
    gpio_pull_up(scl);
}

// --- OLED + espelho web (/display)
static void oled_lines(const char *l1, const char *l2, const char *l3, const char *l4) {
    web_display_set_lines(l1, l2, l3, l4); // espelho /display
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
    ST_OXI_PREP,       // Inicializa/ativa oxímetro
    ST_OXI_RUN,        // Oxímetro rodando
    ST_SHOW_BPM,       // Mostra BPM final por ~1.5s
    ST_ENERGY_ASK,     // Pergunta energia 1..3 (joystick)
    ST_HUMOR_ASK,      // Pergunta humor 1..3 (joystick)
    ST_ANS_ASK,        // Pergunta ansiedade 1..3 (joystick)
    ST_TRIAGE_RESULT,  // Recomenda pulseira (verde/amarelo/vermelho)
    ST_COLOR_INTRO,    // Tela de instrução (5s) para validação da pulseira
    ST_COLOR_LOOP,     // Loop de leitura de cor para validar a pulseira
    ST_SAVE_AND_DONE,  // Confirma e salva tudo
    ST_ANS_SAVED,      // (compatibilidade)
    ST_REPORT          // Tela de relatório (botão do joystick para entrar/sair)
} state_t;

// --- rótulo textual dos níveis 1..3 ---
static const char* nivel_label(int v) {
    switch (v) { case 1: return "Baixa"; case 2: return "Media"; case 3: return "Alta"; default: return "?"; }
}

// --- classificação por triagem (retorna cor recomendada) ---
static stat_color_t triage_decide(float bpm, int energia, int humor, int ans) {
    int anx = ans - 1;            // 0..2 (maior = mais risco)
    int ene_def = 3 - energia;    // 0..2 (menor energia = mais risco)
    int hum_def = 3 - humor;      // 0..2 (pior humor = mais risco)
    int bpm_band = 0;             // repouso 55–85 ok, 86–99 alerta leve, >=100 alerta
    if (bpm >= 100.f) bpm_band = 2;
    else if (bpm >= 85.f || bpm < 55.f) bpm_band = 1;
    int risco = 2*anx + ene_def + hum_def + bpm_band;
    if (risco >= 5) return STAT_COLOR_VERMELHO;
    if (risco >= 3) return STAT_COLOR_AMARELO;
    return STAT_COLOR_VERDE;
}

// --- helpers de texto para a cor ---
static const char* cor_nome(stat_color_t c) {
    switch (c) {
        case STAT_COLOR_VERDE: return "VERDE";
        case STAT_COLOR_AMARELO: return "AMARELA";
        case STAT_COLOR_VERMELHO: return "VERMELHA";
        default: return "?";
    }
}

// ====== VARS DE SESSÃO (buffer até validar pulseira) ======
static float bpm_final_buf = NAN;
static int   energia_buf = 2;
static int   humor_buf   = 2;
static int   ans_buf     = 2;
static stat_color_t cor_recomendada = STAT_COLOR_VERDE;

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
    web_ap_start(); // Sobe AP TheraLink e HTTP (/ , /display, /stats.json, /download.csv)

    // Sensores (no I2C0 via extensor)
    bool cor_ready = false;
    bool oxi_inited = false;

    // Estado
    state_t st = ST_ASK;
    state_t last_st = (state_t)-1;
    uint32_t t_last = 0;
    uint32_t show_until_ms = 0; // para telas temporárias
    int energy_level = 2;       // 1..3
    int humor_level  = 2;       // 1..3
    int ans_level    = 2;       // 1..3

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
                    oled_lines("Iniciar triagem?", "(A) Sim   (B) Nao", "Botao Joy: Relatorio", "");
                    break;

                case ST_OXI_PREP:
                    oled_lines("Oximetro ativando...", "Posicione o dedo", "", "");
                    break;

                case ST_OXI_RUN:
                    break;

                case ST_SHOW_BPM:
                    break;

                case ST_ENERGY_ASK: {
                    char l2[24]; snprintf(l2, sizeof l2, "Nivel: %d (%s)", energy_level, nivel_label(energy_level));
                    oled_lines("Energia", l2, "Joy<- ->   A=OK", "");
                    break;
                }
                case ST_HUMOR_ASK: {
                    char l2[24]; snprintf(l2, sizeof l2, "Nivel: %d (%s)", humor_level, nivel_label(humor_level));
                    oled_lines("Humor", l2, "Joy<- ->   A=OK", "");
                    break;
                }
                case ST_ANS_ASK: {
                    char l2[24]; snprintf(l2, sizeof l2, "Nivel: %d (%s)", ans_level, nivel_label(ans_level));
                    oled_lines("Ansiedade", l2, "Joy<- ->   A=OK", "");
                    break;
                }

                case ST_TRIAGE_RESULT: {
                    char l2[24]; snprintf(l2, sizeof l2, "Pegue a pulseira");
                    char l3[24]; snprintf(l3, sizeof l3, "%s", cor_nome(cor_recomendada));
                    oled_lines("Recomendacao:", l2, l3, "Validaremos no sensor");
                    show_until_ms = now_ms + 3000; // ↑ 3s para dar tempo de ler
                    break;
                }

                case ST_COLOR_INTRO:
                    oled_lines("Validar pulseira", "Aproxime a pulseira", "no sensor", "");
                    show_until_ms = now_ms + 5000;  // timer claro ao ENTRAR no estado
                    break;

                case ST_COLOR_LOOP:
                    // desenhado dinamicamente abaixo
                    break;

                case ST_SAVE_AND_DONE:
                    break;

                case ST_REPORT:
                    break;

                default: break;
            }
            last_st = st;
        }

        switch (st) {
        // ======== PONTO DE PARTIDA ========
        case ST_ASK:
            if (a_edge) {
                // Prepara oximetro (com retry para evitar “não encontrado”)
                if (!oxi_inited) {
                    bool ok = false;
                    for (int tries = 0; tries < 3 && !ok; tries++) {
                        i2c_setup(OXI_I2C, OXI_SDA, OXI_SCL, 100000);
                        ok = oxi_init(OXI_I2C, OXI_SDA, OXI_SCL);
                        if (!ok) sleep_ms(200);
                    }
                    oxi_inited = ok;
                }
                if (!oxi_inited) {
                    oled_lines("MAX3010x nao encontrado", "Verifique cabos", "Voltando ao menu", "");
                    sleep_ms(1200);
                    st = ST_ASK;
                    break;
                }
                bpm_final_buf = NAN; // limpa buffer
                oxi_start();         // liga LED e zera filtros
                t_last = now_ms;
                st = ST_OXI_RUN;
            } else if (b_edge) {
                // “Nao” = fica ocioso nesta tela
            } else if (joy_btn_edge) {
                st = ST_REPORT;
                t_last = now_ms;
            }
            break;

        // ======== OXIMETRO ========
        case ST_OXI_RUN: {
            if (b_edge) {
                oled_lines("Oximetro cancelado", "Voltando ao menu...", "", "");
                sleep_ms(700);
                st = ST_ASK;
                break;
            }
            oxi_poll(now_ms);
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
                    bpm_final_buf = oxi_get_bpm_final(); // guarda (não envia ainda)
                    char l2[22]; snprintf(l2, sizeof l2, "BPM FINAL: %.1f", bpm_final_buf);
                    oled_lines("Concluido!", l2, "", "");
                    show_until_ms = now_ms + 1500;
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
                // Perguntas 1..3
                energy_level = 2;
                char l2[24]; snprintf(l2, sizeof l2, "Nivel: %d (%s)", energy_level, nivel_label(energy_level));
                oled_lines("Energia", l2, "Joy<- ->   A=OK", "");
                st = ST_ENERGY_ASK;
            }
            break;

        // ======== QUESTIONÁRIOS 1..3 ========
        case ST_ENERGY_ASK: {
            bool changed = false;
            if (jev.left_edge && energy_level > 1)  { energy_level--; changed = true; }
            if (jev.right_edge && energy_level < 3) { energy_level++; changed = true; }
            if (changed) {
                char l2[24]; snprintf(l2, sizeof l2, "Nivel: %d (%s)", energy_level, nivel_label(energy_level));
                oled_lines("Energia", l2, "Joy<- ->   A=OK", "");
            }
            if (a_edge) {
                energia_buf = energy_level;
                humor_level = 2;
                char l2[24]; snprintf(l2, sizeof l2, "Nivel: %d (%s)", humor_level, nivel_label(humor_level));
                oled_lines("Humor", l2, "Joy<- ->   A=OK", "");
                st = ST_HUMOR_ASK;
            }
            break;
        }

        case ST_HUMOR_ASK: {
            bool changed = false;
            if (jev.left_edge && humor_level > 1)  { humor_level--; changed = true; }
            if (jev.right_edge && humor_level < 3) { humor_level++; changed = true; }
            if (changed) {
                char l2[24]; snprintf(l2, sizeof l2, "Nivel: %d (%s)", humor_level, nivel_label(humor_level));
                oled_lines("Humor", l2, "Joy<- ->   A=OK", "");
            }
            if (a_edge) {
                humor_buf = humor_level;
                ans_level = 2;
                char l2[24]; snprintf(l2, sizeof l2, "Nivel: %d (%s)", ans_level, nivel_label(ans_level));
                oled_lines("Ansiedade", l2, "Joy<- ->   A=OK", "");
                st = ST_ANS_ASK;
            }
            break;
        }

        case ST_ANS_ASK: {
            bool changed = false;
            if (jev.left_edge && ans_level > 1)  { ans_level--; changed = true; }
            if (jev.right_edge && ans_level < 3) { ans_level++; changed = true; }
            if (changed) {
                char l2[24]; snprintf(l2, sizeof l2, "Nivel: %d (%s)", ans_level, nivel_label(ans_level));
                oled_lines("Ansiedade", l2, "Joy<- ->   A=OK", "");
            }
            if (a_edge) {
                ans_buf = ans_level;
                float bpm_ok = isnan(bpm_final_buf) ? 80.f : bpm_final_buf;
                cor_recomendada = triage_decide(bpm_ok, energia_buf, humor_buf, ans_buf);
                st = ST_TRIAGE_RESULT;
            }
            break;
        }

        // ======== MOSTRA RECOMENDAÇÃO E SEGUE PARA VALIDAÇÃO DA COR ========
        case ST_TRIAGE_RESULT:
            if ((int32_t)(show_until_ms - now_ms) <= 0 || a_edge) {
                if (!cor_ready) {
                    i2c_setup(COL_I2C, COL_SDA, COL_SCL, 100000);
                    cor_ready = cor_init(COL_I2C, COL_SDA, COL_SCL);
                }
                if (!cor_ready) {
                    oled_lines("TCS34725 nao encontrado", "Pulando validacao", "", "");
                    sleep_ms(900);
                    stats_set_current_color((stat_color_t)STAT_COLOR_NONE);
                    st = ST_SAVE_AND_DONE;
                } else {
                    color_baseline_ready = false;
                    color_baseline_until = now_ms + 800;
                    c0_r = c0_g = c0_b = c0_c = 0.f; c0_n = 0;
                    st = ST_COLOR_INTRO;
                }
            }
            break;

        case ST_COLOR_INTRO:
            if ((int32_t)(show_until_ms - now_ms) <= 0 || a_edge) {
                t_last = now_ms;
                st = ST_COLOR_LOOP;
            }
            break;

        // ======== LOOP DE VALIDAÇÃO DA PULSEIRA NO SENSOR DE COR ========
        case ST_COLOR_LOOP: {
            if (now_ms - t_last > 200) {
                t_last = now_ms;

                float rf, gf, bf, cf;
                bool have = cor_read_rgb_norm(&rf, &gf, &bf, &cf);

                if (!color_baseline_ready) {
                    if (have) { c0_r += rf; c0_g += gf; c0_b += bf; c0_c += cf; c0_n++; }
                    if ((int32_t)(color_baseline_until - now_ms) <= 0 && c0_n >= 3) {
                        c0_r /= (float)c0_n; c0_g /= (float)c0_n; c0_b /= (float)c0_n; c0_c /= (float)c0_n;
                        color_baseline_ready = true;
                    }
                    oled_lines("Validar pulseira", "Aproxime a pulseira", "no sensor", "Medindo ambiente...");
                } else {
                    const char *nome = "Sem leitura";
                    bool valid = false;
                    if (have) {
                        float maxc = fmaxf(rf, fmaxf(gf, bf));
                        float minc = fminf(rf, fminf(gf, bf));
                        float chroma = maxc - minc;
                        float deltaC = 0.f;
                        if (c0_c > 1e-6f) deltaC = fabsf(cf - c0_c) / c0_c;
                        bool luz_ok    = (cf > C_MIN);
                        bool mudou_ok  = (deltaC > DELTA_C_MIN);
                        bool chroma_ok = (chroma > CHROMA_MIN);
                        if (luz_ok && mudou_ok && chroma_ok) {
                            cor_class_t cls = cor_classify(rf, gf, bf, cf);
                            nome = cor_class_to_str(cls);
                            valid = true;
                            char l4[24];
                            snprintf(l4, sizeof l4, "Lido: %s  A=OK", nome);
                            oled_lines("Validar pulseira", "Aproxime e pressione A", l4, cor_nome(cor_recomendada));
                        } else {
                            oled_lines("Validar pulseira", "Aproxime a pulseira", "Leitura fraca...", cor_nome(cor_recomendada));
                        }
                    } else {
                        oled_lines("Validar pulseira", "Aproxime a pulseira", "Sem leitura", cor_nome(cor_recomendada));
                    }
                }
            }

            if (a_edge) {
                if (!color_baseline_ready) { oled_lines("Aguarde...", "Medindo ambiente", "", ""); sleep_ms(600); break; }
                float rf, gf, bf, cf;
                if (cor_read_rgb_norm(&rf, &gf, &bf, &cf)) {
                    float maxc = fmaxf(rf, fmaxf(gf, bf));
                    float minc = fminf(rf, fminf(gf, bf));
                    float chroma = maxc - minc;
                    float deltaC = (c0_c > 1e-6f) ? fabsf(cf - c0_c)/c0_c : 1.f;
                    bool luz_ok    = (cf > C_MIN);
                    bool mudou_ok  = (deltaC > DELTA_C_MIN);
                    bool chroma_ok = (chroma > CHROMA_MIN);
                    if (luz_ok && mudou_ok && chroma_ok) {
                        cor_class_t cls = cor_classify(rf, gf, bf, cf);
                        stat_color_t sc; bool ok = true;
                        switch (cls) {
                            case COR_VERDE:    sc = STAT_COLOR_VERDE;    break;
                            case COR_AMARELO:  sc = STAT_COLOR_AMARELO;  break;
                            case COR_VERMELHO: sc = STAT_COLOR_VERMELHO; break;
                            default: ok = false; break;
                        }
                        if (ok && sc == cor_recomendada) {
                            char msg[26]; snprintf(msg, sizeof msg, "Pulseira %s ok!", cor_nome(sc));
                            oled_lines(msg, "", "", "");
                            show_until_ms = now_ms + 900;
                            stats_set_current_color(sc); // define grupo corrente
                            st = ST_SAVE_AND_DONE;
                        } else {
                            oled_lines("Pulseira incorreta", "Pegue a pulseira:", cor_nome(cor_recomendada), "");
                            sleep_ms(1000);
                        }
                    } else {
                        oled_lines("Sem leitura", "Aproxime melhor", "", "");
                        sleep_ms(700);
                    }
                } else {
                    oled_lines("Falha na leitura", "Tente novamente", "", "");
                    sleep_ms(700);
                }
            }
            break;
        }

        // ======== SALVA TUDO (após validar cor) ========
        case ST_SAVE_AND_DONE:
            if ((int32_t)(show_until_ms - now_ms) <= 0) {
                stats_inc_color(cor_recomendada);
                if (!isnan(bpm_final_buf)) stats_add_bpm(bpm_final_buf);
                stats_add_energy((uint8_t)energia_buf);
                stats_add_humor((uint8_t)humor_buf);
                stats_add_anxiety((uint8_t)ans_buf);
                oled_lines("Registro concluido", "Obrigado!", "", "");
                sleep_ms(900);
                stats_set_current_color((stat_color_t)STAT_COLOR_NONE);
                st = ST_ASK;
            }
            break;

        // ======== RELATÓRIO ========
        case ST_REPORT: {
            if (now_ms - t_last > 1000) {
                t_last = now_ms;
                stats_snapshot_t snap; stats_get_snapshot(&snap);
                char l1[22], l2[22], l3[22];
                float bpm = snap.bpm_mean_trimmed;
                float ans = snap.ans_mean;
                if (isnan(bpm)) snprintf(l1, sizeof l1, "BPM: --");
                else            snprintf(l1, sizeof l1, "BPM: %.1f (n=%lu)", bpm, (unsigned long)snap.bpm_count);
                snprintf(l2, sizeof l2, "V:%lu A:%lu R:%lu",
                        (unsigned long)snap.cor_verde,
                        (unsigned long)snap.cor_amarelo,
                        (unsigned long)snap.cor_vermelho);
                if (isnan(ans)) snprintf(l3, sizeof l3, "Ans: --  Joy=sair");
                else            snprintf(l3, sizeof l3, "Ans: %.2f (n=%lu)", ans, (unsigned long)snap.ans_count);
                oled_lines("Relatorio Grupo", l1, l2, l3);
            }
            if (joy_btn_edge) st = ST_ASK;
            break;
        }

        default: break;
        } // switch(st)

        sleep_ms(10);
    }
}
