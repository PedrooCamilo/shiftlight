# Sistema de Telemetria Veicular com Raspberry Pi Pico

Este projeto é um sistema completo de telemetria automotiva, desenvolvido para fornecer dados em tempo real do veículo, além de funcionalidades de performance e análise de dados. O coração do sistema é um **Raspberry Pi Pico**, que controla uma interface gráfica, uma matriz de LEDs para shift light e se comunica com um script Python para obter dados via OBD-II.

---

## 📜 Funcionalidades Principais

- **Monitoramento em Tempo Real**: Exibe dados vitais do motor em um display LCD, incluindo:
  - RPM (Rotações por Minuto)
  - Velocidade do Veículo (km/h)
  - Temperatura do Ar de Admissão (IAT)
  - Temperatura do Líquido de Arrefecimento
  - Avanço de Ignição
  - AFR (Air/Fuel Ratio) Comandado pela ECU

- **Shift Light Progressivo**: Utiliza uma matriz de LEDs 5x5 WS2812B para indicar visualmente a rotação ideal para a troca de marcha, com RPM alvo configurável.

- **Menu Interativo**: Navegação simples através de um joystick analógico para acessar diferentes modos de operação.

- **Medidor de Performance**: Função para teste de aceleração de 0-100 km/h com medição de tempo precisa.

- **Teste de Consumo**: Modo para medir o consumo de combustível durante um trajeto ou período específico.

- **Sistema de Alertas**: Alerta visual na tela para parâmetros críticos (ex: temperatura de admissão muito alta).

- **Datalogging**: O script Python pode registrar os dados da sessão em um arquivo `.csv` para análise posterior.

- **Análise de Dados e Gráficos**: Scripts auxiliares em Python para gerar gráficos de performance e estimar curvas de potência e torque a partir dos logs gravados.

---

## 🛠️ Hardware Necessário

- **Raspberry Pi Pico**: O microcontrolador principal do projeto.  
- **Adaptador OBD-II Bluetooth Low Energy (BLE)**: Para comunicação sem fio com a porta OBD-II do veículo (ex: V-Gate iCar Pro BLE 4.0).  
- **Display LCD**: Compatível com a biblioteca LVGL (ex: ST7789 de 240x240 pixels).  
- **Matriz de LEDs 5x5 WS2812B**: Para o efeito de shift light.  
- **Módulo Joystick Analógico (KY-023)**: Para navegação no menu.  
- **Módulo de Som/Buzzer**: Para alertas sonoros.  
- **Computador/Notebook**: Para rodar os scripts Python que capturam e enviam os dados.

---

## ⚙️ Software e Dependências

### Firmware (Raspberry Pi Pico)

O firmware é escrito em **C/C++** e requer o **Pico C/C++ SDK** configurado no seu ambiente de desenvolvimento.

- **Pico SDK**: Biblioteca principal para desenvolvimento no Pico.  
- **LVGL**: Biblioteca gráfica para a interface de usuário no display.  
- **PIO (Programmable I/O)**: Utilizado para o controle preciso dos LEDs WS2812B.

### Scripts Python (Computador)

Os scripts rodam no computador que se conecta ao adaptador OBD-II e se comunica com o Pico via USB.

- **Python 3.8+**
- Bibliotecas:
  - `bleak`: Para comunicação Bluetooth Low Energy.
  - `pyserial`: Para comunicação serial com o Pico.
  - `pandas`: Para manipulação e análise dos dados do log.
  - `matplotlib`: Para a geração dos gráficos.
  - `numpy`: Dependência do Pandas para cálculos numéricos.
  - `scipy`: Usada para a suavização de dados no script de análise de potência.

Crie um arquivo **`requirements.txt`** com o seguinte conteúdo e instale com:

```bash
pip install -r requirements.txt
```

requirements.txt
``` bash
bleak
pyserial
pandas
matplotlib
numpy
scipy
```

## 📂 Estrutura do Projeto

- **shiftlight.c**: Código fonte principal do firmware que roda no Raspberry Pi Pico.  
- **get_rpm.py**: Script Python executado no computador. Conecta-se ao adaptador OBD-II, lê os dados do carro e envia para o Pico via porta serial USB. Também gera os arquivos de log `.csv`.  
- **graphs.py**: Script para visualizar os dados de um arquivo de log gerado, plotando gráficos de RPM, velocidade, etc.  
- **analise_potencia.py**: Script para análise aprofundada, estimando curvas de potência (CV) e torque (N·m) do motor com base nos dados do log.  

---

## 🚀 Como Compilar e Usar

### 🔹 Parte 1: Compilando o Firmware para o Pico

1. **Configure o Ambiente**  
   Certifique-se de que o ambiente de desenvolvimento Pico C/C++ SDK está instalado e configurado corretamente.

2. **Crie o `CMakeLists.txt`** na pasta do projeto com o seguinte conteúdo:

   ```cmake
   cmake_minimum_required(VERSION 3.13)
   include(pico_sdk_import.cmake)

   project(telemetria_automotiva C CXX ASM)
   set(CMAKE_C_STANDARD 11)
   set(CMAKE_CXX_STANDARD 17)

   pico_sdk_init()

   # Adiciona o executável do projeto
   add_executable(${PROJECT_NAME}
       shiftlight.c
       # Adicione aqui outros arquivos .c se houver (ex: lv_port_disp.c)
   )

   # Adiciona a biblioteca LVGL (assumindo que está em uma pasta 'lvgl')
   # add_subdirectory(lvgl)
   # target_link_libraries(${PROJECT_NAME} lvgl)

   # Adiciona o programa PIO
   pico_generate_pio_header(${PROJECT_NAME} ws2818b.pio)

   # Linka as bibliotecas necessárias do SDK
   target_link_libraries(${PROJECT_NAME} PRIVATE
       pico_stdlib
       pico_multicore
       hardware_pio
       hardware_adc
   )

   pico_add_extra_outputs(${PROJECT_NAME})


3. Compile o Projeto 
    ``` bash
    mkdir build
    cd build
    cmake ..
    make
    ```
4. Flashe o Firmware

    Pressione o botão BOOTSEL no seu Pico.

    Conecte-o ao computador.

    Arraste o arquivo build/telemetria_automotiva.uf2 para o drive que aparecer.

### 🔹 Parte 2: Executando o Sistema

Configure o get_rpm.py alterando as variáveis:

DEVICE_ADDRESS: Endereço MAC do seu adaptador OBD-II BLE.

SERIAL_PORT: Porta COM correta que o Pico está usando.

Instale as dependências Python:

pip install -r requirements.txt


Execute o Script:

python get_rpm.py


O script conecta-se ao adaptador OBD-II.
Uma vez conectado, os dados começam a ser enviados para o Pico e exibidos na tela.

### 🔹 Parte 3: Analisando os Dados

Arquivos de Log
Os testes de performance e consumo geram arquivos .csv na mesma pasta do script get_rpm.py.

Visualizar Gráficos:

python graphs.py nome_do_seu_log.csv


Estimar Potência:

python analise_potencia.py nome_do_seu_log.csv


⚠️ Atenção: Preencha os dados do seu veículo no início do arquivo analise_potencia.py para obter resultados mais precisos.

## 🧠 Como Funciona

O sistema é dividido em dois núcleos no Raspberry Pi Pico para garantir performance:

Core 1:

Dedicado exclusivamente a receber dados via UART (enviados pelo script Python).

Garante que nenhuma informação da ECU seja perdida.

Core 0:

Processa os dados recebidos do Core 1.

Controla a interface gráfica com a biblioteca LVGL.

Lê as entradas do joystick para navegação no menu.

Atualiza a matriz de LEDs com base na RPM atual.

Gerencia os estados do programa (menu, monitor, testes, etc.).

## 🔌 O script get_rpm.py atua como uma ponte:

Traduz os comandos OBD-II em dados simples.

Envia-os serialmente para o Pico.

Simplifica a lógica no microcontrolador.

## 👨‍💻 Autor

Pedro Camilo e Maria Eduarda Araujo