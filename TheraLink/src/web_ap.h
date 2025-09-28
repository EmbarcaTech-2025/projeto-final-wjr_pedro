#pragma once
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Sobe o AP + DHCP/DNS + HTTP
void web_ap_start(void);

// Espelha as 4 linhas do OLED para /display e /oled.json
void web_display_set_lines(const char *l1, const char *l2, const char *l3, const char *l4);

/* ===== Survey API (compatível com o main.c antigo) ===== */

// Liga/desliga o “modo survey” (quando ON, /display redireciona para /survey)
void web_set_survey_mode(bool on);

// Limpa respostas pendentes (não altera o modo)
void web_survey_reset(void);

// Espia respostas (sem consumir).
// - Se out_bits != NULL, recebe os 10 bits de resposta (bit i => Q(i+1)), em uint16_t.
// - Se out_token != NULL, recebe o token monotônico que incrementa a cada envio.
// Retorna true se há respostas pendentes desde o último reset/consumo.
bool web_survey_peek(uint16_t *out_bits, uint32_t *out_token);

/* ===== API nova (você pode usar se desejar) ===== */
void web_survey_begin(void);                 // atalho: liga modo e limpa pendências
bool web_take_survey(char out_bits_10[11]);  // consome respostas (string "##########")

#ifdef __cplusplus
}
#endif
