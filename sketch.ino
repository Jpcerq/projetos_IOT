// ESP32-S3 - Controle Remoto do Robo (Etapa 01)

// Mapeamento de pinos para ESP32-S3
const int PIN_LED_VERDE = 15;       // Status: Ligado / Conectado
const int PIN_LED_VERMELHO = 16;    // Status: Desligado / Desconectado
const int PIN_BUTTON_TOGGLE = 14;   // Botao de Desligar Remoto
const int PIN_HOR = 4;              // Eixo X do Joystick (ADC1_CH3)
const int PIN_VER = 5;              // Eixo Y do Joystick (ADC1_CH4)
const int PIN_SEL = 6;              // Botao Selecionar do Joystick

// Limites do ADC (12 bits: 0 a 4095)
const int CENTRO = 2047;
const int TOLERANCIA = 500; // Zona morta para estabilizar comandos

// Variaveis de estado
bool roboLigado = true;
String ultimoComando = "";

// Variaveis para debounce e detecao de flanco do botao
bool ultimoEstadoLeitura = HIGH;
bool estadoEstavelBotao = HIGH;
unsigned long ultimoTempoDebounce = 0;
const unsigned long atrasoDebounce = 50; 

void setup() {
  Serial.begin(115200);
  delay(1000); // Aguarda inicializacao do CDC USB nativo do ESP32-S3

  // Configuracao dos pinos
  pinMode(PIN_LED_VERDE, OUTPUT);
  pinMode(PIN_LED_VERMELHO, OUTPUT);
  
  pinMode(PIN_BUTTON_TOGGLE, INPUT_PULLUP); 
  pinMode(PIN_HOR, INPUT);
  pinMode(PIN_VER, INPUT);
  pinMode(PIN_SEL, INPUT_PULLUP);

  // Estado inicial dos LEDs (Controle iniciado e ligado)
  digitalWrite(PIN_LED_VERDE, HIGH);   
  digitalWrite(PIN_LED_VERMELHO, LOW);

  ultimoEstadoLeitura = digitalRead(PIN_BUTTON_TOGGLE);
  estadoEstavelBotao = ultimoEstadoLeitura;

  Serial.println("--- CONTROLE REMOTO INICIALIZADO ---");
  Serial.println("Status: Robo LIGADO (LED Verde ACESO)");
}

void loop() {
  // 1. Verificacao do Botao de Desligar Remoto (Com Debounce Nao-Bloqueante)
  bool leituraAtual = digitalRead(PIN_BUTTON_TOGGLE);

  if (leituraAtual != ultimoEstadoLeitura) {
    ultimoTempoDebounce = millis();
  }

  if ((millis() - ultimoTempoDebounce) > atrasoDebounce) {
    if (leituraAtual != estadoEstavelBotao) {
      estadoEstavelBotao = leituraAtual;

      // Executa apenas ao pressionar o botao (transicao para LOW)
      if (estadoEstavelBotao == LOW) {
        roboLigado = !roboLigado; // Alterna entre ligado e desligado

        if (roboLigado) {
          // Robo Religado
          digitalWrite(PIN_LED_VERDE, HIGH);
          digitalWrite(PIN_LED_VERMELHO, LOW);
          
          ultimoComando = ""; 
          Serial.println("Comando enviado: LIGAR");
          Serial.println("Status: Robo LIGADO (LED Verde ACESO)");
        } else {
          // Robo Desligado
          digitalWrite(PIN_LED_VERDE, LOW);
          digitalWrite(PIN_LED_VERMELHO, HIGH);
          
          ultimoComando = "DESLIGAR";
          Serial.println("Comando enviado: DESLIGAR"); // Exigencia do edital
          Serial.println("Status: Robo DESLIGADO (LED Vermelho ACESO)");
        }
      }
    }
  }

  ultimoEstadoLeitura = leituraAtual;

  // 2. Leitura e Envio dos Comandos do Joystick (Apenas se o robo estiver LIGADO)
  if (roboLigado) {
    int valorX = analogRead(PIN_HOR); 
    int valorY = analogRead(PIN_VER); 
    String comandoAtual = "PARADO";

    // Mapeamento das 4 direcoes
    if (valorY > (CENTRO + TOLERANCIA)) {
      comandoAtual = "Frente";
    } 
    else if (valorY < (CENTRO - TOLERANCIA)) {
      comandoAtual = "Tras";
    } 
    else if (valorX > (CENTRO + TOLERANCIA)) {
      comandoAtual = "Direita";
    } 
    else if (valorX < (CENTRO - TOLERANCIA)) {
      comandoAtual = "Esquerda";
    }

    // Exibe no monitor serial somente se o comando tiver mudado
    if (comandoAtual != ultimoComando) {
      ultimoComando = comandoAtual;
      Serial.print("Comando: ");
      Serial.println(ultimoComando);
    }
  }

  delay(20);
}