// Fluxo: BPM -> SURVEY no painel -> recomenda pulseira -> valida cor -> registra métricas

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

// Joystick (apenas botão para sair do relatório)
#define JOY_BTN     22

static ssd1306_t oled;
static bool oled_ok = false;

// --- Detecção robusta de cor ---
static bool     color_baseline_ready = false;
static uint32_t color_baseline_until = 0;
static float    c0_r=0.f, c0_g=0.f, c0_b=0.f, c0_c=0.f;
static uint32_t c0_n = 0;

#define C_MIN        0.06f
#define CHROMA_MIN   0.14f
#define DELTA_C_MIN  0.25f

static void i2c_setup(i2c_inst_t *i2c, uint sda, uint scl, uint hz) {
    i2c_init(i2c, hz);
    gpio_set_function(sda, GPIO_FUNC_I2C);
    gpio_set_function(scl, GPIO_FUNC_I2C);
    gpio_pull_up(sda);
    gpio_pull_up(scl);
}

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

static void joystick_init(void) {
    gpio_init(JOY_BTN);
    gpio_set_dir(JOY_BTN, GPIO_IN);
    gpio_pull_up(JOY_BTN);
}
typedef struct { bool btn_edge; } joy_events_t;
static joy_events_t joystick_poll(void) {
    static bool btn_prev=false;
    joy_events_t ev = {0};
    bool btn_now = !gpio_get(JOY_BTN);
    ev.btn_edge = edge_press(btn_now, &btn_prev);
    return ev;
}

typedef enum {
    ST_ASK = 0,
    ST_OXI_RUN,
    ST_SHOW_BPM,
    ST_SURVEY_WAIT,
    ST_TRIAGE_RESULT,
    ST_COLOR_INTRO,
    ST_COLOR_LOOP,
    ST_SAVE_AND_DONE,
    ST_REPORT
} state_t;

static const char* cor_nome(stat_color_t c) {
    switch (c) {
        case STAT_COLOR_VERDE: return "VERDE";
        case STAT_COLOR_AMARELO: return "AMARELO";
        case STAT_COLOR_VERMELHO: return "VERMELHO";
        default: return "?";
    }
}

static float        bpm_final_buf = NAN;
static stat_color_t cor_recomendada = STAT_COLOR_VERDE;

// >>> NEW: token da submissão a ser atribuída à cor após validação
static uint32_t survey_last_token = 0;
static uint32_t survey_token_to_assign = 0;

int main(void) {
    stdio_init_all();
    sleep_ms(300);

    i2c_setup(OLED_I2C, OLED_SDA, OLED_SCL, 400000);
    oled.external_vcc = false;
    oled_ok = ssd1306_init(&oled, 128, 64, OLED_ADDR, OLED_I2C);

    gpio_init(BUTTON_A); gpio_set_dir(BUTTON_A, GPIO_IN); gpio_pull_up(BUTTON_A);
    gpio_init(BUTTON_B); gpio_set_dir(BUTTON_B, GPIO_IN); gpio_pull_up(BUTTON_B);
    bool a_prev=false, b_prev=false;

    joystick_init();

    stats_init();
    web_ap_start();

    bool cor_ready = false;
    bool oxi_inited = false;

    state_t st = ST_ASK, last_st = (state_t)-1;
    uint32_t t_last = 0, show_until_ms = 0;

    while (true) {
        uint32_t now_ms = to_ms_since_boot(get_absolute_time());
        bool a_edge = edge_press(!gpio_get(BUTTON_A), &a_prev);
        bool b_edge = edge_press(!gpio_get(BUTTON_B), &b_prev);
        bool joy_btn_edge = joystick_poll().btn_edge;

        if (st != last_st) {
            switch (st) {
                case ST_ASK:
                    oled_lines("Iniciar triagem?", "(A) Sim   (B) Nao", "Botao Joy: Relatorio", "");
                    break;
                case ST_SURVEY_WAIT:
                    oled_lines("Aguardando envio", "Responda no celular", "[SURVEY]", "(B) Cancelar");
                    break;
                case ST_TRIAGE_RESULT: {
                    char l2[24]; snprintf(l2, sizeof l2, "Pegue a pulseira");
                    char l3[24]; snprintf(l3, sizeof l3, "%s", cor_nome(cor_recomendada));
                    oled_lines("Recomendacao:", l2, l3, "Validaremos no sensor");
                    show_until_ms = now_ms + 3000;
                    break;
                }
                case ST_COLOR_INTRO:
                    oled_lines("Validar pulseira", "Aproxime a pulseira", "no sensor", "");
                    show_until_ms = now_ms + 5000;
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
                    oled_lines("MAX3010x nao encontrado", "Verifique cabos", "Voltando ao menu", "");
                    sleep_ms(1200);
                    st = ST_ASK;
                    break;
                }
                bpm_final_buf = NAN;
                web_set_survey_mode(false);
                web_survey_reset();
                oxi_start();
                t_last = now_ms;
                st = ST_OXI_RUN;
            } else if (joy_btn_edge) {
                st = ST_REPORT;
                t_last = now_ms;
            }
            break;

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
                    int n,tgt; oxi_get_progress(&n,&tgt);
                    float live = oxi_get_bpm_live();
                    char l2[22], l3[22];
                    snprintf(l2, sizeof l2, "BPM~ %.1f", live);
                    snprintf(l3, sizeof l3, "Validas: %d/%d", n, tgt);
                    oled_lines("Medindo...", l2, l3, "(B) Voltar");
                } else if (s == OXI_DONE) {
                    bpm_final_buf = oxi_get_bpm_final();
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
        // Abre survey e só depois lê o último token
        web_survey_reset();
        web_set_survey_mode(true);

        uint32_t tok0 = 0;
        web_survey_peek(NULL, &tok0);   // memoriza token vigente (se houver)
        survey_last_token = tok0;

        oled_lines("Responda no painel", "Abrir /survey no celular", "[SURVEY]", "");
        st = ST_SURVEY_WAIT;
    }
    break;


        case ST_SURVEY_WAIT: {
    uint16_t bits = 0;
    uint32_t tok  = 0;
    bool has = web_survey_peek(&bits, &tok);

    // Só avança se existe submissão pendente E o token mudou (e não é 0)
    if (has && tok != 0 && tok != survey_last_token) {
        survey_last_token = tok;

        float bpm_ok = isnan(bpm_final_buf) ? 80.f : bpm_final_buf;

        /* Mapa das perguntas (ordem atual do /survey):
           0=Dor forte hoje?           (Sim=risco)
           1=Comeu nas ultimas horas?  (Sim=ok)
           2=Dormiu bem?               (Sim=ok)
           3=Fadiga forte agora?       (Sim=risco)
           4=Conflito forte?           (Sim=risco)
           5=Muito nervoso?            (Sim=risco)
           6=Dificuldade concentrar?   (Sim=risco)
           7=Risco de crise agora?     (Sim=risco)
           8=Evitando ficar com grupo? (Sim=risco)
           9=Quer falar com adulto?    (Sim=risco/atenção)
        */
        int risk = 0;
        if (bits & (1u<<0)) risk += 2;        // dor
        if (!(bits & (1u<<1))) risk += 1;     // não comeu/hidratou
        if (!(bits & (1u<<2))) risk += 1;     // não dormiu bem
        if (bits & (1u<<3)) risk += 1;        // fadiga
        if (bits & (1u<<4)) risk += 2;        // conflito
        if (bits & (1u<<5)) risk += 2;        // nervoso
        if (bits & (1u<<6)) risk += 1;        // concentração
        if (bits & (1u<<7)) risk += 3;        // crise
        if (bits & (1u<<8)) risk += 1;        // evitando grupo
        if (bits & (1u<<9)) risk += 3;        // quer falar

        int bpm_band = 0;
        if (bpm_ok >= 100.f) bpm_band = 2;
        else if (bpm_ok >= 85.f || bpm_ok < 55.f) bpm_band = 1;
        risk += bpm_band;

        if (risk >= 6)      cor_recomendada = STAT_COLOR_VERMELHO;
        else if (risk >= 3) cor_recomendada = STAT_COLOR_AMARELO;
        else                cor_recomendada = STAT_COLOR_VERDE;

        char l2[24]; snprintf(l2, sizeof l2, "Pegue a pulseira");
        char l3[24]; snprintf(l3, sizeof l3, "%s", cor_nome(cor_recomendada));
        oled_lines("Recomendacao:", l2, l3, "Validaremos no sensor");
        show_until_ms = now_ms + 3000;
        st = ST_TRIAGE_RESULT;
    } else {
        oled_lines("Aguardando envio", "Responda no celular", "[SURVEY]", "(B) Cancelar");
        if (b_edge) {
            web_set_survey_mode(false);
            web_survey_reset();
            st = ST_ASK;
        }
    }
    break;
}


        case ST_TRIAGE_RESULT:
            if ((int32_t)(show_until_ms - now_ms) <= 0 || a_edge) {
                static bool cor_ready_once=false;
                if (!cor_ready_once) {
                    i2c_setup(COL_I2C, COL_SDA, COL_SCL, 100000);
                    cor_ready_once = cor_init(COL_I2C, COL_SDA, COL_SCL);
                }
                if (!cor_ready_once) {
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

        case ST_COLOR_LOOP: {
            if (now_ms - t_last > 200) {
                t_last = now_ms;

                float rf,gf,bf,cf;
                bool have = cor_read_rgb_norm(&rf,&gf,&bf,&cf);

                if (!color_baseline_ready) {
                    if (have) { c0_r+=rf; c0_g+=gf; c0_b+=bf; c0_c+=cf; c0_n++; }
                    if ((int32_t)(color_baseline_until - now_ms) <= 0 && c0_n >= 3) {
                        c0_r/= (float)c0_n; c0_g/= (float)c0_n; c0_b/= (float)c0_n; c0_c/= (float)c0_n;
                        color_baseline_ready = true;
                    }
                    oled_lines("Validar pulseira", "Aproxime a pulseira", "no sensor", "Medindo ambiente...");
                } else {
                    if (have) {
                        float maxc=fmaxf(rf,fmaxf(gf,bf));
                        float minc=fminf(rf,fminf(gf,bf));
                        float chroma=maxc-minc;
                        float deltaC=(c0_c>1e-6f)? fabsf(cf-c0_c)/c0_c : 1.f;
                        bool luz_ok=(cf>C_MIN), mudou_ok=(deltaC>DELTA_C_MIN), chroma_ok=(chroma>CHROMA_MIN);
                        if (luz_ok && mudou_ok && chroma_ok) {
                            cor_class_t cls=cor_classify(rf,gf,bf,cf);
                            const char* nome=cor_class_to_str(cls);
                            char l4[24]; snprintf(l4,sizeof l4,"Lido: %s  A=OK", nome);
                            oled_lines("Validar pulseira","Aproxime e pressione A", l4, cor_nome(cor_recomendada));
                        } else {
                            oled_lines("Validar pulseira","Aproxime a pulseira","Leitura fraca...", cor_nome(cor_recomendada));
                        }
                    } else {
                        oled_lines("Validar pulseira","Aproxime a pulseira","Sem leitura", cor_nome(cor_recomendada));
                    }
                }
            }

            if (a_edge) {
                if (!color_baseline_ready) { oled_lines("Aguarde...","Medindo ambiente","",""); sleep_ms(600); break; }
                float rf,gf,bf,cf;
                if (cor_read_rgb_norm(&rf,&gf,&bf,&cf)) {
                    float maxc=fmaxf(rf,fmaxf(gf,bf));
                    float minc=fminf(rf,fminf(gf,bf));
                    float chroma=maxc-minc;
                    float deltaC=(c0_c>1e-6f)? fabsf(cf-c0_c)/c0_c : 1.f;
                    bool luz_ok=(cf>C_MIN), mudou_ok=(deltaC>DELTA_C_MIN), chroma_ok=(chroma>CHROMA_MIN);
                    if (luz_ok && mudou_ok && chroma_ok) {
                        cor_class_t cls=cor_classify(rf,gf,bf,cf);
                        stat_color_t sc; bool ok=true;
                        switch (cls) {
                            case COR_VERDE:    sc=STAT_COLOR_VERDE;    break;
                            case COR_AMARELO:  sc=STAT_COLOR_AMARELO;  break;
                            case COR_VERMELHO: sc=STAT_COLOR_VERMELHO; break;
                            default: ok=false; break;
                        }
                        if (ok && sc == cor_recomendada) {
                            char msg[26]; snprintf(msg, sizeof msg, "Pulseira %s ok!", cor_nome(sc));
                            oled_lines(msg, "", "", "");

                            // Vincula a submissão do survey à cor validada
                            if (survey_last_token != 0) {
                                web_assign_survey_token_to_color(survey_last_token, sc);
                            }

                            stats_set_current_color(sc);
                            st = ST_SAVE_AND_DONE;
                        } else {
                            oled_lines("Pulseira incorreta", "Pegue a pulseira:", cor_nome(cor_recomendada), "");
                            sleep_ms(1000);
                        }

                    } else {
                        oled_lines("Sem leitura","Aproxime melhor","","");
                        sleep_ms(700);
                    }
                } else {
                    oled_lines("Falha na leitura","Tente novamente","","");
                    sleep_ms(700);
                }
            }
            break;
        }

        case ST_SAVE_AND_DONE:
            stats_inc_color(cor_recomendada);
            if (!isnan(bpm_final_buf)) stats_add_bpm(bpm_final_buf);
            oled_lines("Registro concluido","Obrigado!","","");
            sleep_ms(900);
            stats_set_current_color((stat_color_t)STAT_COLOR_NONE);
            web_set_survey_mode(false);
            st = ST_ASK;
            break;

        case ST_REPORT: {
            if (now_ms - t_last > 1000) {
                t_last = now_ms;
                stats_snapshot_t s; stats_get_snapshot(&s);
                char l1[22], l2[22];
                float bpm = s.bpm_mean_trimmed;
                if (isnan(bpm)) snprintf(l1,sizeof l1,"BPM: --");
                else            snprintf(l1,sizeof l1,"BPM: %.1f (n=%lu)", bpm,(unsigned long)s.bpm_count);
                snprintf(l2,sizeof l2,"V:%lu A:%lu R:%lu",
                        (unsigned long)s.cor_verde,
                        (unsigned long)s.cor_amarelo,
                        (unsigned long)s.cor_vermelho);
                oled_lines("Relatorio Grupo", l1, l2, "Joy=sair");
            }
            if (joy_btn_edge) st = ST_ASK;
            break;
        }

        default: break;
        }

        sleep_ms(10);
    }
}
