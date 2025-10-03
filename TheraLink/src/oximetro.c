#include "oximetro.h"
#include <string.h>
#include <math.h>
#include "hardware/i2c.h"





// ================= I2C / endereço =================
#define I2C_ADDR 0x57

// ================= Config de aquisição =================
// MAX30102: 400 Hz, avg=8 => ~50 Hz efetivo
#define SR_SENSOR_HZ        400
#define AVG_SAMPLES         8
#define FS_HZ               (SR_SENSOR_HZ / AVG_SAMPLES)     // 50 Hz (inteiro)
#define SAMPLE_PERIOD_MS    (1000 / FS_HZ)                   // 20 ms

// ================= Gate de dedo =================
#define FINGER_IR_MIN_30100   6000.0f
#define FINGER_IR_MIN_30102   12000.0f
#define FINGER_ON_HOLD_MS     250
#define FINGER_OFF_HOLD_MS    300

// ================= Janela / filtros =================
#define SMOOTH_N              7    // média móvel curta (suave, sem comer picos)
#define AC_WIN_SEC            6    // 6 s de janela p/ autocorrelação
#define AC_SAMPLES            (FS_HZ * AC_WIN_SEC)           // 300 @50 Hz
#define AC_RECOMP_MS          1000                           // recalcula a cada ~1 s

// Banda de BPM e conversão p/ lags
#define BPM_MIN               40.0f
#define BPM_MAX               180.0f
#define LAG_MIN               (FS_HZ * 60 / (int)BPM_MAX)    // ~17 @50 Hz
#define LAG_MAX               (FS_HZ * 60 / (int)BPM_MIN)    // ~75 @50 Hz

// Qualidade e aceitação
#define Q_MIN                 0.30f   // Rmax/R0 mínimo p/ aceitar
#define BAND_TOL_FRAC         0.10f   // ±10% banda p/ estabilidade
#define FINAL_GOOD_EST        4       // precisa de 4 estimativas boas

// Timeout / settle
#define SETTLE_SAMPLES        ((FS_HZ * 8) / 10)   // ~0.8 s
#define TIMEOUT_MS            20000

// Corrente LED (MAX30102). Ajuste se saturar ou faltar SNR.
#define LED_CURR              0x5F   // ~19–25 mA

// ====== I2C helpers ======
static i2c_inst_t *g_i2c = NULL;
static uint g_sda=0, g_scl=0;

static inline bool w8(uint8_t r, uint8_t v){
    uint8_t b[2]={r,v};
    int w = i2c_write_timeout_us(g_i2c, I2C_ADDR, b, 2, false, 2000);
    return (w == 2);
}
static inline bool rn(uint8_t r, uint8_t *d, size_t n){
    int w = i2c_write_timeout_us(g_i2c, I2C_ADDR, &r, 1, true, 2000);
    if(w < 0) return false;
    int rr = i2c_read_timeout_us(g_i2c, I2C_ADDR, d, n, false, 2000);
    return (rr == (int)n);
}

// ====== MAX30100 ======
static bool max30100_init(void){
    bool ok=true;
    ok &= w8(0x06,0x40); sleep_ms(10);                 // reset
    ok &= w8(0x02,0x00); ok &= w8(0x03,0x00); ok &= w8(0x04,0x00);
    ok &= w8(0x07,(1u<<6)|(0b011<<2)|0b11);            // SPO2: 100Hz, 16-bit
    ok &= w8(0x09,0x24); ok &= w8(0x0A,0x24);          // ~8–10 mA
    ok &= w8(0x06,0x03);                               // SPO2 mode
    uint8_t m=0; ok &= rn(0x06,&m,1);
    return ok && ((m&0x07)==0x03);








}
static bool max30100_read(uint16_t *ir, uint16_t *red){
    uint8_t d[4];
    if(!rn(0x05,d,4)) return false;
    *ir =(uint16_t)((d[0]<<8)|d[1]);
    *red=(uint16_t)((d[2]<<8)|d[3]);


















    return true;
}

// ====== MAX30102 ======
static bool max30102_init(void){
    bool ok=true;
    ok &= w8(0x09,0x40); sleep_ms(10);                                 // reset
    ok &= w8(0x08,(0b011<<5)|(1<<4)|0x00);                              // AVG=8, rollover
    ok &= w8(0x0A,(0b11<<5)|(0b011<<2)|0b11);                           // 16384nA, 400Hz, 411us
    ok &= w8(0x0C,LED_CURR);                                           // RED
    ok &= w8(0x0D,LED_CURR);                                           // IR
    ok &= w8(0x11,(0x01)|(0x02<<4));                                   // slots: RED, IR
    ok &= w8(0x12,0x00);
    ok &= w8(0x04,0x00); ok &= w8(0x05,0x00); ok &= w8(0x06,0x00);     // FIFO ptrs
    ok &= w8(0x09,0x03);                                               // SPO2 mode
    uint8_t m=0; ok &= rn(0x09,&m,1);
    return ok && ((m&0x07)==0x03);
}
static bool max30102_read(uint32_t *ir, uint32_t *red){
    uint8_t d[6];
    if(!rn(0x07,d,6)) return false;
    *red = (((uint32_t)d[0]<<16)|((uint32_t)d[1]<<8)|d[2]) & 0x3FFFF;
    *ir  = (((uint32_t)d[3]<<16)|((uint32_t)d[4]<<8)|d[5]) & 0x3FFFF;
    return true;

}

// ====== Estado/variáveis ======
static oxi_state_t g_state = OXI_IDLE;
static bool g_is30102=false, g_inited=false;

static uint32_t sample_last_ms=0;
static uint32_t settle_done_ms=0;

static float bpm_live=0.0f, bpm_final=NAN;
static int   good_estimates=0;

// finger debounce
static bool finger_on=false;
static uint32_t finger_on_ms=0, finger_off_ms=0;

// escolha de canal
typedef enum { CH_IR=0, CH_RED=1 } chan_t;
static chan_t use_ch = CH_IR;

// suavização curta
static float smooth_q[SMOOTH_N];
static int   smooth_n=0, smooth_head=0;
static float smooth_sum=0;

// buffer de autocorrelação (6 s)
static float ac_buf[AC_SAMPLES];
static int   ac_n=0, ac_head=0;
static uint32_t ac_last_ms=0;

// histórico de estimativas p/ final
#define EST_BUF 8
static float bpm_hist[EST_BUF];
static int   est_n=0;

// ====== helpers ======
static inline float finger_gate_min(void){
    return g_is30102 ? FINGER_IR_MIN_30102 : FINGER_IR_MIN_30100;
}
static inline float smooth_push(float x){
    if(smooth_n<SMOOTH_N){ smooth_q[smooth_head]=x; smooth_sum+=x; smooth_head=(smooth_head+1)%SMOOTH_N; smooth_n++; }
    else { smooth_sum -= smooth_q[smooth_head]; smooth_q[smooth_head]=x; smooth_sum += x; smooth_head=(smooth_head+1)%SMOOTH_N; }
    return smooth_sum / (float)smooth_n;
}
static inline void ac_push(float y){
    if(ac_n<AC_SAMPLES){ ac_buf[ac_head]=y; ac_head=(ac_head+1)%AC_SAMPLES; ac_n++; }
    else { ac_buf[ac_head]=y; ac_head=(ac_head+1)%AC_SAMPLES; }
}
static void reset_buffers(void){
    smooth_n=0; smooth_head=0; smooth_sum=0;
    ac_n=0; ac_head=0;
    est_n=0; good_estimates=0;
    bpm_live=0.0f; bpm_final=NAN;
}

// calcula média da janela
static void ac_copy_demean(float *dst){
    // copia em ordem temporal e remove média
    double s=0;
    for(int i=0;i<ac_n;i++){
        int idx = (ac_head - ac_n + i + AC_SAMPLES) % AC_SAMPLES;
        float v = ac_buf[idx];
        dst[i] = v;
        s += v;
    }
    float mean = (float)(s / (double)ac_n);
    for(int i=0;i<ac_n;i++) dst[i] -= mean;

}

// autocorrelação normalizada na banda de lags
static bool ac_estimate_bpm(float *out_bpm, float *out_q){
    if(ac_n < AC_SAMPLES) return false; // precisa janela cheia p/ estabilidade

    // cópia de trabalho
    static float x[AC_SAMPLES];
    ac_copy_demean(x);

    // energia no zero-lag (R[0])
    double r0=0.0;
    for(int i=0;i<AC_SAMPLES;i++){ r0 += (double)x[i]*(double)x[i]; }
    if(r0 <= 1e-6) return false;

    int best_k = 0;
    double best_r = -1e30;

    // busca pelo pico em k in [LAG_MIN..LAG_MAX]
    for(int k=LAG_MIN; k<=LAG_MAX; k++){
        double rk=0.0;
        int n = AC_SAMPLES - k;
        for(int i=0;i<n; i++){
            rk += (double)x[i]*(double)x[i+k];
        }
        // normaliza por R0 e por n (mantém escala comparável)
        rk /= r0;
        if(rk > best_r){
            best_r = rk;
            best_k = k;
        }
    }

    // interpolação parabólica p/ subamostra (melhora ~1–2 bpm)
    if(best_k> LAG_MIN && best_k< LAG_MAX){
        double rkm1=0.0, rkp1=0.0, rkk=0.0;
        int n0 = AC_SAMPLES - (best_k-1);
        for(int i=0;i<n0;i++) rkm1 += (double)x[i]*(double)x[i+best_k-1];
        int n1 = AC_SAMPLES - best_k;
        for(int i=0;i<n1;i++) rkk  += (double)x[i]*(double)x[i+best_k];
        int n2 = AC_SAMPLES - (best_k+1);
        for(int i=0;i<n2;i++) rkp1 += (double)x[i]*(double)x[i+best_k+1];
        rkm1/=r0; rkk/=r0; rkp1/=r0;
        double denom = (rkm1 - 2.0*rkk + rkp1);
        double delta = 0.0;
        if(fabs(denom) > 1e-9) delta = 0.5*(rkm1 - rkp1)/denom; // -b/2a
        double k_refined = (double)best_k + delta;
        // limita delta a [-1,1]
        if(delta < -1.0) k_refined = (double)best_k - 1.0;
        if(delta >  1.0) k_refined = (double)best_k + 1.0;

        // BPM = 60*Fs/k
        float bpm = (float)(60.0 * (double)FS_HZ / k_refined);
        *out_bpm = bpm;
        *out_q   = (float)rkk; // qualidade ~ correlação no pico
    } else {
        float bpm = (float)(60.0f * (float)FS_HZ / (float)best_k);
        *out_bpm = bpm;
        *out_q   = (float)best_r;
    }





    return true;
}

// ====== Leitura sample ======
static bool read_sample(float *ir_f, float *red_f){
    if(!g_inited) return false;
    if(g_is30102){
        uint32_t ir32, rd32; if(!max30102_read(&ir32, &rd32)) return false;
        *ir_f = (float)ir32; *red_f = (float)rd32;
    } else {
        uint16_t ir16, rd16; if(!max30100_read(&ir16, &rd16)) return false;
        *ir_f = (float)ir16; *red_f = (float)rd16;
    }
    return true;
}

// ====== API ======
bool oxi_init(i2c_inst_t *i2c, uint sda_pin, uint scl_pin){
    g_i2c=i2c; g_sda=sda_pin; g_scl=scl_pin;



    i2c_init(g_i2c, 100000);
    gpio_set_function(g_sda, GPIO_FUNC_I2C);
    gpio_set_function(g_scl, GPIO_FUNC_I2C);
    gpio_pull_up(g_sda); gpio_pull_up(g_scl);

    uint8_t tmp=0;
    if(!rn(0x00,&tmp,1) && !rn(0x01,&tmp,1)) { g_inited=false; g_state=OXI_ERROR; return false; }

    uint8_t part=0; bool ok_part = rn(0xFF,&part,1);
    g_is30102 = ok_part && (part==0x15);


    bool init_ok = g_is30102 ? max30102_init() : max30100_init();
    if(!init_ok){ g_inited=false; g_state=OXI_ERROR; return false; }





    g_inited=true; g_state=OXI_IDLE;

    return true;
}

void oxi_start(void){
    if(!g_inited){ g_state=OXI_ERROR; return; }
    if(g_is30102) max30102_init(); else max30100_init();

    sample_last_ms=0; settle_done_ms=0;
    finger_on=false; finger_on_ms=0; finger_off_ms=0;
    use_ch = CH_IR;
    reset_buffers();
    g_state=OXI_WAIT_FINGER;
}

void oxi_abort(void){ g_state=OXI_IDLE; }



void oxi_poll(uint32_t now_ms){
    if(g_state==OXI_IDLE || g_state==OXI_ERROR || g_state==OXI_DONE) return;
    if(now_ms - sample_last_ms < SAMPLE_PERIOD_MS) return;
    sample_last_ms = now_ms;

    float ir=0, red=0;
    if(!read_sample(&ir, &red)) return;




    // finger gate no IR cru
    float gate = finger_gate_min();
    if(ir > gate){
        if(!finger_on){
            if(finger_on_ms==0) finger_on_ms=now_ms;
            if(now_ms - finger_on_ms >= FINGER_ON_HOLD_MS) { finger_on=true; finger_off_ms=0; }
        }
    }else{
        finger_on_ms=0;
        if(finger_on){
            if(finger_off_ms==0) finger_off_ms=now_ms;
            if(now_ms - finger_off_ms >= FINGER_OFF_HOLD_MS){
                finger_on=false;
                g_state = OXI_WAIT_FINGER;
                reset_buffers();
                return;
            }
        }
    }

    switch(g_state){
    case OXI_WAIT_FINGER:
        if(finger_on){
            g_state=OXI_SETTLE;
            reset_buffers();

        }
        break;


    case OXI_SETTLE: {
        static int sc=0;
        static double s_ir=0, s2_ir=0, s_rd=0, s2_rd=0;

        s_ir  += ir; s2_ir += (double)ir*(double)ir;
        s_rd  += red; s2_rd += (double)red*(double)red;
        sc++;

        if(sc >= SETTLE_SAMPLES){
            double m_ir = s_ir / sc, var_ir = fmax(1.0, (s2_ir/sc) - m_ir*m_ir);
            double m_rd = s_rd / sc, var_rd = fmax(1.0, (s2_rd/sc) - m_rd*m_rd);
            use_ch = (var_ir >= var_rd) ? CH_IR : CH_RED;

            sc=0; s_ir=s2_ir=s_rd=s2_rd=0;
            settle_done_ms=now_ms;
            g_state=OXI_RUN;

        }
        break;
    }

    case OXI_RUN: {
        float raw = (use_ch==CH_IR) ? ir : red;
        float y = smooth_push(raw); // suaviza alto-freq

        // enche janela de autocorrelação (6s)
        ac_push(y);

        // recalcula ~1x/s quando a janela está cheia
        if(ac_n == AC_SAMPLES && (now_ms - ac_last_ms) >= AC_RECOMP_MS){
            ac_last_ms = now_ms;
            float est_bpm=0, q=0;
            if(ac_estimate_bpm(&est_bpm, &q)){
                // valida banda e qualidade
                if(est_bpm>=BPM_MIN && est_bpm<=BPM_MAX && q>=Q_MIN){
                    // suaviza BPM live (EMA)
                    if(bpm_live<=0) bpm_live=est_bpm;
                    else bpm_live = 0.7f*bpm_live + 0.3f*est_bpm;

                    // guarda no histórico p/ final
                    if(est_n<EST_BUF) bpm_hist[est_n++]=est_bpm;
                    else { for(int i=1;i<EST_BUF;i++) bpm_hist[i-1]=bpm_hist[i]; bpm_hist[EST_BUF-1]=est_bpm; }

                    // checa estabilidade: últimas 4 dentro de ±10% do mediano
                    if(est_n>=4){
                        // calcula mediana
                        float tmp[EST_BUF];
                        for(int i=0;i<est_n;i++) tmp[i]=bpm_hist[i];
                        for(int i=1;i<est_n;i++){ float x=tmp[i]; int j=i; while(j>0 && tmp[j-1]>x){tmp[j]=tmp[j-1]; j--; } tmp[j]=x; }
                        float med = (est_n&1)? tmp[est_n/2]: 0.5f*(tmp[est_n/2-1]+tmp[est_n/2]);

                        int ok=0;
                        for(int i=est_n-4;i<est_n;i++){
                            if(i<0) continue;
                            float dev = fabsf(bpm_hist[i]-med)/fmaxf(1.0f, med);
                            if(dev <= BAND_TOL_FRAC) ok++;
                        }
                        if(ok>=4) good_estimates++;

                        if(good_estimates >= FINAL_GOOD_EST){
                            // média aparada das últimas estimativas
                            int n = est_n<6? est_n: 6; // usa até 6 mais recentes
                            float arr[6];
                            for(int i=0;i<n;i++) arr[i]=bpm_hist[est_n-n+i];
                            // ordena
                            for(int i=1;i<n;i++){ float x=arr[i]; int j=i; while(j>0 && arr[j-1]>x){arr[j]=arr[j-1]; j--; } arr[j]=x; }
                            int s= (n>=4)? 1: 0, e=(n>=4)? n-1: n; // corta 1 em cada ponta se der
                            double acc=0; for(int i=s;i<e;i++) acc+=arr[i];
                            bpm_final = (float)(acc / (e-s));
                            g_state = OXI_DONE;
                        }
                    }
                }
            }

            // timeout p/ não travar
            if((now_ms - settle_done_ms) > TIMEOUT_MS && g_state==OXI_RUN){
                if(est_n>=3){
                    // fallback: média simples das estimativas
                    double acc=0; for(int i=0;i<est_n;i++) acc+=bpm_hist[i];
                    bpm_final = (float)(acc/est_n);
                    g_state=OXI_DONE;
                }else{
                    g_state=OXI_WAIT_FINGER;
                    reset_buffers();
                }



            }
        }
        break;
    }

    default: break;



    }
}

oxi_state_t oxi_get_state(void){ return g_state; }



void oxi_get_progress(int *valid_count, int *target_valid){
    if(valid_count)  *valid_count  = (good_estimates>FINAL_GOOD_EST? FINAL_GOOD_EST: good_estimates);
    if(target_valid) *target_valid = FINAL_GOOD_EST;
}
float oxi_get_bpm_live(void){ return bpm_live; }
float oxi_get_bpm_final(void){ return bpm_final; }