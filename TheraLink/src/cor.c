#include "cor.h"
#include <string.h>
#include <math.h>

// ---------- Estado interno ----------
static i2c_inst_t *s_i2c = NULL;
static uint8_t     s_addr = TCS34725_ADDR;

// Bits de comando do TCS34725
#define CMD_BIT     0x80
#define CMD_AUTOINC 0x20

// Registradores
#define REG_ENABLE   0x00
#define REG_ATIME    0x01
#define REG_CONTROL  0x0F
#define REG_ID       0x12
#define REG_CDATAL   0x14   // sequência: C, R, G, B (16b cada, little-endian)

static inline bool wr8(uint8_t reg, uint8_t val) {
    uint8_t b[2] = { (uint8_t)(CMD_BIT | reg), val };
    return i2c_write_blocking(s_i2c, s_addr, b, 2, false) == 2;
}
static inline bool rd(uint8_t reg, uint8_t *dst, size_t n) {
    uint8_t r = (uint8_t)(CMD_BIT | ((n>1) ? CMD_AUTOINC : 0) | reg);
    if (i2c_write_blocking(s_i2c, s_addr, &r, 1, true) != 1) return false;
    return i2c_read_blocking (s_i2c, s_addr, dst, n, false) == (int)n;
}

// ---------- API ----------
bool cor_init(i2c_inst_t *i2c, uint sda_pin, uint scl_pin)
{
    s_i2c = i2c;

    // I2C a 100 kHz (ou 400k se preferir)
    i2c_init(s_i2c, 100 * 1000);
    gpio_set_function(sda_pin, GPIO_FUNC_I2C);
    gpio_set_function(scl_pin, GPIO_FUNC_I2C);
    gpio_pull_up(sda_pin);
    gpio_pull_up(scl_pin);

    // Verifica ID do TCS34725 (datasheet: 0x44 ou 0x4D)
    uint8_t id = 0;
    if (!rd(REG_ID, &id, 1)) return false;
    if (!(id == 0x44 || id == 0x4D)) return false;

    // Tempo de integração ~100ms (ATIME = 0xD5 => 43 * 2.4ms ≈ 103ms)
    wr8(REG_ATIME, 0xD5);

    // Ganho 16x (CONTROL: 0x00=1x, 0x01=4x, 0x02=16x, 0x03=60x)
    wr8(REG_CONTROL, 0x02);

    // Liga: PON depois AEN
    wr8(REG_ENABLE, 0x01);         // PON
    sleep_ms(3);
    wr8(REG_ENABLE, 0x03);         // PON | AEN

    // Pequeno tempo para primeira conversão
    sleep_ms(5);
    return true;
}

bool cor_read_raw(uint16_t *clear, uint16_t *red, uint16_t *green, uint16_t *blue)
{
    if (!s_i2c) return false;
    uint8_t d[8];
    if (!rd(REG_CDATAL, d, 8)) return false;

    // little-endian
    uint16_t c = (uint16_t)d[0] | ((uint16_t)d[1] << 8);
    uint16_t r = (uint16_t)d[2] | ((uint16_t)d[3] << 8);
    uint16_t g = (uint16_t)d[4] | ((uint16_t)d[5] << 8);
    uint16_t b = (uint16_t)d[6] | ((uint16_t)d[7] << 8);

    if (clear) *clear = c;
    if (red)   *red   = r;
    if (green) *green = g;
    if (blue)  *blue  = b;
    return true;
}

bool cor_read_rgb_norm(float *r, float *g, float *b, float *c_norm)
{
    uint16_t c, rr, gg, bb;
    if (!cor_read_raw(&c, &rr, &gg, &bb)) return false;

    float cf = (float)c;
    if (cf < 1.0f) cf = 1.0f;                // evita divisão por zero

    if (r) *r = (float)rr / cf;
    if (g) *g = (float)gg / cf;
    if (b) *b = (float)bb / cf;
    if (c_norm) *c_norm = (float)c;
    return true;
}

// Heurística simples e estável para cartões de cor
cor_class_t cor_classify(float r, float g, float b, float c_norm)
{
    // --- “sem objeto” / escuro: sobe um pouco o limiar para evitar falso "verde"
    if (c_norm < 80.0f) return COR_PRETO;

    // clamp 0..1
    float rr = r; if (rr < 0) rr = 0; if (rr > 1) rr = 1;
    float gg = g; if (gg < 0) gg = 0; if (gg > 1) gg = 1;
    float bb = b; if (bb < 0) bb = 0; if (bb > 1) bb = 1;

    float maxv = fmaxf(rr, fmaxf(gg, bb));
    float minv = fminf(rr, fminf(gg, bb));
    float span = maxv - minv;

    // Branco: componentes altas e próximas + brilho razoável
    if (span < 0.10f && c_norm > 120.0f) return COR_BRANCO;

    // --- Amarelo primeiro: R e G altos, próximos; B baixo
    //     (não importa se G > R ou R > G)
    if (bb < 0.30f && rr > 0.38f && gg > 0.38f && fabsf(rr - gg) < 0.18f)
        return COR_AMARELO;

    // --- Decisão por razão para vermelho/verde (mais estável que "maior componente")
    float rg_ratio = (gg > 0.f) ? (rr / gg) : 99.f;
    float gr_ratio = (rr > 0.f) ? (gg / rr) : 99.f;

    if (rg_ratio > 1.35f && rr > 0.32f && bb < 0.45f)
        return COR_VERMELHO;

    if (gr_ratio > 1.35f && gg > 0.32f && bb < 0.45f)
        return COR_VERDE;

    // Azul (opcional, só pra testes/calibração)
    if (bb > rr && bb > gg && bb > 0.35f)
        return COR_AZUL;

    return COR_DESCONHECIDA;
}

const char *cor_class_to_str(cor_class_t c)
{
    switch (c) {
        case COR_VERMELHO:    return "Vermelho";
        case COR_VERDE:       return "Verde";
        case COR_AZUL:        return "Azul";
        case COR_AMARELO:     return "Amarelo";
        case COR_BRANCO:      return "Branco";
        case COR_PRETO:       return "Preto";
        case COR_DESCONHECIDA:
        default:              return "Desconhecida";
    }
}
