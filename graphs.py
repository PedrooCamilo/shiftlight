import pandas as pd
import matplotlib.pyplot as plt
import sys

def analisar_log(nome_arquivo):
    """
    Lê um arquivo de log CSV e gera um painel de gráficos de desempenho.
    """
    try:
        # Lê o arquivo CSV usando o Pandas
        # O parse_dates=[0] diz ao pandas para tratar a primeira coluna como data/hora
        dados = pd.read_csv(nome_arquivo, parse_dates=[0])
    except FileNotFoundError:
        print(f"❌ Erro: Arquivo '{nome_arquivo}' não encontrado.")
        print("Verifique se o nome está correto e se ele está na mesma pasta do script.")
        return

    # Define a coluna 'Timestamp' como o índice do nosso conjunto de dados
    dados.set_index('Timestamp', inplace=True)

    # Cria uma figura e um conjunto de subplots. 4 gráficos, 1 coluna.
    # sharex=True faz com que todos os gráficos compartilhem o mesmo eixo X (tempo)
    fig, (ax1, ax2, ax3, ax4) = plt.subplots(4, 1, figsize=(12, 10), sharex=True)
    fig.suptitle(f'Análise de Performance - {nome_arquivo}', fontsize=16)

    # Gráfico 1: RPM vs. Tempo
    ax1.plot(dados.index, dados['RPM'], color='blue', label='RPM')
    ax1.set_ylabel('RPM')
    ax1.grid(True)
    ax1.legend()

    # Gráfico 2: Velocidade vs. Tempo
    ax2.plot(dados.index, dados['Speed_kmh'], color='green', label='Velocidade (km/h)')
    ax2.set_ylabel('Velocidade (km/h)')
    ax2.grid(True)
    ax2.legend()

    # Gráfico 3: IAT vs. Tempo
    ax3.plot(dados.index, dados['IAT_C'], color='red', label='Temp. Admissão (°C)')
    ax3.set_ylabel('IAT (°C)')
    ax3.grid(True)
    ax3.legend()

    # Gráfico 4: Consumo vs. Tempo
    ax4.plot(dados.index, dados['Fuel_LPH'], color='purple', label='Consumo (L/h)')
    ax4.set_ylabel('Consumo (L/h)')
    ax4.set_xlabel('Tempo')
    ax4.grid(True)
    ax4.legend()

    # Ajusta o layout para não sobrepor os títulos e exibe o gráfico
    plt.tight_layout(rect=[0, 0, 1, 0.96])
    plt.show()

if __name__ == "__main__":
    if len(sys.argv) > 1:
        # Se um nome de arquivo foi passado como argumento na linha de comando
        arquivo = sys.argv[1]
    else:
        # Pede ao usuário para digitar o nome do arquivo
        arquivo = input("Digite o nome do arquivo de log a ser analisado (ex: datalog_20250913_181530.csv): ")
    
    analisar_log(arquivo)