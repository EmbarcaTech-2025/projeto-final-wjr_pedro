// Evita conflito com lwIP (que também tem stats_init)
#define stats_init          appstats_init
#define stats_add_bpm       appstats_add_bpm
#define stats_inc_color     appstats_inc_color
#define stats_add_anxiety   appstats_add_anxiety
#define stats_get_snapshot  appstats_get_snapshot


#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint32_t sample_id;

    float     bpm_mean_trimmed;  // média “robusta” p/ exibição
    uint32_t  bpm_count;         // quantos BPMs acumulados

    uint32_t  cor_verde;         // contagem por cor
    uint32_t  cor_amarelo;
    uint32_t  cor_vermelho;

    float     ans_mean;          // média simples da ansiedade
    uint32_t  ans_count;         // quantos níveis de ansiedade acumulados
} stats_snapshot_t;

// Enum interno de cor (independente do sensor)
typedef enum {
    STAT_COLOR_VERDE = 0,
    STAT_COLOR_AMARELO = 1,
    STAT_COLOR_VERMELHO = 2,
    STAT_COLOR_COUNT
} stat_color_t;

void stats_init(void);
void stats_add_bpm(float bpm);
void stats_inc_color(stat_color_t c);
void stats_add_anxiety(uint8_t level);
void stats_get_snapshot(stats_snapshot_t *out);
