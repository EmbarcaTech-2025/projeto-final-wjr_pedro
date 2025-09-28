// Evita conflito com lwIP (que também tem stats_*)
#define stats_init                   appstats_init
#define stats_set_current_color      appstats_set_current_color
#define stats_add_bpm                appstats_add_bpm
#define stats_inc_color              appstats_inc_color
#define stats_add_anxiety            appstats_add_anxiety
#define stats_add_energy             appstats_add_energy
#define stats_add_humor              appstats_add_humor
#define stats_get_snapshot           appstats_get_snapshot
#define stats_get_snapshot_by_color  appstats_get_snapshot_by_color
#define stats_dump_csv               appstats_dump_csv
// NEW: getter da cor corrente do ciclo
#define stats_get_current_color      appstats_get_current_color

#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>   // size_t

// Enum interno de cor (independente do sensor)
typedef enum {
    STAT_COLOR_VERDE = 0,
    STAT_COLOR_AMARELO = 1,
    STAT_COLOR_VERMELHO = 2,
    STAT_COLOR_COUNT
} stat_color_t;

// Cor corrente do “ciclo” (quando usuário capturou pulseira).
// Use STAT_COLOR_NONE para “sem cor definida”.
typedef enum {
    STAT_COLOR_NONE = 255
} stat_color_none_t;

typedef struct {
    uint32_t sample_id;

    float     bpm_mean_trimmed;  // média “robusta” p/ exibição
    uint32_t  bpm_count;         // quantos BPMs acumulados

    uint32_t  cor_verde;         // contagem por cor
    uint32_t  cor_amarelo;
    uint32_t  cor_vermelho;

    float     ans_mean;          // ansiedade média (1..4)
    uint32_t  ans_count;

    float     energy_mean;       // energia média (1..4)
    uint32_t  energy_count;

    float     humor_mean;        // humor médio (1..4)
    uint32_t  humor_count;
} stats_snapshot_t;

void   stats_init(void);

// Define a “cor corrente” para atribuir próximos dados (BPM/ans/energia/humor)
void   stats_set_current_color(stat_color_t c);

// NEW: lê a cor corrente do ciclo (para o web travar a cor no início do survey)
stat_color_t stats_get_current_color(void);

void   stats_add_bpm(float bpm);
void   stats_inc_color(stat_color_t c);
void   stats_add_anxiety(uint8_t level);
void   stats_add_energy(uint8_t level);
void   stats_add_humor(uint8_t level);

// Snapshot geral (todas as cores)
void   stats_get_snapshot(stats_snapshot_t *out);

// Snapshot filtrado por cor específica
void   stats_get_snapshot_by_color(stat_color_t color, stats_snapshot_t *out);

// Gera CSV agregado para download (/download.csv)
size_t stats_dump_csv(char *dst, size_t maxlen);
