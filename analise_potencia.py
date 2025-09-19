import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import sys
from scipy.signal import savgol_filter 

MASSA_TOTAL_KG = 1060.0       # Peso do carro + ocupantes + combustível (em kg)
AREA_FRONTAL_M2 = 2.08        # Área frontal do carro (em m²)
COEF_ARRASTO_CD = 0.367        # Coeficiente de arrasto aerodinâmico (Cd)
COEF_ATRITO_ROLAMENTO = 0.015 # Coeficiente de atrito de rolamento (pneus)
RAIO_RODA_M = 0.301           # Raio da roda com pneu (em metros)

# Valores de exemplo para um VW Up! TSI
RELACAO_DIFERENCIAL = 3.625
RELACAO_MARCHAS = {
    1: 3.77,
    2: 2.12,
    3: 1.36,
    4: 1.03,
    5: 0.81
}

# Constantes Físicas
DENSIDADE_AR_KG_M3 = 1.225
G_ACEL_GRAVIDADE = 9.81

def calcular_potencia_torque(dados, marcha_usada):
    """Calcula a potência e o torque estimados a partir dos dados do log."""
    
    dados = dados.copy()
    
    tempo_inicio = dados.index[0]
    tempo_fim = dados.index[-1]
    novo_indice_tempo = pd.to_datetime(np.arange(tempo_inicio.value, tempo_fim.value, 10_000_000))
    dados = dados.reindex(dados.index.union(novo_indice_tempo)).interpolate('time').loc[novo_indice_tempo]

    dados['Velocidade_ms'] = dados['Speed_kmh'] / 3.6
    
    tempo_em_segundos = dados.index.astype(np.int64) / 10**9
    delta_tempo_s = np.diff(tempo_em_segundos, prepend=tempo_em_segundos[0])
    delta_velocidade_ms = np.diff(dados['Velocidade_ms'], prepend=dados['Velocidade_ms'].iloc[0])
    
    delta_tempo_s[delta_tempo_s == 0] = np.nan
    
    dados['Aceleracao_ms2'] = delta_velocidade_ms / delta_tempo_s
    
    # Suavização da aceleração
    dados['Aceleracao_suave_ms2'] = dados['Aceleracao_ms2'].rolling(window=80, center=True, min_periods=1).mean()
    
    # Cálculos de Força
    dados['Forca_Arrasto_N'] = 0.5 * DENSIDADE_AR_KG_M3 * AREA_FRONTAL_M2 * COEF_ARRASTO_CD * (dados['Velocidade_ms'] ** 2)
    dados['Forca_Atrito_N'] = COEF_ATRITO_ROLAMENTO * MASSA_TOTAL_KG * G_ACEL_GRAVIDADE
    forca_inercial = MASSA_TOTAL_KG * dados['Aceleracao_suave_ms2']
    dados['Forca_Motor_N'] = forca_inercial + dados['Forca_Arrasto_N'] + dados['Forca_Atrito_N']
    
    # Cálculo de Potência
    potencia_watts = dados['Forca_Motor_N'] * dados['Velocidade_ms']
    dados['Potencia_cv'] = potencia_watts / 735.5
    
    # cálculo do Torque do motor
    torque_na_roda_Nm = dados['Forca_Motor_N'] * RAIO_RODA_M
    relacao_total = RELACAO_DIFERENCIAL * RELACAO_MARCHAS[marcha_usada]
    dados['Torque_Motor_Nm'] = torque_na_roda_Nm / relacao_total
    
    return dados

def plotar_grafico_dino(dados, nome_arquivo):
    """Gera um gráfico estilo dinamômetro com Potência e Torque vs RPM."""
    
    # Remove valores negativos/irreais e acelerações negativas
    dados_plot = dados[(dados['Potencia_cv'] > 0) & 
                       (dados['RPM'] > 1500) & 
                       (dados['Aceleracao_suave_ms2'] > 0.2)].copy()

    if dados_plot.empty:
        print("⚠️ Não há dados suficientes para plotar o gráfico após a filtragem.")
        return

    # Aplica um filtro suave nas curvas finais para um visual limpo
    dados_plot['Potencia_suave_cv'] = savgol_filter(dados_plot['Potencia_cv'], 21, 3)
    dados_plot['Torque_suave_Nm'] = savgol_filter(dados_plot['Torque_Motor_Nm'], 21, 3)
    
    pico_potencia = dados_plot.loc[dados_plot['Potencia_suave_cv'].idxmax()]
    pico_torque = dados_plot.loc[dados_plot['Torque_suave_Nm'].idxmax()]

    fig, ax1 = plt.subplots(figsize=(12, 7))
    fig.suptitle(f'Curva de Potência e Torque Estimados - {nome_arquivo}', fontsize=16)

    color = 'tab:red'
    ax1.set_xlabel('RPM do Motor')
    ax1.set_ylabel('Potência (cv)', color=color)
    ax1.plot(dados_plot['RPM'], dados_plot['Potencia_suave_cv'], color=color) # Plota a curva suave
    ax1.tick_params(axis='y', labelcolor=color)
    ax1.grid(True, linestyle='--', alpha=0.6)
    ax1.annotate(f'Pico: {pico_potencia["Potencia_suave_cv"]:.1f} cv @ {pico_potencia["RPM"]:.0f} RPM',
                 xy=(pico_potencia['RPM'], pico_potencia['Potencia_suave_cv']),
                 xytext=(pico_potencia['RPM'], pico_potencia['Potencia_suave_cv'] + 5),
                 arrowprops=dict(facecolor='red', shrink=0.05), ha='center')

    ax2 = ax1.twinx()
    color = 'tab:blue'
    ax2.set_ylabel('Torque do Motor (N.m)', color=color) # Label corrigido
    ax2.plot(dados_plot['RPM'], dados_plot['Torque_suave_Nm'], color=color) # Plota a curva suave
    ax2.tick_params(axis='y', labelcolor=color)
    ax2.annotate(f'Pico: {pico_torque["Torque_suave_Nm"]:.1f} N.m @ {pico_torque["RPM"]:.0f} RPM',
                 xy=(pico_torque['RPM'], pico_torque['Torque_suave_Nm']),
                 xytext=(pico_torque['RPM'], pico_torque['Torque_suave_Nm'] - 15),
                 arrowprops=dict(facecolor='blue', shrink=0.05), ha='center')
    
    plt.show()

if __name__ == "__main__":
    nome_arquivo = input("Digite o nome do arquivo de log (ex: datalog.csv): ")
    try:
        dados_brutos = pd.read_csv(nome_arquivo)
        dados_brutos['Timestamp'] = pd.to_datetime(dados_brutos['Timestamp'], errors='coerce')
        dados_brutos.dropna(subset=['Timestamp'], inplace=True)
        dados_brutos.set_index('Timestamp', inplace=True)
        
        print("\nPara uma análise precisa, analise uma puxada em UMA ÚNICA MARCHA.")
        
        marcha = int(input("Qual marcha foi usada na puxada (2, 3, 4, etc.)? "))
        if marcha not in RELACAO_MARCHAS:
            print("❌ Marcha inválida!")
        else:
            rpm_inicio = int(input("Digite o RPM inicial da puxada (ex: 2000): "))
            rpm_fim = int(input("Digite o RPM final da puxada (ex: 6500): "))
            
            dados_puxada = dados_brutos[(dados_brutos['RPM'] >= rpm_inicio) & (dados_brutos['RPM'] <= rpm_fim)].copy()
            
            if dados_puxada.empty:
                print("Nenhum dado encontrado nesse intervalo de RPM.")
            else:
                dados_calculados = calcular_potencia_torque(dados_puxada, marcha)
                plotar_grafico_dino(dados_calculados, nome_arquivo)

    except FileNotFoundError:
        print(f"❌ Erro: Arquivo '{nome_arquivo}' não encontrado.")
    except Exception as e:
        print(f"Ocorreu um erro inesperado: {e}")