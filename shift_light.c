#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include <stdlib.h>
#include <ctype.h>
#include "hardware/uart.h"
#include "hardware/clocks.h"
#include "pico/binary_info.h"
#include "hardware/i2c.h"
#include "ws2818b.pio.h"  // Biblioteca gerada pelo arquivo .pio
#include "hardware/adc.h"
#include "hardware/pwm.h"
#include "inc/ssd1306.h"
#include "play_audio.h"

#define LED_COUNT 25  // Número de LEDs
#define LED_PIN 7     // Pino conectado aos LEDs
#define UART_ID uart0 // Define qual UART será usada
#define BAUD_RATE 115200 // Taxa de transmissão UART
#define UART_TX_PIN 0  // Pino TX (não é usado, mas precisa ser configurado)
#define UART_RX_PIN 1  // Pino RX onde receberemos o valor do RPM
#define JOYSTICK_THRESHOLD 2000  // Limiar para considerar a seleção do joystick

// Definição dos pinos usados para o joystick e LEDs
const int vRx = 26;          // Pino de leitura do eixo X do joystick (conectado ao ADC)
const int vRy = 27;          // Pino de leitura do eixo Y do joystick (conectado ao ADC)
const int ADC_CHANNEL_0 = 0; // Canal ADC para o eixo X do joystick
const int ADC_CHANNEL_1 = 1; // Canal ADC para o eixo Y do joystick
const int SW = 22;           // Pino de leitura do botão do joystick

#define PIN_BTN_A 21  // Defina os pinos dos botões conforme necessário
#define PIN_BTN_B 20
float brightness = 0.01;  // Começa com brilho máximo

// Estados do programa
typedef enum {
    STATE_MENU,
    STATE_SHIFTLIGHT
} ProgramState;

ProgramState currentState = STATE_MENU; // Estado inicial
void ajustar_brilho() {
  if (!gpio_get(PIN_BTN_A) && brightness < 1.0) {
      brightness += 0.1;
  }
  if (!gpio_get(PIN_BTN_B) && brightness > 0.1) {
      brightness -= 0.1;
  }
}

// Função para configurar o joystick (pinos de leitura e ADC)
void setup_joystick()
{
  adc_init();         // Inicializa o módulo ADC
  adc_gpio_init(vRx); // Configura o pino VRX (eixo X) para entrada ADC
  adc_gpio_init(vRy); // Configura o pino VRY (eixo Y) para entrada ADC

  gpio_init(SW);             // Inicializa o pino do botão
  gpio_set_dir(SW, GPIO_IN); // Configura o pino do botão como entrada
  gpio_pull_up(SW);          // Ativa o pull-up no pino do botão para evitar flutuações
}

// Função de configuração geral
void setup()
{
  stdio_init_all();                                // Inicializa a porta serial para saída de dados
  setup_joystick();                                // Chama a função de configuração do joystick
}

// Função para ler os valores dos eixos do joystick (X e Y)
void joystick_read_axis(uint16_t *eixo_x, uint16_t *eixo_y)
{
  adc_select_input(ADC_CHANNEL_0); // Seleciona o canal ADC para o eixo X
  sleep_us(2);                     // Pequeno delay para estabilidade
  *eixo_x = adc_read();         // Lê o valor do eixo X (0-4095)

  adc_select_input(ADC_CHANNEL_1); // Seleciona o canal ADC para o eixo Y
  sleep_us(2);                     // Pequeno delay para estabilidade
  *eixo_y = adc_read();         // Lê o valor do eixo Y (0-4095)
}

const uint I2C_SDA = 14; // PINOS PARA DISPLAY
const uint I2C_SCL = 15;

struct pixel_t {
    uint8_t G, R, B;
};
typedef struct pixel_t pixel_t;
typedef pixel_t npLED_t;

npLED_t leds[LED_COUNT];
PIO np_pio;
uint sm;

//Inicializa matriz de LEDS
void npInit(uint pin) {
    uint offset = pio_add_program(pio0, &ws2818b_program);
    np_pio = pio0;
    sm = pio_claim_unused_sm(np_pio, false);
    if (sm < 0) {
        np_pio = pio1;
        sm = pio_claim_unused_sm(np_pio, true);
    }
    ws2818b_program_init(np_pio, sm, offset, pin, 800000.f);
    for (uint i = 0; i < LED_COUNT; ++i) {
        leds[i].R = 0;
        leds[i].G = 0;
        leds[i].B = 0;
    }
}

// define a cor de um LED específico
void npSetLED(const uint index, const uint8_t r, const uint8_t g, const uint8_t b) {
    leds[index].R = r;
    leds[index].G = g;
    leds[index].B = b;
}

//Apaga os LEDS
void npClear() {
    for (uint i = 0; i < LED_COUNT; ++i)
        npSetLED(i, 0, 0, 0);
}

//envia os valores de cor
void npWrite() {
    for (uint i = 0; i < LED_COUNT; ++i) {
        pio_sm_put_blocking(np_pio, sm, leds[i].G);
        pio_sm_put_blocking(np_pio, sm, leds[i].R);
        pio_sm_put_blocking(np_pio, sm, leds[i].B);
    }
    sleep_us(100);
}

//retorna posição
int getIndex(int x, int y) {
    return 24 - (y * 5 + (y % 2 == 0 ? x : (4 - x)));
}

//inicializa comunicação UART
void setup_uart() {
    uart_init(UART_ID, BAUD_RATE);
    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);
}

//Atualiza os LEDs conforme o RPM
void atualizarMatriz(int rpm, float brightness) {
  int matriz[5][5][3] = {0};

  if (rpm > 0 && rpm < 1800) {
      for (int i = 0; i < 5; i++) {
          matriz[2][i][2] = 255 * brightness; // Azul
      }
  } else if (rpm >= 1800 && rpm < 2300) {
      for (int i = 0; i < 5; i++) {
          matriz[2][i][2] = 255 * brightness; // Azul
      }
      matriz[2][2][0] = 57 * brightness; // Amarelo
      matriz[2][2][1] = 255 * brightness;
      matriz[2][2][2]= 20 * brightness;
  } else if (rpm >= 2300 && rpm < 2900) {
      for (int i = 1; i < 4; i++) {
          matriz[2][i][0] = 57 * brightness ; // Amarelo
          matriz[2][i][1] = 255 * brightness;
          matriz[2][i][0] = 20 * brightness ;
      }
      matriz[2][0][2] = 255 * brightness; // Azul
      matriz[2][4][2] = 255 * brightness;
  } else if (rpm >= 2900 && rpm < 3500) {
      for (int i = 0; i < 5; i++) {
          matriz[2][i][0] = 57 * brightness; // Amarelo
          matriz[2][i][1] = 255 * brightness;
          matriz[2][i][2] = 20 * brightness ;
      }
  } else if (rpm >= 3500) {
      main_audio();
      for (int i = 0; i < 5; i++) {
          matriz[2][i][0] = 255 * brightness; // Vermelho
      }
      for (int linha = 0; linha < 5; linha++) {
        for (int coluna = 0; coluna < 5; coluna++) {
            int posicao = getIndex(linha, coluna);
            npSetLED(posicao, matriz[coluna][linha][0], matriz[coluna][linha][1], matriz[coluna][linha][2]);
        }
    }
      npClear();
      for (int linha = 0; linha < 5; linha++) {
        for (int coluna = 0; coluna < 5; coluna++) {
            int posicao = getIndex(linha, coluna);
            npSetLED(posicao, matriz[coluna][linha][0], matriz[coluna][linha][1], matriz[coluna][linha][2]);
        }
    }
    for (int i = 0; i < 5; i++) {
      matriz[2][i][0] = 255 * brightness; // Vermelho
  }
  npClear();
  for (int linha = 0; linha < 5; linha++) {
    for (int coluna = 0; coluna < 5; coluna++) {
        int posicao = getIndex(linha, coluna);
        npSetLED(posicao, matriz[coluna][linha][0], matriz[coluna][linha][1], matriz[coluna][linha][2]);
    }
  }
    for (int i = 0; i < 5; i++) {
      matriz[2][i][0] = 255 * brightness; // Vermelho
    }
  }

  // Aplicar os valores na matriz de LEDs
  for (int linha = 0; linha < 5; linha++) {
      for (int coluna = 0; coluna < 5; coluna++) {
          int posicao = getIndex(linha, coluna);
          npSetLED(posicao, matriz[coluna][linha][0], matriz[coluna][linha][1], matriz[coluna][linha][2]);
      }
  }
  npWrite();
  npClear();
  npWrite();
}

//Faz todo o tratamento da shift light
void shiftlight() {
    printf("Entrei na função shiftlight\n");
    npInit(LED_PIN);
    npClear();

    int leitura;
    int rpm;

    while (currentState == STATE_SHIFTLIGHT) {
        printf("Aguardando RPM...\n");
        printf("Botão: %d\n", gpio_get(SW));

        // Verifica se o botão A foi pressionado para sair do loop
        
    

        if (scanf("%d", &leitura) == 1) {
            rpm = leitura;  // Lê um número inteiro da serial
            printf("RPM recebido: %d\n", rpm);
        }
        // Parte do código para exibir a mensagem no display


        struct render_area frame_area = {
          start_column : 0,
          end_column : ssd1306_width - 1,
          start_page : 0,
          end_page : ssd1306_n_pages - 1
        };
        calculate_render_area_buffer_length(&frame_area);
        // Zera o display inteiro
    uint8_t ssd[ssd1306_buffer_length];
    memset(ssd, 0, ssd1306_buffer_length);
    render_on_display(ssd, &frame_area);

    // Parte do código para exibir a mensagem no display
    char rpm_text[20];
    snprintf(rpm_text, sizeof(rpm_text), "RPM: %d", rpm);

    ssd1306_draw_string(ssd, 5, 20, rpm_text);
    render_on_display(ssd, &frame_area);

    atualizarMatriz(rpm, brightness);

        if (gpio_get(SW) == 0) {
            printf("Botão A pressionado, voltando ao menu...\n");
            rpm = 0;
                  // Zera o display inteiro
                  uint8_t ssd[ssd1306_buffer_length];
                  memset(ssd, 0, ssd1306_buffer_length);
                  render_on_display(ssd, &frame_area);

                int matriz[5][5][3] = {
                    {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}},
                    {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}},
                    {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}},
                    {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}},
                    {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}
                };
                for (int linha = 0; linha < 5; linha++) {
                    for (int coluna = 0; coluna < 5; coluna++) {
                        int posicao = getIndex(linha, coluna);
                        npSetLED(posicao, matriz[coluna][linha][0], matriz[coluna][linha][1], matriz[coluna][linha][2]);
                    }
                }
                npWrite();
                npClear();
                npWrite();
            currentState = STATE_MENU; // Altera o estado para voltar ao menu
        }
        

        // Outras condições de RPM...

          // Pequeno delay para evitar loop muito rápido
    }
}

//Faz o controle dos estados do nosso código e responsável pelo loop principal
int main() {
    stdio_init_all();
    setup_audio();
    i2c_init(i2c1, ssd1306_i2c_clock * 1000);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);
    ssd1306_init();
    uint16_t valor_x, valor_y;
    setup();

    struct render_area frame_area = {
        start_column : 0,
        end_column : ssd1306_width - 1,
        start_page : 0,
        end_page : ssd1306_n_pages - 1
    };

    calculate_render_area_buffer_length(&frame_area);

    // Zera o display inteiro
    uint8_t ssd[ssd1306_buffer_length];
    memset(ssd, 0, ssd1306_buffer_length);
    render_on_display(ssd, &frame_area);

    // Parte do código para exibir a mensagem no display
    char *text[] = {
        "  Shift Light  ",
        "  Pedro Camilo   "};

    int y = 0;
    for (uint i = 0; i < count_of(text); i++) {
        ssd1306_draw_string(ssd, 5, y, text[i]);
        y += 8;
    }
    render_on_display(ssd, &frame_area);

    while (1) {
        if (currentState == STATE_MENU) {
            joystick_read_axis(&valor_x, &valor_y); // Lê os valores dos eixos do joystick
            printf("X: %d\n", valor_x);
            printf("Y: %d\n", valor_y);
            printf("Botao: %d\n", gpio_get(SW));

            // Verifica se o eixo Y está dentro do limite para selecionar "EMBARCATECH"
            if (valor_y > 2200) {
                printf("EMBARCATECH selecionado!\n");
                currentState = STATE_SHIFTLIGHT; // Altera o estado para executar a função shiftlight
            }

            // Pequeno delay antes da próxima leitura
        } else if (currentState == STATE_SHIFTLIGHT) {
            shiftlight(); // Executa a função shiftlight
        }
    }

    return 0;
}