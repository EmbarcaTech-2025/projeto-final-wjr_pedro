#include "stats.h"
#include <string.h>
#include <math.h>
#include <stdio.h>

#define MAX_BPM_SAMPLES 64

// --------- Globais (gerais) ----------
static float    s_bpm_buf[MAX_BPM_SAMPLES];
static uint32_t s_bpm_n = 0;

static uint32_t s_cor[STAT_COLOR_COUNT] = {0};

static uint32_t s_ans_count = 0;
static double   s_ans_sum   = 0.0;

static uint32_t s_energy_count = 0;
static double   s_energy_sum   = 0.0;

static uint32_t s_humor_count = 0;
static double   s_humor_sum   = 0.0;

static uint32_t s_sample_id = 0;

// --------- Por cor ----------
static float    s_bpm_c[STAT_COLOR_COUNT][MAX_BPM_SAMPLES];
static uint32_t s_bpm_n_c[STAT_COLOR_COUNT] = {0};

static uint32_t s_ans_count_c[STAT_COLOR_COUNT]    = {0};
static double   s_ans_sum_c[STAT_COLOR_COUNT]      = {0.0, 0.0, 0.0};

static uint32_t s_energy_count_c[STAT_COLOR_COUNT] = {0};
static double   s_energy_sum_c[STAT_COLOR_COUNT]   = {0.0, 0.0, 0.0};

static uint32_t s_humor_count_c[STAT_COLOR_COUNT]  = {0};
static double   s_humor_sum_c[STAT_COLOR_COUNT]    = {0.0, 0.0, 0.0};

// Cor “corrente” do ciclo (definida quando captura pulseira)
static stat_color_t s_current_color = (stat_color_t)STAT_COLOR_NONE;

// --------- Helpers ----------
static float trimmed_mean_1(const float *v, uint32_t n) {
    if (n == 0) return NAN;
    if (n <= 2) {
        double s = 0;
        for (uint32_t i = 0; i < n; i++) s += v[i];
        return (float)(s / (double)n);
    }
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

static void push_bpm_buf(float *buf, uint32_t *pn, float bpm) {
    uint32_t n = *pn;
    if (n < MAX_BPM_SAMPLES) {
        buf[n] = bpm;
        *pn = n + 1;
    } else {
        memmove(&buf[0], &buf[1], (MAX_BPM_SAMPLES-1)*sizeof(float));
        buf[MAX_BPM_SAMPLES-1] = bpm;
    }
}

// --------- API ----------
void appstats_init(void) {
    memset(s_bpm_buf, 0, sizeof(s_bpm_buf));
    s_bpm_n = 0;

    memset(s_cor, 0, sizeof(s_cor));

    s_ans_count = 0;   s_ans_sum = 0.0;
    s_energy_count = 0; s_energy_sum = 0.0;
    s_humor_count = 0;  s_humor_sum = 0.0;

    memset(s_bpm_c, 0, sizeof(s_bpm_c));
    memset(s_bpm_n_c, 0, sizeof(s_bpm_n_c));

    memset(s_ans_count_c, 0, sizeof(s_ans_count_c));
    memset(s_ans_sum_c,   0, sizeof(s_ans_sum_c));

    memset(s_energy_count_c, 0, sizeof(s_energy_count_c));
    memset(s_energy_sum_c,   0, sizeof(s_energy_sum_c));

    memset(s_humor_count_c, 0, sizeof(s_humor_count_c));
    memset(s_humor_sum_c,   0, sizeof(s_humor_sum_c));

    s_sample_id = 0;
    s_current_color = (stat_color_t)STAT_COLOR_NONE;
}

void appstats_set_current_color(stat_color_t c) {
    if ((unsigned)c < STAT_COLOR_COUNT) {
        s_current_color = c;
    } else {
        s_current_color = (stat_color_t)STAT_COLOR_NONE;
    }
}

// NEW: getter da cor corrente
stat_color_t appstats_get_current_color(void) {
    return s_current_color;
}

void appstats_add_bpm(float bpm) {
    if (!(bpm > 0.0f && bpm < 250.0f)) return;
    push_bpm_buf(s_bpm_buf, &s_bpm_n, bpm);

    if ((unsigned)s_current_color < STAT_COLOR_COUNT) {
        push_bpm_buf(s_bpm_c[s_current_color], &s_bpm_n_c[s_current_color], bpm);
    }
    s_sample_id++;
}

void appstats_inc_color(stat_color_t c) {
    if ((unsigned)c < STAT_COLOR_COUNT) {
        s_cor[c]++;
        s_sample_id++;
    }
}

void appstats_add_anxiety(uint8_t level) {
    if (level < 1 || level > 4) return;
    s_ans_sum   += (double)level;
    s_ans_count += 1;

    if ((unsigned)s_current_color < STAT_COLOR_COUNT) {
        s_ans_sum_c[s_current_color]   += (double)level;
        s_ans_count_c[s_current_color] += 1;
    }
    s_sample_id++;
}

void appstats_add_energy(uint8_t level) {
    if (level < 1 || level > 4) return;
    s_energy_sum   += (double)level;
    s_energy_count += 1;

    if ((unsigned)s_current_color < STAT_COLOR_COUNT) {
        s_energy_sum_c[s_current_color]   += (double)level;
        s_energy_count_c[s_current_color] += 1;
    }
    s_sample_id++;
}

void appstats_add_humor(uint8_t level) {
    if (level < 1 || level > 4) return;
    s_humor_sum   += (double)level;
    s_humor_count += 1;

    if ((unsigned)s_current_color < STAT_COLOR_COUNT) {
        s_humor_sum_c[s_current_color]   += (double)level;
        s_humor_count_c[s_current_color] += 1;
    }
    s_sample_id++;
}

static void fill_snapshot_overall(stats_snapshot_t *out) {
    out->sample_id = s_sample_id;

    out->bpm_count = s_bpm_n;
    out->bpm_mean_trimmed = trimmed_mean_1(s_bpm_buf, s_bpm_n);

    out->cor_verde    = s_cor[STAT_COLOR_VERDE];
    out->cor_amarelo  = s_cor[STAT_COLOR_AMARELO];
    out->cor_vermelho = s_cor[STAT_COLOR_VERMELHO];

    out->ans_count = s_ans_count;
    out->ans_mean  = (s_ans_count ? (float)(s_ans_sum / (double)s_ans_count) : NAN);

    out->energy_count = s_energy_count;
    out->energy_mean  = (s_energy_count ? (float)(s_energy_sum / (double)s_energy_count) : NAN);

    out->humor_count = s_humor_count;
    out->humor_mean  = (s_humor_count ? (float)(s_humor_sum / (double)s_humor_count) : NAN);
}

void appstats_get_snapshot(stats_snapshot_t *out) {
    if (!out) return;
    fill_snapshot_overall(out);
}

void appstats_get_snapshot_by_color(stat_color_t color, stats_snapshot_t *out) {
    if (!out) return;
    if (!((unsigned)color < STAT_COLOR_COUNT)) {
        fill_snapshot_overall(out);
        return;
    }

    out->sample_id = s_sample_id;

    // BPM filtrado por cor
    out->bpm_count = s_bpm_n_c[color];
    out->bpm_mean_trimmed = trimmed_mean_1(s_bpm_c[color], s_bpm_n_c[color]);

    // Contagem de cores: mantém só a da cor filtrada
    out->cor_verde    = (color == STAT_COLOR_VERDE   ? s_cor[STAT_COLOR_VERDE]   : 0);
    out->cor_amarelo  = (color == STAT_COLOR_AMARELO ? s_cor[STAT_COLOR_AMARELO] : 0);
    out->cor_vermelho = (color == STAT_COLOR_VERMELHO? s_cor[STAT_COLOR_VERMELHO]: 0);

    // Ansiedade / Energia / Humor filtrados
    out->ans_count = s_ans_count_c[color];
    out->ans_mean  = (s_ans_count_c[color] ? (float)(s_ans_sum_c[color] / (double)s_ans_count_c[color]) : NAN);

    out->energy_count = s_energy_count_c[color];
    out->energy_mean  = (s_energy_count_c[color] ? (float)(s_energy_sum_c[color] / (double)s_energy_count_c[color]) : NAN);

    out->humor_count = s_humor_count_c[color];
    out->humor_mean  = (s_humor_count_c[color] ? (float)(s_humor_sum_c[color] / (double)s_humor_count_c[color]) : NAN);
}

// CSV agregado para /download.csv
size_t appstats_dump_csv(char *dst, size_t maxlen) {
    if (!dst || maxlen == 0) return 0;

    stats_snapshot_t s;
    appstats_get_snapshot(&s);

    // Se vier NaN, substitui por 0 para não imprimir "nan"
    double bpm_mean = isnan(s.bpm_mean_trimmed) ? 0.0 : s.bpm_mean_trimmed;
    double ans_mean = isnan(s.ans_mean)         ? 0.0 : s.ans_mean;
    double ene_mean = isnan(s.energy_mean)      ? 0.0 : s.energy_mean;
    double hum_mean = isnan(s.humor_mean)       ? 0.0 : s.humor_mean;

    size_t total = 0;

    int w = snprintf(dst + total, (total < maxlen) ? (maxlen - total) : 0,
                     "bpm_mean,bpm_n,ans_mean,ans_n,energy_mean,energy_n,humor_mean,humor_n,cores_verde,cores_amarelo,cores_vermelho\r\n");
    if (w < 0) return total;
    total += (size_t)((w > 0) ? w : 0);
    if (total >= maxlen) return maxlen;

    w = snprintf(dst + total, (total < maxlen) ? (maxlen - total) : 0,
                 "%.3f,%lu,%.3f,%lu,%.3f,%lu,%.3f,%lu,%lu,%lu,%lu\r\n",
                 bpm_mean,  (unsigned long)s.bpm_count,
                 ans_mean,  (unsigned long)s.ans_count,
                 ene_mean,  (unsigned long)s.energy_count,
                 hum_mean,  (unsigned long)s.humor_count,
                 (unsigned long)s.cor_verde,
                 (unsigned long)s.cor_amarelo,
                 (unsigned long)s.cor_vermelho);
    if (w < 0) return total;
    total += (size_t)((w > 0) ? w : 0);

    if (total > maxlen) total = maxlen;
    return total;
}
