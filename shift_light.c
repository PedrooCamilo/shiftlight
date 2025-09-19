/**
 * @file shift_light.c
 * @author Pedro Camilo e Maria Eduarda
 * @brief Versão final do firmware para o Sistema de Telemetria Veicular 
 * @version 2.7
 * @date 2025-09-08
 *
 * @copyright Copyright (c) 2025
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/pio.h"
#include "hardware/adc.h"
#include "ws2818b.pio.h"
#include "play_audio.h"
#include "lvgl.h"
#include "lv_port_disp.h"

// DEFINIÇÕES E TIPOS GLOBAIS
#define LED_COUNT 25
#define LED_PIN 7
#define SW 22
const int vRx = 26;
const int vRy = 27;
#define MAX_IAT_TEMP 90 // Temperatura de Admissão do Ar máxima em °C

struct pixel_t { uint8_t G, R, B; };
typedef struct pixel_t pixel_t;
typedef pixel_t npLED_t;

typedef enum {
    STATE_MENU,
    STATE_SHIFTLIGHT,
    STATE_PERF_STATS,
    STATE_FUEL_TEST, 
    STATE_SETTINGS_SHIFTLIGHT
} ProgramState;

// VARIÁVEIS GLOBAIS
volatile int global_rpm = 0;
volatile int global_speed = 0;
volatile int global_iat = 0;
volatile float global_fuel_rate_lph = 0.0; 
volatile float global_km_per_liter = 0.0;
volatile float brightness = 1.0;
volatile int shift_light_rpm_target = 3500;
static mutex_t lvgl_mutex;

volatile int global_coolant_temp = 0;
volatile float global_timing_advance = 0.0;
volatile float global_commanded_afr = 0.0;

// Novas variáveis para o sistema de alertas
volatile bool alert_active = false;
volatile char alert_message[32];


ProgramState currentState = STATE_MENU;
static int menu_selection = 0;
const int MENU_ITEM_COUNT = 4; 

// Variáveis para o Teste 0-100
bool perf_test_running = false;
uint32_t perf_test_start_time = 0;
float perf_test_result_time = 0.0;
int perf_test_final_speed = 0;

// Variáveis para o Teste de Consumo
bool fuel_test_running = false;
uint32_t fuel_test_start_time = 0;
double total_fuel_consumed_liters = 0.0;
uint32_t last_fuel_calc_time = 0;

static PIO pio_leds;
static uint sm_leds;
npLED_t leds[LED_COUNT];

lv_obj_t *ui_data_screen, *ui_menu_screen;
lv_obj_t *ui_rpm_label, *ui_iat_label, *ui_speed_label;
lv_obj_t *ui_coolant_label, *ui_timing_label, *ui_afr_label;
lv_obj_t *ui_menu_title, *ui_menu_item1, *ui_menu_item2, *ui_menu_item3, *ui_menu_item4;
lv_obj_t *ui_status_label;
lv_obj_t *ui_alert_screen; 
lv_obj_t *ui_alert_label;  

// PROTÓTIPOS DE FUNÇÕES
void npSetLED(const uint index, const uint8_t r, const uint8_t g, const uint8_t b);
void npWrite();
int getIndex(int x, int y);
void npInit(uint pin);
void setup_joystick();
void create_ui();
void update_menu_ui();
bool lv_tick_callback(struct repeating_timer *t);
bool read_line_from_stdio(char* buffer, int max_len);
void atualizarMatriz(int rpm, float brightness);
void core1_entry();
void check_for_alerts();
void calculate_instant_consumption();

// NÚCLEO 1 (DADOS)
void core1_entry() {
    sleep_ms(10); 
    char uart_buffer[64];
    int tag_recebida, valor_recebido;

    while (1) {
        if (read_line_from_stdio(uart_buffer, sizeof(uart_buffer))) {
            if (sscanf(uart_buffer, "%d,%d", &tag_recebida, &valor_recebido) == 2) {
                switch (tag_recebida) {
                    case 1: multicore_fifo_push_blocking(valor_recebido); break;
                    case 2: global_iat = valor_recebido; break;
                    case 3: global_speed = valor_recebido; break;
                    case 4: global_fuel_rate_lph = valor_recebido / 100.0f; break;
                    case 5: global_coolant_temp = valor_recebido; break;
                    case 6: global_timing_advance = valor_recebido / 10.0f; break; // Ex: Python envia 125, aqui vira 12.5
                    case 7: global_commanded_afr = valor_recebido / 100.0f; break; // Ex: Python envia 1470, aqui vira 14.70
                }
            }
        }
        sleep_ms(1);
    }
}

// FUNÇÃO MAIN (NÚCLEO 0)
int main() {
    stdio_init_all();
    sleep_ms(2500);

    mutex_init(&lvgl_mutex);
    lv_init();
    lv_port_disp_init();
    static struct repeating_timer timer;
    add_repeating_timer_ms(-5, lv_tick_callback, NULL, &timer);

    setup_joystick();
    npInit(LED_PIN);
    create_ui();
    
    multicore_fifo_clear_irq();
    multicore_launch_core1(core1_entry);

    bool sw_pressed_last_frame = false;
    uint32_t last_joystick_time = 0;
    uint32_t last_display_update_time = 0;
    
    while (1) {
        mutex_enter_blocking(&lvgl_mutex);
        lv_timer_handler();
        mutex_exit(&lvgl_mutex);
        
        while (multicore_fifo_rvalid()) {
            global_rpm = multicore_fifo_pop_blocking();
        }
        check_for_alerts();
        calculate_instant_consumption();

        atualizarMatriz(global_rpm, brightness);
        
        mutex_enter_blocking(&lvgl_mutex);
        if (alert_active) {
            // Se o alerta está ativo, mostra a tela e atualiza a mensagem
            if (lv_obj_has_flag(ui_alert_screen, LV_OBJ_FLAG_HIDDEN)) {
                lv_obj_clear_flag(ui_alert_screen, LV_OBJ_FLAG_HIDDEN);
            }
            lv_label_set_text(ui_alert_label, (const char*)alert_message);
        } else {
            // Se não há alerta, garante que a tela está escondida
            if (!lv_obj_has_flag(ui_alert_screen, LV_OBJ_FLAG_HIDDEN)) {
                lv_obj_add_flag(ui_alert_screen, LV_OBJ_FLAG_HIDDEN);
            }
        }
        mutex_exit(&lvgl_mutex);

        bool sw_is_pressed_now = !gpio_get(SW);

        if (fuel_test_running) {
            uint32_t now = time_us_32();
            if (last_fuel_calc_time > 0) {
                uint32_t delta_t_us = now - last_fuel_calc_time;
                double delta_t_hours = (double)delta_t_us / 3600000000.0;
                total_fuel_consumed_liters += global_fuel_rate_lph * delta_t_hours;
            }
            last_fuel_calc_time = now;
        }

        switch (currentState) {
            case STATE_MENU: {
                if (sw_is_pressed_now && !sw_pressed_last_frame) {
                    if (menu_selection == 0) {
                        currentState = STATE_SHIFTLIGHT;
                        lv_label_set_text(ui_status_label, "MONITOR");
                    } else if (menu_selection == 1) {
                        currentState = STATE_PERF_STATS;
                        perf_test_result_time = 0.0;
                        perf_test_final_speed = 0;
                        lv_label_set_text(ui_status_label, "TESTE 0-100");
                    } else if (menu_selection == 2) {
                        currentState = STATE_FUEL_TEST;
                        lv_label_set_text(ui_status_label, "TESTE CONSUMO");
                    }else if (menu_selection == 3) { 
                        currentState = STATE_SETTINGS_SHIFTLIGHT;
                        lv_label_set_text(ui_status_label, "AJUSTE RPM");
                    }
                    
                    lv_obj_add_flag(ui_menu_screen, LV_OBJ_FLAG_HIDDEN);
                    lv_obj_clear_flag(ui_data_screen, LV_OBJ_FLAG_HIDDEN);
                }

                if (time_us_32() - last_joystick_time > 200000) {
                    adc_select_input(1);
                    uint16_t joy_y = adc_read();
                    if (joy_y > 3000) {
                        menu_selection = (menu_selection + 1) % MENU_ITEM_COUNT;
                        update_menu_ui();
                    } else if (joy_y < 1000) {
                        menu_selection = (menu_selection == 0) ? MENU_ITEM_COUNT - 1 : menu_selection - 1;
                        update_menu_ui();
                    }
                    last_joystick_time = time_us_32();
                }
                break;
            }

            case STATE_SHIFTLIGHT: {
                if (sw_is_pressed_now && !sw_pressed_last_frame) {
                    currentState = STATE_MENU;
                    lv_label_set_text(ui_status_label, "MENU");
                    lv_obj_clear_flag(ui_menu_screen, LV_OBJ_FLAG_HIDDEN);
                    lv_obj_add_flag(ui_data_screen, LV_OBJ_FLAG_HIDDEN);
                }
                 if (time_us_32() - last_display_update_time > 100000) {
                    lv_label_set_text_fmt(ui_rpm_label, "RPM: %d", global_rpm);
                    lv_label_set_text_fmt(ui_iat_label, "IAT: %d C", global_iat);
                    lv_label_set_text_fmt(ui_speed_label, "Velocidade: %d km/h", global_speed);
                    lv_label_set_text_fmt(ui_coolant_label, "Arref.: %d C", global_coolant_temp);
                    lv_label_set_text_fmt(ui_timing_label, "Avanço: %.1f", global_timing_advance);
                    lv_label_set_text_fmt(ui_afr_label, "AFR Cmd: %.2f", global_commanded_afr);
                    last_display_update_time = time_us_32();
                }
                break;
            }
            
            case STATE_PERF_STATS: {
                if (sw_is_pressed_now && !sw_pressed_last_frame) {
                    if (perf_test_running) {
                        perf_test_running = false;
                        uint32_t tempo_fim_teste = time_us_32();
                        perf_test_result_time = (tempo_fim_teste - perf_test_start_time) / 1000000.0f;
                        perf_test_final_speed = global_speed; 
                    } else {
                        currentState = STATE_MENU;
                        lv_label_set_text(ui_status_label, "MENU");
                        lv_obj_clear_flag(ui_menu_screen, LV_OBJ_FLAG_HIDDEN);
                        lv_obj_add_flag(ui_data_screen, LV_OBJ_FLAG_HIDDEN);
                    }
                }

                if (!perf_test_running && global_speed > 0 && perf_test_result_time == 0.0) { perf_test_running = true; perf_test_start_time = time_us_32(); printf("START_LOG\n"); }
                if (perf_test_running && global_speed >= 100) { perf_test_running = false; uint32_t tempo_fim_teste = time_us_32(); perf_test_result_time = (tempo_fim_teste - perf_test_start_time) / 1000000.0f; perf_test_final_speed = 100;printf("STOP_LOG\n"); }
                if (perf_test_running && global_speed == 0) { perf_test_running = false; perf_test_result_time = 0.0; printf("STOP_LOG\n"); }

                if (time_us_32() - last_display_update_time > 100000) {
                    if (perf_test_running) { float tempo_parcial = (time_us_32() - perf_test_start_time) / 1000000.0f; lv_label_set_text_fmt(ui_rpm_label, "Tempo: %.2f s", tempo_parcial); lv_label_set_text(ui_iat_label, "Clique para PARAR");
                    } else {
                         if (perf_test_result_time > 0.0) { lv_label_set_text_fmt(ui_rpm_label, "0-%d: %.2f s", perf_test_final_speed, perf_test_result_time); lv_label_set_text(ui_iat_label, "Clique para MENU"); } 
                         else { lv_label_set_text(ui_rpm_label, "Aguardando..."); lv_label_set_text(ui_iat_label, "Acelere para iniciar."); }
                    }
                    lv_label_set_text_fmt(ui_speed_label, "Velocidade: %d km/h", global_speed);
                    last_display_update_time = time_us_32();
                }
                break;
            }

            case STATE_FUEL_TEST: {
                if (sw_is_pressed_now && !sw_pressed_last_frame) {
                    if (fuel_test_running) {
                        fuel_test_running = false;
                        last_fuel_calc_time = 0;
                        printf("STOP_LOG\n");
                    } else {
                        if (total_fuel_consumed_liters > 0.0) {
                            currentState = STATE_MENU;
                            total_fuel_consumed_liters = 0.0;
                            lv_label_set_text(ui_status_label, "MENU");
                            lv_obj_clear_flag(ui_menu_screen, LV_OBJ_FLAG_HIDDEN);
                            lv_obj_add_flag(ui_data_screen, LV_OBJ_FLAG_HIDDEN);
                        } else {
                            fuel_test_running = true;
                            total_fuel_consumed_liters = 0.0;
                            fuel_test_start_time = time_us_32();
                            last_fuel_calc_time = time_us_32();
                        }
                    }
                }

                if (time_us_32() - last_display_update_time > 100000) {
                    if (fuel_test_running) {
                        uint32_t elapsed_time_ms = (time_us_32() - fuel_test_start_time) / 1000;
                        int minutes = elapsed_time_ms / 60000;
                        int seconds = (elapsed_time_ms % 60000) / 1000;
                        
                        lv_label_set_text_fmt(ui_rpm_label, "Tempo: %02d:%02d", minutes, seconds);
                        lv_label_set_text_fmt(ui_iat_label, "Gasto: %.3f L", total_fuel_consumed_liters);
                        lv_label_set_text(ui_speed_label, "Clique para PARAR");
                    } else {
                         if (total_fuel_consumed_liters > 0.0) {
                            uint32_t elapsed_time_ms = (last_fuel_calc_time > 0) ? (last_fuel_calc_time - fuel_test_start_time) / 1000 : 0;
                            int minutes = elapsed_time_ms / 60000;
                            int seconds = (elapsed_time_ms % 60000) / 1000;

                            lv_label_set_text_fmt(ui_rpm_label, "Final: %02d:%02d", minutes, seconds);
                            lv_label_set_text_fmt(ui_iat_label, "Total: %.3f L", total_fuel_consumed_liters);
                            lv_label_set_text(ui_speed_label, "Clique para MENU");

                         } else {
                            lv_label_set_text(ui_rpm_label, "Pronto para iniciar");
                            lv_label_set_text_fmt(ui_iat_label, "Consumo: %.1f L/h", global_fuel_rate_lph);
                            lv_label_set_text(ui_speed_label, "Clique para INICIAR");
                         }
                    }
                    last_display_update_time = time_us_32();
                }
                break;
            }
            case STATE_SETTINGS_SHIFTLIGHT: {
                // Lógica de entrada/saída
                if (sw_is_pressed_now && !sw_pressed_last_frame) {
                    currentState = STATE_MENU; // Volta ao menu
                    lv_label_set_text(ui_status_label, "MENU");
                    lv_obj_clear_flag(ui_menu_screen, LV_OBJ_FLAG_HIDDEN);
                    lv_obj_add_flag(ui_data_screen, LV_OBJ_FLAG_HIDDEN);
                }

                // Lógica do Joystick para ajustar valor
                if (time_us_32() - last_joystick_time > 150000) { // Tempo de resposta menor
                    adc_select_input(1); // Eixo Y
                    uint16_t joy_y = adc_read();
                    if (joy_y > 3000) { // Para cima
                        shift_light_rpm_target += 100;
                        if (shift_light_rpm_target > 9000) shift_light_rpm_target = 9000;
                    } else if (joy_y < 1000) { // Para baixo
                        shift_light_rpm_target -= 100;
                        if (shift_light_rpm_target < 1000) shift_light_rpm_target = 1000;
                    }
                    last_joystick_time = time_us_32();
                }

                // Atualiza a tela
                if (time_us_32() - last_display_update_time > 100000) {
                    lv_label_set_text_fmt(ui_rpm_label, "RPM Alvo: %d", shift_light_rpm_target);
                    lv_label_set_text(ui_iat_label, "Use o joystick para alterar");
                    lv_label_set_text(ui_speed_label, "Clique para salvar e voltar");
                    last_display_update_time = time_us_32();
                }
                break;
            }
        }

        sw_pressed_last_frame = sw_is_pressed_now;
        sleep_ms(5);
    }
    return 0;
}


// IMPLEMENTAÇÃO DAS DEMAIS FUNÇÕES
void update_menu_ui() {
    lv_label_set_text_fmt(ui_menu_item1, "%s Monitor", (menu_selection == 0 ? ">" : " "));
    lv_label_set_text_fmt(ui_menu_item2, "%s Teste 0-100", (menu_selection == 1 ? ">" : " "));
    lv_label_set_text_fmt(ui_menu_item3, "%s Teste Consumo", (menu_selection == 2 ? ">" : " ")); 
    lv_label_set_text_fmt(ui_menu_item4, "%s Ajustar Shift Light", (menu_selection == 3 ? ">" : " ")); 

    lv_obj_set_style_text_color(ui_menu_item1, (menu_selection == 0 ? lv_color_hex(0xFFD700) : lv_color_hex(0xFFFFFF)), 0);
    lv_obj_set_style_text_color(ui_menu_item2, (menu_selection == 1 ? lv_color_hex(0xFFD700) : lv_color_hex(0xFFFFFF)), 0);
    lv_obj_set_style_text_color(ui_menu_item3, (menu_selection == 2 ? lv_color_hex(0xFFD700) : lv_color_hex(0xFFFFFF)), 0);
    lv_obj_set_style_text_color(ui_menu_item4, (menu_selection == 3 ? lv_color_hex(0xFFD700) : lv_color_hex(0xFFFFFF)), 0);
}

void create_ui(void) {
    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x101010), LV_PART_MAIN);

    ui_data_screen = lv_obj_create(screen);
    lv_obj_set_size(ui_data_screen, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(ui_data_screen, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ui_data_screen, 0, 0);
    lv_obj_add_flag(ui_data_screen, LV_OBJ_FLAG_HIDDEN);

    ui_rpm_label = lv_label_create(ui_data_screen);
    lv_obj_set_style_text_font(ui_rpm_label, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(ui_rpm_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(ui_rpm_label, LV_ALIGN_TOP_LEFT, 20, 20);

    
    ui_iat_label = lv_label_create(ui_data_screen);
    lv_obj_set_style_text_font(ui_iat_label, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(ui_iat_label, lv_color_hex(0x00BFFF), 0);
    lv_obj_align(ui_iat_label, LV_ALIGN_TOP_LEFT, 20, 60); // Posição Y ajustada

    ui_speed_label = lv_label_create(ui_data_screen);
    lv_obj_set_style_text_font(ui_speed_label, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(ui_speed_label, lv_color_hex(0x32CD32), 0);
    lv_obj_align(ui_speed_label, LV_ALIGN_TOP_LEFT, 20, 85); // Posição Y ajustada

    ui_coolant_label = lv_label_create(ui_data_screen);
    lv_obj_set_style_text_font(ui_coolant_label, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(ui_coolant_label, lv_color_hex(0xFF6347), 0); // Cor Tomate
    lv_obj_align(ui_coolant_label, LV_ALIGN_TOP_LEFT, 20, 110);

    ui_timing_label = lv_label_create(ui_data_screen);
    lv_obj_set_style_text_font(ui_timing_label, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(ui_timing_label, lv_color_hex(0xFFD700), 0); // Cor Dourada
    lv_obj_align(ui_timing_label, LV_ALIGN_TOP_LEFT, 20, 135);

    ui_afr_label = lv_label_create(ui_data_screen);
    lv_obj_set_style_text_font(ui_afr_label, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(ui_afr_label, lv_color_hex(0x9370DB), 0); // Cor Roxo Médio
    lv_obj_align(ui_afr_label, LV_ALIGN_TOP_LEFT, 20, 160);


    ui_alert_screen = lv_obj_create(lv_screen_active());
    lv_obj_set_size(ui_alert_screen, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(ui_alert_screen, lv_color_hex(0xFF0000), 0); // Fundo vermelho
    lv_obj_set_style_bg_opa(ui_alert_screen, LV_OPA_80, 0); // Levemente transparente
    lv_obj_add_flag(ui_alert_screen, LV_OBJ_FLAG_HIDDEN); // Começa escondido

    ui_alert_label = lv_label_create(ui_alert_screen);
    lv_obj_set_style_text_font(ui_alert_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(ui_alert_label, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(ui_alert_label, "ALERTA!");
    lv_obj_center(ui_alert_label);

    ui_menu_screen = lv_obj_create(screen);
    lv_obj_set_size(ui_menu_screen, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(ui_menu_screen, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ui_menu_screen, 0, 0);

    ui_menu_title = lv_label_create(ui_menu_screen);
    lv_obj_set_style_text_font(ui_menu_title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(ui_menu_title, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(ui_menu_title, "Menu Principal");
    lv_obj_align(ui_menu_title, LV_ALIGN_TOP_MID, 0, 10);

    ui_menu_item1 = lv_label_create(ui_menu_screen);
    lv_obj_set_style_text_font(ui_menu_item1, &lv_font_montserrat_22, 0);
    lv_obj_align(ui_menu_item1, LV_ALIGN_CENTER, 0, -20);

    ui_menu_item2 = lv_label_create(ui_menu_screen);
    lv_obj_set_style_text_font(ui_menu_item2, &lv_font_montserrat_22, 0);
    lv_obj_align(ui_menu_item2, LV_ALIGN_CENTER, 0, 10);
    
    ui_menu_item3 = lv_label_create(ui_menu_screen);
    lv_obj_set_style_text_font(ui_menu_item3, &lv_font_montserrat_22, 0);
    lv_obj_align(ui_menu_item3, LV_ALIGN_CENTER, 0, 40);

    ui_menu_item4 = lv_label_create(ui_menu_screen);
    lv_obj_set_style_text_font(ui_menu_item4, &lv_font_montserrat_22, 0);
    lv_obj_align(ui_menu_item4, LV_ALIGN_CENTER, 0, 70); // Ajuste a posição Y
    
    ui_status_label = lv_label_create(screen);
    lv_obj_set_style_text_font(ui_status_label, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(ui_status_label, lv_color_hex(0xFFD700), 0);
    lv_obj_align(ui_status_label, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_label_set_text(ui_status_label, "MENU");

    update_menu_ui();
}

void check_for_alerts() {
    if (global_iat > MAX_IAT_TEMP) {
        if (!alert_active) { 
            alert_active = true;
            snprintf((char*)alert_message, sizeof(alert_message), "IAT ALTA: %d C", global_iat);
        }
    } else {
      
        if (alert_active) {
            alert_active = false;
        }
    }

}

bool read_line_from_stdio(char* buffer, int max_len) {
    static int pos = 0;
    int c;
    while ((c = getchar_timeout_us(0)) != PICO_ERROR_TIMEOUT) {
        if (c == '\n' || c == '\r') {
            buffer[pos] = '\0';
            pos = 0;
            if (buffer[0] != '\0') return true;
        } else if (pos < max_len - 1) {
            buffer[pos++] = (char)c;
        }
    }
    return false;
}

void npSetLED(const uint index, const uint8_t r, const uint8_t g, const uint8_t b) {
    leds[index].R = r;
    leds[index].G = g;
    leds[index].B = b;
}

void npWrite() {
    for (uint i = 0; i < LED_COUNT; ++i) {
        pio_sm_put_blocking(pio_leds, sm_leds, leds[i].G);
        pio_sm_put_blocking(pio_leds, sm_leds, leds[i].R);
        pio_sm_put_blocking(pio_leds, sm_leds, leds[i].B);
    }
    sleep_us(100);
}

int getIndex(int x, int y) {
    return 24 - (y * 5 + (y % 2 == 0 ? x : (4 - x)));
}

void calculate_instant_consumption() {
    if (global_speed > 2 && global_fuel_rate_lph > 0.05) {
        global_km_per_liter = global_speed / global_fuel_rate_lph;
    } else {
        global_km_per_liter = 0.0;
    }
}

void npInit(uint pin) {
    pio_leds = pio0;
    uint offset = pio_add_program(pio_leds, &ws2818b_program);
    sm_leds = pio_claim_unused_sm(pio_leds, true);
    ws2818b_program_init(pio_leds, sm_leds, offset, pin, 800000.f);
    for (uint i = 0; i < LED_COUNT; ++i) npSetLED(i, 0, 0, 0);
    npWrite();
}

void setup_joystick() {
    adc_init();
    adc_gpio_init(vRx);
    adc_gpio_init(vRy);
    gpio_init(SW);
    gpio_set_dir(SW, GPIO_IN);
    gpio_pull_up(SW);
}

bool lv_tick_callback(struct repeating_timer *t) {
    lv_tick_inc(5);
    return true;
}

void atualizarMatriz(int rpm, float brightness) {
    int matriz[5][5][3] = {0};
    int range1 = shift_light_rpm_target - 1700;
    int range2 = shift_light_rpm_target - 1200;
    int range3 = shift_light_rpm_target - 600;
    if (rpm > 0 && rpm < range1) { for (int i = 0; i < 5; i++) { matriz[2][i][2] = 255 * brightness; } } 
    else if (rpm >= range1 && rpm < range2) { for (int i = 0; i < 5; i++) { matriz[2][i][2] = 255 * brightness; } matriz[2][2][0] = 57 * brightness; matriz[2][2][1] = 255 * brightness; matriz[2][2][2]= 20 * brightness; } 
    else if (rpm >= range2 && rpm < range3) { for (int i = 1; i < 4; i++) { matriz[2][i][0] = 57 * brightness ; matriz[2][i][1] = 255 * brightness; matriz[2][i][0] = 20 * brightness ; } matriz[2][0][2] = 255 * brightness; matriz[2][4][2] = 255 * brightness; } 
    else if (rpm >= range3 && rpm < shift_light_rpm_target) { for (int i = 0; i < 5; i++) { matriz[2][i][0] = 57 * brightness; matriz[2][i][1] = 255 * brightness; matriz[2][i][2] = 20 * brightness ; } } 
    else if (rpm >= shift_light_rpm_target) { main_audio(); for (int i = 0; i < 5; i++) { matriz[2][i][0] = 255 * brightness; } }

    for (int linha = 0; linha < 5; linha++) {
        for (int coluna = 0; coluna < 5; coluna++) {
            int posicao = getIndex(linha, coluna);
            npSetLED(posicao, matriz[coluna][linha][0], matriz[coluna][linha][1], matriz[coluna][linha][2]);
        }
    }
    npWrite();
}