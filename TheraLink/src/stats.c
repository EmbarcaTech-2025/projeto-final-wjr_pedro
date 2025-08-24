#include "stats.h"
#include <string.h>
#include <math.h>

#define MAX_BPM_SAMPLES 64

static float    s_bpm_buf[MAX_BPM_SAMPLES];
static uint32_t s_bpm_n = 0;

static uint32_t s_cor[STAT_COLOR_COUNT] = {0};

static uint32_t s_ans_count = 0;
static uint32_t s_sample_id = 0;
static double   s_ans_sum   = 0.0;  // média simples

static float trimmed_mean_1(const float *v, uint32_t n) {
    if (n == 0) return NAN;
    if (n <= 2) {
        double s = 0;
        for (uint32_t i = 0; i < n; i++) s += v[i];
        return (float)(s / (double)n);
    }
    // ordena cópia e remove 1 de cada lado
    float a[MAX_BPM_SAMPLES];
    for (uint32_t i = 0; i < n; i++) a[i] = v[i];
    for (uint32_t i = 1; i < n; i++) {
        float x = a[i]; int j = i;
        while (j > 0 && a[j-1] > x) { a[j] = a[j-1]; j--; }
        a[j] = x;
    }
    double s = 0;
    for (uint32_t i = 1; i < n-1; i++) s += a[i];
    return (float)(s / (double)(n-2));
}

void stats_init(void) {
    memset(s_bpm_buf, 0, sizeof(s_bpm_buf));
    s_bpm_n = 0;
    memset(s_cor, 0, sizeof(s_cor));
    s_ans_count = 0;
    s_ans_sum   = 0.0;
    s_sample_id = 0;
}

void stats_add_bpm(float bpm) {
    if (!(bpm > 0.0f && bpm < 250.0f)) return; // descarta valores ruins
    if (s_bpm_n < MAX_BPM_SAMPLES) {
        s_bpm_buf[s_bpm_n++] = bpm;
    } else {
        // tira o mais antigo (desliza)
        memmove(&s_bpm_buf[0], &s_bpm_buf[1], (MAX_BPM_SAMPLES-1)*sizeof(float));
        s_bpm_buf[MAX_BPM_SAMPLES-1] = bpm;
    }
    s_sample_id++;
}

void stats_inc_color(stat_color_t c) {
    if ((unsigned)c < STAT_COLOR_COUNT) {
        s_cor[c]++;
        s_sample_id++;
    }
}

void stats_add_anxiety(uint8_t level) {
    if (level < 1 || level > 4) return;
    s_ans_sum   += (double)level;
    s_ans_count += 1;
    s_sample_id++;
}

void stats_get_snapshot(stats_snapshot_t *out) {
    if (!out) return;
    out->sample_id = s_sample_id;

    out->bpm_count = s_bpm_n;
    out->bpm_mean_trimmed = trimmed_mean_1(s_bpm_buf, s_bpm_n);

    out->cor_verde    = s_cor[STAT_COLOR_VERDE];
    out->cor_amarelo  = s_cor[STAT_COLOR_AMARELO];
    out->cor_vermelho = s_cor[STAT_COLOR_VERMELHO];

    out->ans_count = s_ans_count;
    out->ans_mean  = (s_ans_count ? (float)(s_ans_sum / (double)s_ans_count) : NAN);
}
