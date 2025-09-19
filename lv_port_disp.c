/**
 * @file lv_port_disp.c
 * @brief LVGL display port for ST7789 - VERSÃO CORRIGIDA
 */

#include "lv_port_disp.h"
#include "st7789_lcd_pio.h"
#include "hardware/gpio.h"

// --- Buffers de Desenho ---
#define BUF_SIZE (SCREEN_WIDTH * SCREEN_HEIGHT / 10)
static uint8_t buf1[BUF_SIZE * sizeof(lv_color_t)];
static uint8_t buf2[BUF_SIZE * sizeof(lv_color_t)];

// *** MUDANÇA CRÍTICA ***
// Variáveis privadas para controlar o PIO do display.
static PIO pio_disp;
static uint sm_disp;


static void disp_flush_cb(lv_display_t * disp, const lv_area_t * area, uint8_t * px_map)
{
    // 1. Usa as variáveis privadas para chamar a função de baixo nível
    lcd_set_window(pio_disp, sm_disp, area->x1, area->x2, area->y1, area->y2);

    // 2. Envia os dados de pixel
    uint32_t size = lv_area_get_size(area);
    uint16_t* color_p16 = (uint16_t*)px_map;

    st7789_lcd_wait_idle(pio_disp, sm_disp);
    lcd_set_dc_cs(1, 0); // Modo de dados
    for (uint32_t i = 0; i < size; i++) {
        st7789_lcd_put(pio_disp, sm_disp, *color_p16 >> 8);
        st7789_lcd_put(pio_disp, sm_disp, *color_p16 & 0xFF);
        color_p16++;
    }
    st7789_lcd_wait_idle(pio_disp, sm_disp);
    lcd_set_dc_cs(1, 1); // Fim dos dados

    // 3. Informa à LVGL que o envio terminou
    lv_display_flush_ready(disp);
}

void lv_port_disp_init(void)
{
    // *** MUDANÇA CRÍTICA ***
    // 1. Inicialização do Hardware (PIO e GPIOs)
    // Este arquivo agora gerencia seus próprios recursos PIO.
    pio_disp = pio1; // Usando o PIO 1 para o display
    
    uint offset = pio_add_program(pio_disp, &st7789_lcd_program);
    sm_disp = pio_claim_unused_sm(pio_disp, true);
    
    st7789_lcd_program_init(pio_disp, sm_disp, offset, PIN_DIN, PIN_CLK, 1.f);

    // O resto da inicialização dos GPIOs...
    gpio_init(PIN_CS);
    gpio_init(PIN_DC);
    gpio_init(PIN_RESET);
    gpio_init(PIN_BL);

    gpio_set_dir(PIN_CS, GPIO_OUT);
    gpio_set_dir(PIN_DC, GPIO_OUT);
    gpio_set_dir(PIN_RESET, GPIO_OUT);
    gpio_set_dir(PIN_BL, GPIO_OUT);

    gpio_put(PIN_CS, 1);
    gpio_put(PIN_RESET, 1);
    sleep_ms(100);
    gpio_put(PIN_RESET, 0);
    sleep_ms(100);
    gpio_put(PIN_RESET, 1);
    
    // Passa os handles do PIO para a função de inicialização
    lcd_init(pio_disp, sm_disp, st7789_init_seq);
    
    gpio_put(PIN_BL, 1);

    // 2. Configuração do Driver na LVGL
    lv_display_t * disp = lv_display_create(SCREEN_WIDTH, SCREEN_HEIGHT);
    if (disp == NULL) {
        return;
    }
    
    lv_display_set_flush_cb(disp, disp_flush_cb);
    lv_display_set_buffers(disp, buf1, buf2, sizeof(buf1), LV_DISPLAY_RENDER_MODE_PARTIAL);
}