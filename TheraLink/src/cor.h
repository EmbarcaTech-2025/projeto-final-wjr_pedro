#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"

// Endereço I2C do TCS34725
#define TCS34725_ADDR  0x29

// Classes de cor usadas no projeto
typedef enum {
    COR_DESCONHECIDA = 0,
    COR_VERMELHO,
    COR_VERDE,
    COR_AZUL,
    COR_AMARELO,
    COR_BRANCO,
    COR_PRETO,
    COR_CLASS_COUNT                // <-- usado no main.c
} cor_class_t;

// Inicializa o sensor de cor no barramento/pinos informados
// (configura I2C, verifica ID, liga o sensor, define tempo de integração e ganho)
bool cor_init(i2c_inst_t *i2c, uint sda_pin, uint scl_pin);

// Lê valores crus (clear, red, green, blue) – 16 bits cada
bool cor_read_raw(uint16_t *clear, uint16_t *red, uint16_t *green, uint16_t *blue);

// Lê normalizado (0..1 aprox) baseado em 'clear'. Retorna false se leitura falhar.
bool cor_read_rgb_norm(float *r, float *g, float *b, float *c_norm);

// Classifica a cor a partir dos valores normalizados
cor_class_t cor_classify(float r, float g, float b, float c_norm);

// Nome amigável da classe
const char *cor_class_to_str(cor_class_t c);
