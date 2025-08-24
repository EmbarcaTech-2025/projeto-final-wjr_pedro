#ifndef SSD1306_I2C_H
#define SSD1306_I2C_H

#include "pico/stdlib.h"
#include "ssd1306.h"

// Estrutura para definir uma área de renderização
struct render_area {
    uint8_t start_col;
    uint8_t end_col;
    uint8_t start_page;
    uint8_t end_page;
    int buflen;
};

/**
 * @brief Calcula o tamanho do buffer plano para uma área de renderização.
 * @param area Ponteiro para a área.
 */
void calc_render_area_buflen(struct render_area *area);

/**
 * @brief Envia um comando único para o display via I2C.
 * @param cmd Comando a enviar.
 */
void SSD1306_send_cmd(uint8_t cmd);

/**
 * @brief Envia uma lista de comandos.
 * @param buf Array de comandos.
 * @param num Número de comandos.
 */
void SSD1306_send_cmd_list(uint8_t *buf, int num);

/**
 * @brief Envia um buffer de dados para o display.
 * @param buf Ponteiro para o buffer.
 * @param buflen Tamanho do buffer.
 */
void SSD1306_send_buf(uint8_t *buf, int buflen);

/**
 * @brief Ativa ou desativa o scroll horizontal.
 * @param on Se true ativa, se false desativa.
 */
void SSD1306_scroll(bool on);

/**
 * @brief Renderiza uma área do display com o buffer fornecido.
 * @param buf Buffer de dados.
 * @param area Ponteiro para a área de renderização.
 */
void render(uint8_t *buf, struct render_area *area);

#endif // SSD1306_I2C_H
