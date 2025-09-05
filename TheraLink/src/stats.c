#include "stats.h"
#include <string.h>
#include <math.h>
#include <stdio.h>

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

// CSV agregado para /download.csv
// Cabeçalho + linha com os valores atuais:
// bpm_mean,bpm_n,ans_mean,ans_n,cores_verde,cores_amarelo,cores_vermelho
size_t stats_dump_csv(char *dst, size_t maxlen) {
    if (!dst || maxlen == 0) return 0;

    stats_snapshot_t s;
    stats_get_snapshot(&s);

    // Se vier NaN, substitui por 0 para não imprimir "nan"
    double bpm_mean = isnan(s.bpm_mean_trimmed) ? 0.0 : s.bpm_mean_trimmed;
    double ans_mean = isnan(s.ans_mean)         ? 0.0 : s.ans_mean;

    size_t total = 0;

    // Cabeçalho
    int w = snprintf(dst + total, (total < maxlen) ? (maxlen - total) : 0,
                     "bpm_mean,bpm_n,ans_mean,ans_n,cores_verde,cores_amarelo,cores_vermelho\r\n");
    if (w < 0) return total;
    total += (size_t)((w > 0) ? w : 0);
    if (total >= maxlen) return maxlen;

    // Linha de dados
    w = snprintf(dst + total, (total < maxlen) ? (maxlen - total) : 0,
                 "%.3f,%lu,%.3f,%lu,%lu,%lu,%lu\r\n",
                 bpm_mean,  (unsigned long)s.bpm_count,
                 ans_mean,  (unsigned long)s.ans_count,
                 (unsigned long)s.cor_verde,
                 (unsigned long)s.cor_amarelo,
                 (unsigned long)s.cor_vermelho);
    if (w < 0) return total;
    total += (size_t)((w > 0) ? w : 0);

    if (total > maxlen) total = maxlen;
    return total;
}
