# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 1.5.1)
set(toolchainVersion 13_2_Rel1)
set(picotoolVersion 2.0.0)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico_w CACHE STRING "Board type")

cmake_minimum_required(VERSION 3.13)

# Definições padrão do CMake
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Configuração do Raspberry Pi Pico SDK
if (NOT DEFINED PICO_SDK_PATH)
    set(PICO_SDK_PATH "${CMAKE_CURRENT_LIST_DIR}/pico-sdk" CACHE STRING "Path to the Pico SDK")
endif()

if (NOT EXISTS ${PICO_SDK_PATH})
    message(FATAL_ERROR "PICO SDK não encontrado no caminho especificado: ${PICO_SDK_PATH}. Certifique-se de que está instalado corretamente.")
endif()

# Importando o SDK do Raspberry Pi Pico
include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)

# Definição do projeto
project(shift_light C CXX ASM)

# Inicialização do SDK
pico_sdk_init()

# Adicionando o executável principal
add_executable(shift_light
    shift_light.c
    inc/ssd1306_i2c.c
    play_audio.c  # Adiciona o arquivo da biblioteca ssd1306
    st7789_lcd_pio.c
    lv_port_disp.c
)

pico_generate_pio_header(shift_light ${CMAKE_CURRENT_LIST_DIR}/ws2818b.pio)
pico_generate_pio_header(shift_light ${CMAKE_CURRENT_LIST_DIR}/st7789_lcd.pio)

# Definindo nome e versão do programa
pico_set_program_name(shift_light "shift_light")
pico_set_program_version(shift_light "0.1")

# Configuração de saída via USB/UART
pico_enable_stdio_uart(shift_light 0)
pico_enable_stdio_usb(shift_light 1)

# Diretórios de inclusão
target_include_directories(shift_light PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/inc  # Adiciona o diretório da biblioteca ssd1306
)

add_subdirectory(lvgl) # jrfo - added
set(LV_LVGL_H_SIMPLE_INCLUDE ON) # jrfo - added
set(LV_CONF_INCLUDE_SIMPLE ON) # jrfo - added
add_subdirectory(ui) # jrfo - added

# Adicionando bibliotecas necessárias
target_link_libraries(shift_light
    pico_stdlib
    hardware_pio
    hardware_clocks
    hardware_adc
    hardware_uart
    pico_multicore
    pico_sync
    hardware_irq
    hardware_i2c  # Adiciona suporte para I2C (necessário para o display OLED)
    hardware_adc  # Adiciona suporte para ADC (necessário para o joystick)
    hardware_pwm  # Adiciona suporte para PWM (se necessário)
    lvgl::lvgl
    ui
)

target_link_options(shift_light PRIVATE "-u_printf_float" "-u_scanf_float")

# Adiciona os arquivos de saída extras (UF2, BIN, HEX)
pico_add_extra_outputs(shift_light)