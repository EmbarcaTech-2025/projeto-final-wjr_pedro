#ifndef OXIMETRO_H
#define OXIMETRO_H

#include <stdbool.h>
#include <stdint.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    OXI_IDLE = 0,
    OXI_WAIT_FINGER,
    OXI_SETTLE,
    OXI_RUN,
    OXI_DONE,
    OXI_ERROR
} oxi_state_t;

/* Inicializa contexto do oxímetro (define barramento/pinos e tenta detectar MAX30100/30102).
   Retorna true se o dispositivo foi detectado e configurado. */
bool oxi_init(i2c_inst_t *i2c, uint sda_pin, uint scl_pin);

/* Começa uma nova medição (limpa buffers/estado) */
void oxi_start(void);

/* Cancela/para a medição atual e volta ao estado IDLE */
void oxi_abort(void);

/* Deve ser chamado periodicamente (ex.: a cada ~10–20 ms).
   'now_ms' = to_ms_since_boot(get_absolute_time()).
   Não bloqueia. */
void oxi_poll(uint32_t now_ms);

/* Estado atual */
oxi_state_t oxi_get_state(void);

/* Progresso: retorna (valid_count, target) */
void oxi_get_progress(int *valid_count, int *target_valid);

/* Última estimativa “suave” (durante RUN) */
float oxi_get_bpm_live(void);

/* Resultado final (após DONE). Retorna NAN se não houver. */
float oxi_get_bpm_final(void);



#ifdef __cplusplus
}
#endif
#endif