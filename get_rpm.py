# Versão Final - Sistema de Telemetria Veicular
# Autor: Pedro Camilo (com assistência do Gemini)
# Data: 28 de agosto de 2025

import asyncio
import serial
from bleak import BleakClient
import csv  # <-- ADICIONE
import datetime # <-- ADICIONE

# --- Configurações ---
DEVICE_ADDRESS = "88:1B:99:67:5B:38"
UUID_WRITE = "0000fff2-0000-1000-8000-00805f9b34fb"
UUID_NOTIFY = "0000fff1-0000-1000-8000-00805f9b34fb"

SERIAL_PORT = "COM9"   # IMPORTANTE: Verifique se esta é a porta COM correta no Gerenciador de Dispositivos
BAUD_RATE = 115200

AIR_FUEL_RATIO = 14.7  # g de ar / g de gasolina
GASOLINE_DENSITY_G_PER_L = 750.0 # g/L

# --- Constantes Físicas para Cálculo de Consumo (Método Speed-Density) ---
ENGINE_DISPLACEMENT_L = 1.0  # Cilindrada do motor em Litros (1.0 para o Up TSI)
VOLUMETRIC_EFFICIENCY = 0.90 # Eficiência volumétrica estimada (90% é um bom valor para turbo)
AIR_FUEL_RATIO = 14.7        # g de ar / g de gasolina
GASOLINE_DENSITY_G_PER_L = 750.0 # g/L

# --- Variáveis globais para armazenar os últimos valores lidos ---
last_rpm = 0
last_iat_celsius = 25 # Começa com um valor padrão
last_speed = 0 # Adicione se não tiver
last_fuel_lph = 0.0 # Adicione se não tiver
last_coolant_temp = 0       ## NEW
last_timing_advance = 0     ## NEW
last_commanded_afr = 0      ## NEW

# --- Variáveis do Datalogger ---
datalog_active = False
csv_file = None
csv_writer = None


# --- Conexão Serial ---
try:
    ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
    print(f"✅ Serial conectado em {SERIAL_PORT} a {BAUD_RATE} baud")
except Exception as e:
    print(f"❌ Erro ao abrir a porta serial: {e}")
    ser = None

def write_log_entry():
    """Escreve a linha de dados atual no arquivo CSV, se a gravação estiver ativa."""
    if datalog_active and csv_writer:
        timestamp = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S.%f")[:-3]
        csv_writer.writerow([
            timestamp,
            int(last_rpm),
            last_speed,
            last_iat_celsius,
            round(last_fuel_lph, 2)
        ])
# --- Funções de Comunicação ---
def send_serial(tag,value):
    if ser and ser.is_open:
        try:
            ser.write(f"{tag},{int(value)}\n".encode())
            print(f"📤 Enviado via Serial: {f"{int(value)}\n".strip()}")
        except Exception as e:
            print(f"❌ Erro ao enviar dados via Serial: {e}")


async def read_obd_data(client, command):
    await client.write_gatt_char(UUID_WRITE, command.encode())


# --- Funções de Processamento de Dados (Parsers) ---
def parse_rpm(response_str):
    global last_rpm
    try:
        parts = response_str.split()
        if len(parts) >= 4:
            rpm = ((int(parts[2], 16) * 256) + int(parts[3], 16)) / 4
            print(f"🚗 RPM: {int(rpm)} RPM")
            last_rpm=rpm
            send_serial(1,rpm)
    except Exception as e:
        print(f"⚠️ Erro ao processar RPM: {e}")


def parse_air_intake_temp(response_str):
    global last_iat_celsius
    try:
        parts = response_str.split()
        if len(parts) >= 3:
            temp = int(parts[2], 16) - 40
            print(f"🌡️ Temp. Admissão: {temp}°C")
            last_iat_celsius = temp
            send_serial(2,temp)
    except Exception as e:
        print(f"⚠️ Erro ao processar IAT: {e}")


def parse_vehicle_speed(response_str):
    global last_speed
    try:
        parts = response_str.split()
        if len(parts) >= 3:
            speed = int(parts[2], 16)
            last_speed = speed
            print(f"🛞 Velocidade: {speed} km/h")
            send_serial(3, speed)
    except Exception as e:
        print(f"⚠️ Erro ao processar VSS: {e}")


def parse_map_and_calc_fuel(response_str):
    global last_rpm
    global last_fuel_lph
    try:
        parts = response_str.split()
        print(f"las_rpm{last_rpm}")
        if response_str.startswith("41 0B") and len(parts) >= 3:
            map_kpa = int(parts[2], 16) # Pressão em kPa
            print(f"map_kpa{map_kpa}")
            
            # Converte temperatura de Celsius para Kelvin
            iat_kelvin = last_iat_celsius + 273.15
            print(f"IAT_KELVIN{iat_kelvin}")
            
            # Calcula a massa de ar admitida por ciclo (g/ciclo)
            # Constante R para ar seco é ~287 J/(kg*K)
            air_mass = (map_kpa * 1000 * ENGINE_DISPLACEMENT_L / 1000 * VOLUMETRIC_EFFICIENCY) / (287.05 * iat_kelvin)
            air_mass_grams = air_mass * 1000
            
            # Calcula o fluxo de massa de ar (g/s)
            # Motor 4 tempos -> 2 rotações por ciclo de admissão
            maf_calc_g_per_sec = (air_mass_grams * last_rpm) / 120.0
            
            # Usa o mesmo cálculo de antes para obter L/h
            fuel_g_per_sec = maf_calc_g_per_sec / AIR_FUEL_RATIO
            fuel_l_per_sec = fuel_g_per_sec / GASOLINE_DENSITY_G_PER_L
            fuel_l_per_hour = fuel_l_per_sec * 3600

            print(f"💧 Consumo (MAP): {fuel_l_per_hour:.2f} L/h")
            last_fuel_lph = fuel_l_per_hour
            value_to_send = int(fuel_l_per_hour * 100)
            send_serial(4, value_to_send)
    except Exception as e:
        print(f"⚠️ Erro ao processar MAP/Consumo: {e}")

def parse_engine_coolant_temp(response_str):
    ## NEW FUNCTION
    global last_coolant_temp
    try:
        parts = response_str.split()
        if len(parts) >= 3:
            # Formula: A - 40
            temp_c = int(parts[2], 16) - 40
            print(f"💧 Temp. Arrefecimento: {temp_c}°C")
            last_coolant_temp = temp_c
            # You can add a new serial tag if you want to display this on your dashboard
            # send_serial(5, temp_c)
    except Exception as e:
        print(f"⚠️ Erro ao processar ECT: {e}")

def parse_timing_advance(response_str):
    ## NEW FUNCTION
    global last_timing_advance
    try:
        parts = response_str.split()
        if len(parts) >= 3:
            # Formula: (A / 2) - 64
            advance = (int(parts[2], 16) / 2) - 64
            print(f"⚡ Avanço de Ignição: {advance:.1f}°")
            last_timing_advance = advance
            # send_serial(6, advance)
    except Exception as e:
        print(f"⚠️ Erro ao processar Avanço de Ignição: {e}")

def parse_commanded_afr(response_str):
    ## NEW FUNCTION
    global last_commanded_afr
    try:
        parts = response_str.split()
        if len(parts) >= 4:
            # Formula for Commanded Equivalence Ratio: ((A * 256) + B) / 32768
            # Then AFR = Ratio * 14.7 (for gasoline)
            ratio = ((int(parts[2], 16) * 256) + int(parts[3], 16)) / 32768
            afr = ratio * 14.7
            print(f"⛽ AFR Comandado: {afr:.2f}:1")
            last_commanded_afr = afr
            # send_serial(7, afr)
    except Exception as e:
        print(f"⚠️ Erro ao processar AFR Comandado: {e}")


# --- Roteador de Notificações ---
def notification_handler(sender, data):
    try:
        response_str = data.decode('utf-8').strip()
    except UnicodeDecodeError:
        return

    if not response_str or ">" in response_str or "STOPPED" in response_str:
        return

    if response_str.startswith("41 0C"):
        parse_rpm(response_str)
    elif response_str.startswith("41 0F"):
        parse_air_intake_temp(response_str)
    elif response_str.startswith("41 0D"):
        parse_vehicle_speed(response_str)
    elif response_str.startswith("41 0B"):
        parse_map_and_calc_fuel(response_str)
    elif response_str.startswith("41 05"): ## NEW
        parse_engine_coolant_temp(response_str)
    elif response_str.startswith("41 0E"): ## NEW
        parse_timing_advance(response_str)
    elif response_str.startswith("41 44"): ## NEW
        parse_commanded_afr(response_str)


# --- Loop Principal e Execução ---
async def main_loop(client):
    monitoring_active = True 
    while client.is_connected:
        # --- SEÇÃO DE COMANDOS DO PICO ---
        if ser and ser.in_waiting > 0:
            try:
                pico_command = ser.readline().decode('utf-8').strip()
                if pico_command == "START_LOG":
                    start_datalogging()
                elif pico_command == "STOP_LOG":
                    stop_datalogging()
                # Adicione outros elifs aqui para futuros comandos (como REQ_DTC)

            except Exception as e:
                print(f"Erro ao ler comando do Pico: {e}")
        # --- SEÇÃO DE LEITURA DE DADOS ---
        if monitoring_active:

            await read_obd_data(client, "010C\r")
            await asyncio.sleep(0.02)  # Pausa para estabilidade

            await read_obd_data(client, "010F\r")
            await asyncio.sleep(0.1)
            await read_obd_data(client, "010D\r")
            await asyncio.sleep(0.02)
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                 
            await read_obd_data(client, "010B\r")
            await asyncio.sleep(0.1)

            await read_obd_data(client, "0105\r") # Engine Coolant Temp ## NEW
            await asyncio.sleep(0.02)
                
            await read_obd_data(client, "010E\r") # Timing Advance ## NEW
            await asyncio.sleep(0.02)

            await read_obd_data(client, "0144\r") # Commanded AFR ## NEW
            await asyncio.sleep(0.02)


            write_log_entry()

        # await read_obd_data(client, "0142\r")
        # await asyncio.sleep(0.2)
def start_datalogging():
    """Inicia uma nova gravação de log."""
    global datalog_active, csv_file, csv_writer
    if datalog_active: # Se já estiver gravando, não faz nada
        return

    datalog_active = True
    filename = f"datalog_{datetime.datetime.now().strftime('%Y%m%d_%H%M%S')}.csv"
    print(f"\n🟢 Datalog Iniciado Automaticamente. Salvando em: {filename}")
    csv_file = open(filename, 'w', newline='', encoding='utf-8')
    csv_writer = csv.writer(csv_file)
    csv_writer.writerow(['Timestamp', 'RPM', 'Speed_kmh', 'IAT_C', 'Fuel_LPH'])

def stop_datalogging():
    """Para a gravação de log atual."""
    global datalog_active, csv_file, csv_writer
    if not datalog_active: # Se não estiver gravando, não faz nada
        return

    datalog_active = False
    if csv_file:
        csv_file.close()
        csv_file = None
        csv_writer = None
        print("\n🔴 Datalog Interrompido. Arquivo salvo.")

async def main():
    while True:
        try:
            print("🔗 Tentando conectar ao OBD-II BLE...")
            async with BleakClient(DEVICE_ADDRESS) as client:
                if client.is_connected:
                    print("✅ Conectado ao OBD-II BLE!")

                    await client.start_notify(UUID_NOTIFY, notification_handler)
                    print("📡 Notificações ativadas!")
                    print("⌛ Aguardando 2 segundos para o adaptador estabilizar...")

                    await asyncio.sleep(2)
                    print("🚀 Iniciando loop de leitura de dados...")

                    await main_loop(client)

        except Exception as e:
            print(f"❌ Conexão perdida ou falhou: {e}")
            print("🔄 Tentando reconectar em 5 segundos...")
            await asyncio.sleep(5)


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\nPrograma encerrado pelo usuário.")
