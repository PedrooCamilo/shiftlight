/**
 * @file st7789_pio.h
 * @brief Header file for ST7789 LCD PIO interface - VERSÃO CORRIGIDA
 */

#ifndef ST7789_PIO_H
#define ST7789_PIO_H

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "st7789_lcd.pio.h"

// Definições de pinos e tela (sem alterações)
#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240
#define ST7789_ROTATION_0 0x00
#define ST7789_ROTATION_90 0x60
#define ST7789_ROTATION_180 0xC0
#define ST7789_ROTATION_270 0xA0
#define ST7789_ROTATION ST7789_ROTATION_90
#define PIN_DIN 19
#define PIN_CLK 18
#define PIN_CS 17
#define PIN_DC 4
#define PIN_RESET 20
#define PIN_BL 9

// Sequência de inicialização (sem alterações)
static const uint8_t st7789_init_seq[] = {
    1, 20, 0x01,
    1, 10, 0x11,
    2, 2, 0x3a, 0x55,
    2, 0, 0x36, ST7789_ROTATION,
    5, 0, 0x2a, 0x00, 0x00, SCREEN_WIDTH >> 8, SCREEN_WIDTH & 0xff,
    5, 0, 0x2b, 0x00, 0x00, SCREEN_HEIGHT >> 8, SCREEN_HEIGHT & 0xff,
    1, 2, 0x21,
    1, 2, 0x13,
    1, 2, 0x29,
    0
};

// *** MUDANÇA CRÍTICA ***
// As funções agora aceitam 'pio' e 'sm' como parâmetros.
// As variáveis globais 'extern' foram removidas.
void lcd_set_window(PIO pio, uint sm, uint16_t x0, uint16_t x1, uint16_t y0, uint16_t y1);
void lcd_init(PIO pio, uint sm, const uint8_t *init_seq);
void lcd_set_dc_cs(bool dc, bool cs); // Adicionando esta declaração que estava faltando

#endif // ST7789_PIO_H