#este arquivo tem como objetivo solicitar o RPM para o OBD2 e enviar para bitdoglab

import asyncio
import serial  # lib para comunicação serial
from bleak import BleakClient #lib para facilitar comunicação com dispositivos BLE

DEVICE_ADDRESS = "88:1B:99:67:5B:38"
UUID_WRITE = "0000fff2-0000-1000-8000-00805f9b34fb"
UUID_NOTIFY = "0000fff1-0000-1000-8000-00805f9b34fb"

# Configuração da porta serial para Raspberry Pi Pico 
SERIAL_PORT = "COM4"  # Porta para a comunicação serial
BAUD_RATE = 115200  # velocidade de transmissão

# Abre a porta serial
try:
    ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
    print(f"✅ Serial conectado em {SERIAL_PORT} a {BAUD_RATE} baud")
except Exception as e:
    print(f"❌ Erro ao abrir a porta serial: {e}")
    ser = None  # Evita falha no código se a serial não abrir

last_rpm = None  # Armazena a última leitura de RPM

#transforma hexa pra decimal
def ascii_to_decimal(ascii_str):
    """Converte string ASCII contendo valores hexadecimais em decimais."""
    try:
        ascii_values = ascii_str.split()
        decimal_values = [int(value, 16) for value in ascii_values]
        return decimal_values
    except ValueError:
        return []

#faz tratamento de valores invalidos e transfoma hexa para decimal
def hex_to_ascii(hex_string):
    """Converte string hexadecimal em ASCII, ignorando valores inválidos."""
    try:
        ascii_string = bytes.fromhex(hex_string).decode('utf-8')
        return ascii_string
    except ValueError:
        return ""

# Para funcionalidades futuras!!
def pressure_to_ascii(hex_string):
    """Converte string hexadecimal em ASCII, ignorando valores inválidos."""
    if not hex_string:  # Verifica se a string está vazia
        print("⚠️ hex_to_ascii recebeu uma string vazia!")
        return ""

    try:
        ascii_string = bytes.fromhex(hex_string).decode('utf-8')
        return ascii_string
    except ValueError as e:
        print(f"⚠️ Erro ao converter hex para ASCII: {e} | Entrada: {hex_string}")
        return ""

# Para funcionalidades futuras!!
def parse_pressure(data):
    """Processa a resposta OBD-II e extrai a pressão do turbo em PSI."""
    if(data != "b'\r'"):
        try:
            hex_pressure= "".join(f"{b:02X}" for b in data if b not in (0x3E, 0x0D, 0x0A))
            
            if hex_pressure.startswith("410B") and len(hex_pressure) >= 4:
                A = int(hex_pressure[4:6], 16)
                pressure_kpa = A  # Pressão absoluta do coletor em kPa
                
                # Calcula a pressão relativa do turbo (boost pressure) subtraindo a pressão atmosférica
                boost_kpa = pressure_kpa - 101.3
                boost_psi = boost_kpa * 0.145  # Conversão para PSI
                
                print(f"🛠️ Pressão do Turbo: {boost_psi:.2f} PSI")
                send_pressure_serial(boost_psi)  # Envia via Serial
                return boost_psi
            else: 
                hex_pressure = hex_pressure[12:]
                pressure_ascii = pressure_to_ascii(hex_pressure)
                valores_pressure = ascii_to_decimal(pressure_ascii)
                pressure = valores_pressure[0] * 0.145
                print(f"🛠️ valor final da pressão {pressure}")
                ##send_pressure_serial(pressure)
        except Exception as e:
            print("")

# Para funcionalidades futuras!!
def send_pressure_serial(pressure):
    """Envia a pressão do turbo (em PSI) via Serial para a Raspberry Pi Pico."""
    if ser and ser.is_open:
        try:
            pressure_str = f"{pressure:.2f}\n"  # Formata o float com 2 casas decimais
           ## ser.write(f"{pressure:.2f}\n".encode())  # Converte para bytes e envia
           ## print(f"📤 Enviado via Serial: {pressure_str.strip()}")
        except Exception as e:
            print(f"❌ Erro ao enviar pressão via Serial: {e}")

#Faz o tratamento dos valores obtidos pelo OBD2
def parse_rpm(data):
    """Processa a resposta OBD-II e extrai o valor do RPM."""
    
    global last_rpm
    try:
            hex_response = "".join(f"{b:02X}" for b in data if b not in (0x3E, 0x0D, 0x0A))

            hex_filtered = hex_response[12:]
            rpm_ascii = hex_to_ascii(hex_filtered)
            valores_rpm = ascii_to_decimal(rpm_ascii)

            if len(valores_rpm) >= 2:
                rpm = ((valores_rpm[0] * 256) + valores_rpm[1]) / 4
                print(f"🚗 RPM: {rpm} RPM")
                last_rpm = rpm
                send_rpm_serial(rpm)  # Envia via Serial

    except Exception as e:
        print(f"❌ Erro ao processar RPM: {e}")

#Envia via COM4 os dados processados do RPM
def send_rpm_serial(rpm):
    """Envia o valor do RPM via Serial para a Raspberry Pi Pico."""
    if ser and ser.is_open:
        try:            
            ser.write(f"{int(rpm)}\n".encode())  # Envia os dados pela porta serial
            print(f"📤 Enviado via Serial: {f"{int(rpm)}\n".strip()}")
        except Exception as e:
            print(f"❌ Erro ao enviar dados via Serial: {e}")

#Executa as funções relacionadas ao tratamento do RPM e nao deixa passar dados fora do padrão
def notification_handler(sender, data):
    
    """Manipula os dados recebidos via notificação BLE."""
    if not data or data == b'\r' or data == b'\r>' or (data == (b'010C\r') or (data == (b'010B\r'))):  # Garante que há dados válidos antes de processar
        return
    parse_rpm(data)
    ##parse_pressure(data)

#Envia o comando de solicitar RPM ao OBD2
async def read_obd_data(client, command):
    """Envia comando OBD-II e aguarda resposta."""
    await client.write_gatt_char(UUID_WRITE, command.encode())
    await asyncio.sleep(1)  # Dá tempo para processar a resposta

#Loop da leitura
async def continuous_rpm_read(client):
    """Lê dados do OBD-II de forma ordenada."""
    while True:
       ## await read_obd_data(client, "010B\r")  # Pressão do turbo
        await read_obd_data(client, "010C\r")  # RPM
        
        
        
#Informa se as conexões deram certo e roda o código principal
async def main():
    print("🔗 Conectando ao OBD-II BLE...")
    async with BleakClient(DEVICE_ADDRESS) as client:
        print("✅ Conectado ao OBD-II BLE!")
        await client.start_notify(UUID_NOTIFY, notification_handler)
        print("📡 Notificações ativadas!")

        # Inicia a leitura contínua do RPM
        await continuous_rpm_read(client)

if __name__ == "__main__":
    asyncio.run(main())