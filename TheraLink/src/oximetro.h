#ifndef OXIMETRO_H_DEMO_HYBRID
#define OXIMETRO_H_DEMO_HYBRID

#include <stdbool.h>
#include <stdint.h>

#include "hardware/i2c.h"


typedef enum {
    OXI_WAIT_FINGER = 0,  // aguardando dedo (MAX30102)
    OXI_SETTLE,           // estabilização (MAX30102)
    OXI_RUN,              // medindo (BPM)

    OXI_DONE,
    OXI_ERROR
} oxi_state_t;

bool  oxi_init(i2c_inst_t *i2c, uint sda, uint scl);
void  oxi_start(void);
void  oxi_poll(uint32_t now_ms);

oxi_state_t oxi_get_state(void);

float oxi_get_bpm_live(void);

float oxi_get_bpm_final(void);
void  oxi_get_progress(int *n_validos, int *n_alvo);

#endif