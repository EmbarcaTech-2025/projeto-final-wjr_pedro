#pragma once
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Sobe o AP + DHCP/DNS + HTTP
void web_ap_start(void);

// Espelha as 4 linhas do OLED para a página /display e para /oled.json
void web_display_set_lines(const char *l1, const char *l2, const char *l3, const char *l4);

#ifdef __cplusplus
}
#endif
