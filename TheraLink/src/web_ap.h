#pragma once
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Sobe o AP + DHCP/DNS + HTTP
void web_ap_start(void);

// Espelha as 4 linhas do OLED para a página /display e para /oled.json
void web_display_set_lines(const char *l1, const char *l2, const char *l3, const char *l4);

/* ---- Integração do SURVEY ----
 * main.c chama web_survey_begin() quando entra na etapa do questionário.
 * O servidor passa a anunciar mode=1 em /survey_state.json.
 * Ao submeter /survey_submit?ans=XXXXXXXXXX, o servidor guarda as respostas
 * e web_take_survey() retorna true uma única vez com os bits.
 */
void web_survey_begin(void);
bool web_take_survey(char out_bits_10[11]); // 10 chars '0'/'1' + '\0'

#ifdef __cplusplus
}
#endif
