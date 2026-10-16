# Robô Explorador Espacial - Controle Remoto (Etapa 01)

## Nome do Projeto
Robô Explorador Espacial — Controle Remoto Simulado (Etapa 01)

---

## Objetivo da Etapa
O objetivo desta etapa é implementar e simular o firmware de controle remoto para um robô explorador espacial utilizando o microcontrolador ESP32-S3.

A etapa contempla:
* Leitura analógica dos eixos do joystick para comandos de movimentação (Frente, Tras, Esquerda, Direita).
* Leitura de botão de alternância com tratamento de debounce não-bloqueante para ligar e desligar remotamente o robô.
* Sinalização visual por LEDs referentes ao estado de comunicação do controle.
* Emissão de logs de status e comandos via Monitor Serial (115200 baud).

---

## Lista de Componentes do Circuito
Abaixo estão os componentes utilizados na simulação do circuito do controle remoto:

* Microcontrolador ESP32-S3: Unidade central de processamento da lógica do controle.
* Joystick Analógico:
  * Eixo X (Horizontal): Leitura do eixo analógico para virar à esquerda/direita.
  * Eixo Y (Vertical): Leitura do eixo analógico para avançar/recuar.
  * Botão SEL: Botão integrado ao joystick.
* Botão de Alternância (Toggle Button / Pushbutton): Utilizado para desligar e religar o robô remotamente.
* LED Verde: Indicador de status (Robô ligado e comunicação ativa).
* LED Vermelho: Indicador de status (Robô desligado ou desconectado).
* Resistores (220 Ω): Para limitação de corrente dos LEDs.
* Protoboard e Jumpers: Para conexões físicas e simuladas.

### Mapeamento dos Pinos (GPIOs)
| Componente | Pino ESP32-S3 | Tipo de Pino | Descrição / Função |
|---|---|---|---|
| Eixo X (Joystick) | GPIO 4 | Entrada Analógica (ADC1_CH3) | Leitura de direção (Esquerda / Direita) |
| Eixo Y (Joystick) | GPIO 5 | Entrada Analógica (ADC1_CH4) | Leitura de direção (Frente / Trás) |
| Botão SEL (Joystick) | GPIO 6 | Entrada Digital (INPUT_PULLUP) | Botão de seleção do joystick |
| Botão Toggle | GPIO 14 | Entrada Digital (INPUT_PULLUP) | Botão de controle de energia (Ligar/Desligar) |
| LED Verde | GPIO 15 | Saída Digital (OUTPUT) | Status: Robô LIGADO |
| LED Vermelho | GPIO 16 | Saída Digital (OUTPUT) | Status: Robô DESLIGADO |

---

## Como Rodar no Velxio / Wokwi

Siga os passos abaixo para testar a simulação do circuito na plataforma Velxio / Wokwi:

1. Acessar a Plataforma:
   * Abra a plataforma de simulação Velxio / Wokwi no seu navegador.
2. Abrir o Projeto:
   * Acesse o link do projeto hospedado no Velxio / Wokwi: [COLE_SEU_LINK_AQUI].
3. Carregar o Código:
   * Copie o código-fonte C++ (.ino / sketch.ino) fornecido no repositório e cole na aba de código do editor.
4. Iniciar a Simulação:
   * Clique no botão Play / Start Simulation para iniciar a execução no ESP32-S3.
5. Abrir o Monitor Serial:
   * Abra o Monitor Serial dentro do simulador (configurado em 115200 baud) para acompanhar as mensagens emitidas.
6. Interagir com o Circuito:
   * Movimentação: Mova o joystick analógico para visualizar os comandos (Frente, Tras, Esquerda, Direita, PARADO) no Monitor Serial.
   * Desligamento Remoto: Pressione o botão toggle conectado à GPIO 14. O LED Verde irá apagar, o LED Vermelho irá acender, e a mensagem "Comando enviado: DESLIGAR" será exibida.
   * Religação: Pressione novamente o botão toggle para reativar o controle e retomar o envio de comandos.