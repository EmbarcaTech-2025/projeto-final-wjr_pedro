#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "stats.h"

#ifdef __cplusplus
extern "C" {
#endif

// Sobe o AP + DHCP/DNS + HTTP
void web_ap_start(void);

// Espelha as 4 linhas do OLED para /display e /oled.json
void web_display_set_lines(const char *l1, const char *l2, const char *l3, const char *l4);

// ---- Survey control ----
// Liga/desliga o modo "abrir /survey" no /display
void web_set_survey_mode(bool on);
// Limpa flags internas (não mexe nos agregados)
void web_survey_reset(void);

// Espia a última submissão pendente (sem consumir). Retorna true se há algo pendente.
bool web_survey_peek(uint16_t *out_bits, uint32_t *out_token);

// Consome a submissão pendente e devolve bits + token. Retorna true se havia.
bool web_take_survey_bits(uint16_t *out_bits, uint32_t *out_token);

// Depois que a cor for definida/validada, chame isto para atribuir
// a submissão (via token) ao grupo correto.
void web_assign_survey_token_to_color(uint32_t token, stat_color_t color);

#ifdef __cplusplus
}
#endif
