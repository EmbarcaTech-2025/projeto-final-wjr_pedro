#include "oximetro.h"
#include "hardware/i2c.h"
#include "pico/stdlib.h"
#include <string.h>

/* ===========================
   MAX30102 (real) + RUN sim
   ===========================*/

// ---------- I2C / MAX30102 ----------
#define MAX3010X_ADDR 0x57

// Registros principais
#define REG_INTR_STATUS_1 0x00
#define REG_INTR_STATUS_2 0x01
#define REG_INTR_ENABLE_1 0x02
#define REG_INTR_ENABLE_2 0x03
#define REG_FIFO_WR_PTR   0x04
#define REG_OVF_COUNTER   0x05
#define REG_FIFO_RD_PTR   0x06
#define REG_FIFO_DATA     0x07
#define REG_FIFO_CONFIG   0x08
#define REG_MODE_CONFIG   0x09
#define REG_SPO2_CONFIG   0x0A
#define REG_LED1_PA       0x0C   // LED1 = RED
#define REG_LED2_PA       0x0D   // LED2 = IR
#define REG_PART_ID       0xFF
#define REG_REV_ID        0xFE

// Bits/valores úteis
#define MODE_SHUTDOWN 0x80
#define MODE_RESET    0x40
#define MODE_SPO2     0x03   // usa RED+IR

// Parâmetros “reais”
#define RED_LED_CURRENT 0x24  // ~7–8mA
#define IR_LED_CURRENT  0x24
#define FIFO_SMP_AVG_4  (0x02 << 5)
#define FIFO_ROLLOVER_EN 0x10
#define FIFO_SMP_32     0x0F
#define SPO2_CFG_100HZ_18BIT 0x2F

#define IR_FINGER_THRESHOLD 20000u
#define SETTLE_REAL_MS      2500u  // tempo de estabilização

// Simulação (apenas no RUN)
#define DEMO_MIN_BPM   87.0f
#define DEMO_MAX_BPM   92.0f
#define DEMO_TARGET_BEATS 8
#define DEMO_STEP_MS   2000u

// Estado
static i2c_inst_t *s_i2c = NULL;
static uint8_t s_addr = MAX3010X_ADDR;
static oxi_state_t s_state = OXI_WAIT_FINGER;
static uint32_t s_t0_ms = 0;
static uint32_t s_last_step_ms = 0;
static int s_valid = 0;
static int s_target = DEMO_TARGET_BEATS;
static float s_live = 0.0f;
static float s_final = 0.0f;
static float s_sum = 0.0f;
static int s_cnt = 0;
static uint32_t s_rng = 0x1234ABCD; // PRNG p/ simulação

// ---------- Helpers I2C ----------
static inline bool wr1(uint8_t reg, uint8_t val) {
    uint8_t buf[2] = {reg, val};
    int r = i2c_write_blocking(s_i2c, s_addr, buf, 2, false);
    return (r == 2);
}
static inline bool rd(uint8_t reg, uint8_t *buf, size_t n) {
    int r = i2c_write_blocking(s_i2c, s_addr, &reg, 1, true);
    if (r != 1) return false;
    r = i2c_read_blocking(s_i2c, s_addr, buf, n, false);
    return (r == (int)n);
}
static inline bool rd_u8(uint8_t reg, uint8_t *val) {
    return rd(reg, val, 1);
}
static inline uint32_t read_sample_18b(void) {
    uint8_t b[3];
    if (!rd(REG_FIFO_DATA, b, 3)) return 0;
    uint32_t v = ((uint32_t)b[0] << 16) | ((uint32_t)b[1] << 8) | b[2];
    v &= 0x3FFFF;
    return v;
}

// ---------- PRNG / Sim bpm ----------
static inline float frand01(void) {
    s_rng = 1664525u * s_rng + 1013904223u;
    return (float)((s_rng >> 8) & 0xFFFFFF) / 16777215.0f;
}
static inline float next_bpm(float prev) {
    float r = DEMO_MIN_BPM + frand01() * (DEMO_MAX_BPM - DEMO_MIN_BPM);
    return (prev <= 0.0f) ? r : (prev * 0.70f + r * 0.30f);
}

// ---------- MAX30102 setup ----------
static bool max3010x_reset(void) {
    if (!wr1(REG_MODE_CONFIG, MODE_RESET)) return false;
    sleep_ms(10);
    uint8_t dummy;
    rd_u8(REG_INTR_STATUS_1, &dummy);
    rd_u8(REG_INTR_STATUS_2, &dummy);
    return true;
}

static bool max3010x_config_spo2(void) {
    if (!wr1(REG_MODE_CONFIG, MODE_SHUTDOWN)) return false;
    wr1(REG_FIFO_WR_PTR, 0);
    wr1(REG_OVF_COUNTER, 0);
    wr1(REG_FIFO_RD_PTR, 0);
    if (!wr1(REG_FIFO_CONFIG, FIFO_SMP_AVG_4 | FIFO_ROLLOVER_EN | FIFO_SMP_32)) return false;
    if (!wr1(REG_SPO2_CONFIG, SPO2_CFG_100HZ_18BIT)) return false;
    if (!wr1(REG_LED1_PA, RED_LED_CURRENT)) return false;
    if (!wr1(REG_LED2_PA, IR_LED_CURRENT))  return false;
    if (!wr1(REG_MODE_CONFIG, MODE_SPO2))   return false;
    uint8_t d;
    rd_u8(REG_INTR_STATUS_1, &d);
    rd_u8(REG_INTR_STATUS_2, &d);
    return true;
}

// Mede “dedo presente”
static bool finger_present_check(uint16_t n_samples) {
    uint32_t acc = 0;
    uint16_t n_ok = 0;
    for (int i = 0; i < n_samples; i++) {
        uint32_t v = read_sample_18b();
        if (v > 0) { acc += v; n_ok++; }
    }
    if (!n_ok) return false;
    uint32_t mean = acc / n_ok;
    return (mean >= IR_FINGER_THRESHOLD);
}

/* ========== API ========== */

bool oxi_init(i2c_inst_t *i2c, uint sda, uint scl) {
    (void)sda; (void)scl;
    s_i2c = i2c;
    uint8_t pid = 0;
    if (!rd_u8(REG_PART_ID, &pid)) return false;
    if (!(pid >= 0x11 && pid <= 0x15)) {
        // aceita faixa comum de IDs, mesmo se clone
    }
    if (!max3010x_reset())       return false;
    if (!max3010x_config_spo2()) return false;
    return true;
}

void oxi_start(void) {
    max3010x_config_spo2();
    s_state = OXI_WAIT_FINGER;
    s_t0_ms = 0;
    s_last_step_ms = 0;
    s_valid = 0;
    s_target = DEMO_TARGET_BEATS;
    s_live  = 0.0f;
    s_sum   = 0.0f;
    s_cnt   = 0;
    s_final = 0.0f;
}

void oxi_poll(uint32_t now_ms) {
    if (s_t0_ms == 0) s_t0_ms = now_ms;
    switch (s_state) {
    case OXI_WAIT_FINGER:
        if (finger_present_check(10)) {
            s_state = OXI_SETTLE;
            s_t0_ms = now_ms;
        }
        break;
    case OXI_SETTLE:
        (void)finger_present_check(12);
        if (!finger_present_check(6)) {
            s_state = OXI_WAIT_FINGER;
            s_t0_ms = now_ms;
            break;
        }
        if (now_ms - s_t0_ms >= SETTLE_REAL_MS) {
            s_state = OXI_RUN;
            s_t0_ms = now_ms;
            s_last_step_ms = now_ms;
            s_valid = 0;
            s_live  = 0.0f;
            s_sum   = 0.0f;
            s_cnt   = 0;
        }
        break;
    case OXI_RUN:
        if (!finger_present_check(6)) {
            s_state = OXI_WAIT_FINGER;
            s_t0_ms = now_ms;
            s_valid = 0;
            s_sum   = 0.0f;
            s_cnt   = 0;
            s_live  = 0.0f;
            break;
        }
        if (now_ms - s_last_step_ms >= DEMO_STEP_MS) {
            s_last_step_ms += DEMO_STEP_MS;
            s_valid++;
            s_live = next_bpm(s_live);
            s_sum += s_live;
            s_cnt++;
            if (s_valid >= s_target) {
                s_final = (s_cnt > 0) ? (s_sum / (float)s_cnt) : s_live;
                wr1(REG_MODE_CONFIG, MODE_SHUTDOWN);
                s_state = OXI_DONE;
            }
        }
        break;
    case OXI_DONE:
    case OXI_ERROR:
    default:
        break;
    }
}

oxi_state_t oxi_get_state(void)  { return s_state; }
float oxi_get_bpm_live(void)     { return s_live;  }
float oxi_get_bpm_final(void)    { return s_final; }

void oxi_get_progress(int *n, int *target) {
    if (n)      *n      = s_valid;
    if (target) *target = s_target;
}
