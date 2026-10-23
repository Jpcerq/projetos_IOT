// ESP32-S3 - Robô Explorador - controle MQTT + sensores + telemetria Etapa 03
// Requer as bibliotecas PubSubClient, DHT sensor library (Adafruit) e UrlEncode.

#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <PubSubClient.h>
#include <UrlEncode.h>
#include <time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "DHT.h"
// Preencha com os mesmos dados de rede e API do seu sketch atual.
#define WIFI_SSID "SUA_REDE_WIFI"
#define WIFI_PASS "SUA_SENHA_WIFI"
#define API_URL "http://IP_DO_COMPUTADOR:5000/leituras"
#define CALLMEBOT_PHONE ""
#define CALLMEBOT_API_KEY ""

// ---------- MQTT: deve coincidir com o firmware do controle Velxio ----------
const char* MQTT_HOST = "broker.hivemq.com";
const int MQTT_PORT = 1883;
#define BASE_TOPICO "cimatec/iot/monitorargado"
const char* TOPICO_CMD = BASE_TOPICO "/robo/cmd";
const char* TOPICO_STATUS = BASE_TOPICO "/robo/status";

// ---------- Pinos do circuito físico enviado ----------
const int PIN_LED_VERDE = 15;
const int PIN_LED_VERMELHO = 16;
const int PIN_DHT = 17;
const int PIN_TRIG = 18;
const int PIN_ECHO = 8;
const int PIN_LDR = 7;
const int PIN_SERVO = 9;
const int PIN_JOY_X = 4;
const int PIN_JOY_SW = 5;

#define DHT_TIPO DHT11
DHT dht(PIN_DHT, DHT_TIPO);

const int LIMITE_LUZ = 2000;
const float LIMITE_DISTANCIA_CM = 50.0;

// ---------- Servo ----------
const int SERVO_FREQ = 50;
const int SERVO_BITS = 14;
const int PULSO_MIN_US = 500;
const int PULSO_MAX_US = 2400;
int anguloAtual = -1; // Alvo comandado; o circuito nao mede a posicao real.
bool servoConfigurado = false;

// Desativado para o joystick fisico nao sobrescrever os comandos do Velxio.
// O botao de emergencia do joystick continua ativo.
const bool HABILITAR_JOYSTICK_LOCAL_SERVO = false;

// ---------- Joystick local ----------
const int JOY_CENTRO = 2047;
const int JOY_TOLERANCIA = 400;
bool joystickNoComando = false;
unsigned long ultimaLeituraJoyMs = 0;

// ---------- Interrupção e estado ----------
volatile bool interrupcaoSolicitada = false;
volatile unsigned long ultimoDebounceMs = 0;
bool roboLigado = true;
bool emAlerta = false;
String comandoAtual = "PARADO";
String ultimoStatusPublicado = "";

unsigned long ultimoCmdMs = 0;
unsigned long ultimaLeituraMs = 0;
unsigned long ultimaTentativaMqtt = 0;
unsigned long ultimoAlertaMs = 0;
const unsigned long TIMEOUT_CMD = 1500;
const unsigned long INTERVALO_LEITURA = 2000;
const unsigned long INTERVALO_ALERTA = 60000;

WiFiClient clienteWifiMqtt;
PubSubClient mqtt(clienteWifiMqtt);

// ---------- Fila HTTP em tarefa separada ----------
// Assim, uma tentativa HTTP não interrompe mqtt.loop() no loop principal.
struct LeituraPendente {
  char timestamp[25];
  bool temTimestamp;
  float temperatura;
  float umidade;
  int luminosidade;
  int presenca;
  int probabilidade;
};

struct AlertaPendente {
  char mensagem[180];
};

const size_t TAMANHO_FILA_TELEMETRIA = 60;
QueueHandle_t filaTelemetria = nullptr;
QueueHandle_t filaAlertas = nullptr;

// ---------- Interrupção por hardware ----------
void IRAM_ATTR isrBotaoJoystick() {
  unsigned long agora = millis();
  if (agora - ultimoDebounceMs > 200) {
    interrupcaoSolicitada = true;
    ultimoDebounceMs = agora;
  }
}

float lerDistanciaCM() {
  digitalWrite(PIN_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_TRIG, LOW);

  long duracao = pulseIn(PIN_ECHO, HIGH, 30000);
  if (duracao == 0) return -1.0;
  return (duracao * 0.0343) / 2.0;
}

void escreverServo(int angulo) {
  if (!servoConfigurado) {
    Serial.println("[SERVO] PWM indisponivel: confira o erro de inicializacao.");
    return;
  }
  angulo = constrain(angulo, 0, 180);
  if (angulo == anguloAtual) return;
  long pulsoUs = map(angulo, 0, 180, PULSO_MIN_US, PULSO_MAX_US);
  uint32_t duty = (pulsoUs * ((1L << SERVO_BITS) - 1)) / 20000L;
  if (!ledcWrite(PIN_SERVO, duty)) {
    Serial.printf("[SERVO] ERRO ao escrever PWM no GPIO %d.\n", PIN_SERVO);
    return;
  }
  anguloAtual = angulo;
  Serial.printf("[SERVO] Alvo=%d graus | pulso=%ld us | GPIO=%d | duty=%lu\n",
                anguloAtual, pulsoUs, PIN_SERVO, (unsigned long)duty);
}

void soltarServo() {
  if (!servoConfigurado) return;
  if (!ledcWrite(PIN_SERVO, 0)) {
    Serial.println("[SERVO] ERRO ao desligar o sinal PWM.");
    return;
  }
  anguloAtual = -1;
  Serial.println("[SERVO] Sinal PWM desligado.");
}
void executarComando(const String& cmd) {
  if (!roboLigado) return;
  if (cmd == "Esquerda") escreverServo(0);
  else if (cmd == "Direita") escreverServo(180);
  else escreverServo(90);
}

void publicarStatusRobo() {
  if (!mqtt.connected()) return;

  const char* status = !roboLigado ? "DESLIGADO" : (emAlerta ? "ALERTA" : "LIGADO");
  if (ultimoStatusPublicado != status) {
    mqtt.publish(TOPICO_STATUS, status, true);
    ultimoStatusPublicado = status;
  }
}

void lerJoystickLocal() {
  if (!HABILITAR_JOYSTICK_LOCAL_SERVO) {
    joystickNoComando = false;
    return;
  }
  if (millis() - ultimaLeituraJoyMs < 20) return;
  ultimaLeituraJoyMs = millis();
  if (!roboLigado) return;

  int leitura = analogRead(PIN_JOY_X);
  bool fora = abs(leitura - JOY_CENTRO) > JOY_TOLERANCIA;
  if (fora) {
    int angulo = map(leitura, 0, 4095, 0, 180);
    if (anguloAtual < 0 || abs(angulo - anguloAtual) >= 2) escreverServo(angulo);
    joystickNoComando = true;
  } else if (joystickNoComando) {
    joystickNoComando = false;
    executarComando(comandoAtual);
  }
}

void aoReceberMqtt(char* topico, byte* payload, unsigned int tamanho) {
  if (String(topico) != TOPICO_CMD) return;
  String msg;
  for (unsigned int i = 0; i < tamanho; i++) msg += (char)payload[i];
  msg.trim();
  Serial.printf("[MQTT] Recebido=%s | robo=%s | joystickLocal=%s\n",
                msg.c_str(), roboLigado ? "LIGADO" : "DESLIGADO",
                joystickNoComando ? "ATIVO" : "INATIVO");

  if (msg == "DESLIGAR") {
    roboLigado = false;
    comandoAtual = "PARADO";
    soltarServo();
    Serial.println(">> Comando MQTT recebido: DESLIGAR");
    publicarStatusRobo();
  } else if (msg == "LIGAR") {
    roboLigado = true;
    comandoAtual = "PARADO";
    executarComando("PARADO");
    Serial.println(">> Comando MQTT recebido: LIGAR");
    publicarStatusRobo();
  } else if (roboLigado &&
             (msg == "Frente" || msg == "Tras" || msg == "Esquerda" ||
              msg == "Direita" || msg == "PARADO")) {
    if (msg != comandoAtual) {
      Serial.print(">> Comando MQTT recebido: ");
      Serial.println(msg);
    }
    comandoAtual = msg;
    ultimoCmdMs = millis();
    if (!joystickNoComando) executarComando(msg);
  }
}

void manterMqtt() {
  if (mqtt.connected()) return;
  comandoAtual = "PARADO";
  ultimoStatusPublicado = "";
  if (millis() - ultimaTentativaMqtt < 3000) return;
  ultimaTentativaMqtt = millis();

  String id = "robo-" + String((uint32_t)ESP.getEfuseMac(), HEX);
  Serial.print("Conectando ao broker MQTT... ");
  if (mqtt.connect(id.c_str(), TOPICO_STATUS, 1, true, "OFFLINE")) {
    Serial.println("OK");
    mqtt.subscribe(TOPICO_CMD);
    publicarStatusRobo();
  } else {
    Serial.print("falhou, rc=");
    Serial.println(mqtt.state());
  }
}

// ---------- CallMeBot em tarefa separada ----------
void enfileirarAlertaWhatsApp(const char* mensagem) {
  if (strlen(CALLMEBOT_PHONE) == 0 || strlen(CALLMEBOT_API_KEY) == 0) {
    Serial.println("CallMeBot sem configuração; alerta mantido no Serial.");
    return;
  }

  if (filaAlertas == nullptr) return;
  AlertaPendente alerta = {};
  snprintf(alerta.mensagem, sizeof(alerta.mensagem), "%s", mensagem);
  if (xQueueSend(filaAlertas, &alerta, 0) == pdTRUE) {
    Serial.println("Alerta CallMeBot enfileirado.");
  } else {
    Serial.println("Fila CallMeBot cheia; alerta não enfileirado.");
  }
}

void tarefaEnviarWhatsApp(void* parametro) {
  AlertaPendente alerta;
  for (;;) {
    if (xQueueReceive(filaAlertas, &alerta, portMAX_DELAY) != pdTRUE) continue;

    bool enviado = false;
    for (int tentativa = 1; tentativa <= 3 && !enviado; ++tentativa) {
      if (WiFi.status() != WL_CONNECTED) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        continue;
      }

      String url = String("https://api.callmebot.com/whatsapp.php?phone=") +
                   CALLMEBOT_PHONE + "&apikey=" + CALLMEBOT_API_KEY +
                   "&text=" + urlEncode(String(alerta.mensagem));
      WiFiClientSecure clienteSeguro;
      clienteSeguro.setInsecure();
      clienteSeguro.setTimeout(1500);
      HTTPClient http;
      http.setConnectTimeout(1500);
      http.setTimeout(1500);
      if (http.begin(clienteSeguro, url)) {
        int codigo = http.GET();
        http.end();
        if (codigo == 200) {
          Serial.println("CallMeBot: mensagem enviada.");
          enviado = true;
        } else {
          Serial.printf("CallMeBot: HTTP %d, tentativa %d de 3.\n",
                        codigo, tentativa);
        }
      }
      if (!enviado && tentativa < 3) vTaskDelay(pdMS_TO_TICKS(2000));
    }
    if (!enviado) Serial.println("CallMeBot: não foi possível enviar após 3 tentativas.");
  }
}

void enfileirarLeitura(float temperatura, float umidade, int luz,
                       bool presenca, int probabilidade) {
  if (filaTelemetria == nullptr) return;

  LeituraPendente leitura = {};
  leitura.temTimestamp = false;
  time_t agora = time(nullptr);
  if (agora > 1700000000) {
    struct tm utc;
    if (gmtime_r(&agora, &utc) != nullptr) {
      leitura.temTimestamp = strftime(leitura.timestamp,
                                     sizeof(leitura.timestamp),
                                     "%Y-%m-%dT%H:%M:%SZ", &utc) > 0;
    }
  }
  leitura.temperatura = temperatura;
  leitura.umidade = umidade;
  leitura.luminosidade = luz;
  leitura.presenca = presenca ? 1 : 0;
  leitura.probabilidade = probabilidade;

  if (xQueueSend(filaTelemetria, &leitura, 0) != pdTRUE) {
    LeituraPendente descartada;
    xQueueReceive(filaTelemetria, &descartada, 0);
    if (xQueueSend(filaTelemetria, &leitura, 0) == pdTRUE) {
      Serial.println("Telemetria: fila cheia; descartada a amostra mais antiga.");
    } else {
      Serial.println("Telemetria: fila cheia; amostra atual não enfileirada.");
    }
  }
}

void tarefaEnviarTelemetria(void* parametro) {
  LeituraPendente leitura;
  for (;;) {
    if (xQueueReceive(filaTelemetria, &leitura, portMAX_DELAY) != pdTRUE) continue;

    // Mantém a mesma amostra pendente até o backend confirmar HTTP 2xx.
    for (;;) {
      if (WiFi.status() != WL_CONNECTED) {
        vTaskDelay(pdMS_TO_TICKS(1500));
        continue;
      }

      char json[256];
      if (leitura.temTimestamp) {
        snprintf(json, sizeof(json),
                 "{\"timestamp\":\"%s\",\"temperatura_c\":%.1f,"
                 "\"umidade_pct\":%.1f,\"luminosidade\":%d,"
                 "\"presenca\":%d,\"probabilidade_vida\":%d}",
                 leitura.timestamp, leitura.temperatura, leitura.umidade,
                 leitura.luminosidade, leitura.presenca, leitura.probabilidade);
      } else {
        snprintf(json, sizeof(json),
                 "{\"temperatura_c\":%.1f,\"umidade_pct\":%.1f,"
                 "\"luminosidade\":%d,\"presenca\":%d,"
                 "\"probabilidade_vida\":%d}",
                 leitura.temperatura, leitura.umidade, leitura.luminosidade,
                 leitura.presenca, leitura.probabilidade);
      }

      WiFiClient clienteHttp;
      HTTPClient http;
      http.setConnectTimeout(1000);
      http.setTimeout(1200);
      if (!http.begin(clienteHttp, String(API_URL))) {
        Serial.println("Telemetria: não iniciou HTTP; nova tentativa em 2 s.");
        vTaskDelay(pdMS_TO_TICKS(2000));
        continue;
      }

      http.addHeader("Content-Type", "application/json");
      int status = http.POST(reinterpret_cast<uint8_t*>(json), strlen(json));
      String resposta = status > 0 ? http.getString() : String();
      http.end();

      if (status >= 200 && status < 300) {
        Serial.printf("Telemetria salva (HTTP %d): %s\n", status,
                      resposta.c_str());
        break;
      }
      Serial.printf("Telemetria falhou (HTTP %d); amostra será reenviada.\n",
                    status);
      if (resposta.length() > 0) Serial.println(resposta);
      vTaskDelay(pdMS_TO_TICKS(2000));
    }
  }
}

void lerSensoresEDecidir() {
  float temperatura = dht.readTemperature();
  float umidade = dht.readHumidity();
  int luz = analogRead(PIN_LDR);
  float distancia = lerDistanciaCM();

  bool presenca = distancia > 0 && distancia <= LIMITE_DISTANCIA_CM;
  bool dhtOk = !isnan(temperatura) && !isnan(umidade);

  int prob = 0;
  if (dhtOk && temperatura >= 15 && temperatura <= 30) prob += 25;
  if (dhtOk && umidade >= 40 && umidade <= 70) prob += 25;
  if (luz > LIMITE_LUZ) prob += 20;
  if (presenca) prob += 30;

  bool alertaAgora = roboLigado && prob > 75;

  Serial.println("--------------------------------");
  if (dhtOk) {
    Serial.print("Temperatura: "); Serial.print(temperatura, 1); Serial.println(" C");
    Serial.print("Umidade: "); Serial.print(umidade, 1); Serial.println(" %");
  } else {
    Serial.println("ERRO: falha na leitura do DHT (confira tipo, pino e fiacao)");
  }
  Serial.print("Luz (ADC 0-4095): "); Serial.println(luz);
  if (distancia > 0) {
    Serial.print("Distancia: "); Serial.print(distancia, 1); Serial.println(" cm");
  } else {
    Serial.println("Distancia: Fora de alcance");
  }
  Serial.println(presenca ? "Presenca detectada (objeto proximo)" : "Sem presenca proxima");
  Serial.print("Alvo do servo (sem medir posicao real): ");
  if (anguloAtual < 0) Serial.println("solto"); else Serial.println(anguloAtual);
  Serial.print("Estado do robo: ");
  Serial.println(!roboLigado ? "DESLIGADO" : (alertaAgora ? "ALERTA" : "LIGADO"));
  Serial.print("Probabilidade de vida: "); Serial.print(prob); Serial.println("%");

  bool vermelho = !roboLigado || alertaAgora;
  digitalWrite(PIN_LED_VERMELHO, vermelho ? HIGH : LOW);
  digitalWrite(PIN_LED_VERDE, vermelho ? LOW : HIGH);

  if (alertaAgora) {
    Serial.println("ALERTA! Alta probabilidade de vida detectada!");
  } else if (roboLigado) {
    Serial.println("Exploracao normal. Nenhum indicio relevante detectado.");
  }

  // A API requer temperatura e umidade válidas; se o DHT falhar, não envia
  // uma amostra com NaN nem grava dados falsos no banco.
  if (dhtOk) {
    enfileirarLeitura(temperatura, umidade, luz, presenca, prob);
  } else {
    Serial.println("Telemetria: amostra ignorada porque o DHT falhou.");
  }

  bool podeEnviar = ultimoAlertaMs == 0 ||
                    millis() - ultimoAlertaMs > INTERVALO_ALERTA;
  if (alertaAgora && !emAlerta && podeEnviar) {
    enfileirarAlertaWhatsApp("Alerta! Alta probabilidade de vida detectada no planeta.");
    ultimoAlertaMs = millis();
  }

  bool mudouAlerta = (emAlerta != alertaAgora);
  emAlerta = alertaAgora;
  if (mudouAlerta) publicarStatusRobo();
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(PIN_LED_VERDE, OUTPUT);
  pinMode(PIN_LED_VERMELHO, OUTPUT);
  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);
  pinMode(PIN_JOY_X, INPUT);
  pinMode(PIN_JOY_SW, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_JOY_SW), isrBotaoJoystick, FALLING);

  digitalWrite(PIN_LED_VERDE, HIGH);
  digitalWrite(PIN_LED_VERMELHO, LOW);

  servoConfigurado = ledcAttach(PIN_SERVO, SERVO_FREQ, SERVO_BITS);
  if (!servoConfigurado) {
    Serial.println("ERRO: nao foi possivel configurar o PWM do servo");
  }
  Serial.printf("[PWM] GPIO=%d | configurado=%s | frequencia=%d Hz\n",
                PIN_SERVO, servoConfigurado ? "SIM" : "NAO", SERVO_FREQ);
  Serial.printf("[CONTROLE] Joystick local do servo: %s\n",
                HABILITAR_JOYSTICK_LOCAL_SERVO ? "HABILITADO" : "DESATIVADO");
  escreverServo(90);
  dht.begin();

  filaTelemetria = xQueueCreate(TAMANHO_FILA_TELEMETRIA,
                               sizeof(LeituraPendente));
  filaAlertas = xQueueCreate(3, sizeof(AlertaPendente));
  if (filaTelemetria == nullptr) {
    Serial.println("ERRO: não foi possível criar a fila HTTP de telemetria.");
  }

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  Serial.print("Conectando ao Wi-Fi");
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.print(" conectado! IP: ");
  Serial.println(WiFi.localIP());
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");

  if (filaTelemetria != nullptr) {
    BaseType_t criado = xTaskCreate(tarefaEnviarTelemetria,
                                    "telemetriaHTTP", 8192, nullptr, 1, nullptr);
    if (criado != pdPASS) {
      Serial.println("ERRO: não foi possível iniciar a tarefa HTTP.");
    }
  }
  if (filaAlertas != nullptr) {
    BaseType_t criado = xTaskCreate(tarefaEnviarWhatsApp,
                                    "alertaWhatsApp", 8192, nullptr, 1, nullptr);
    if (criado != pdPASS) {
      Serial.println("ERRO: não foi possível iniciar a tarefa CallMeBot.");
    }
  }

  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(aoReceberMqtt);
  mqtt.setKeepAlive(15);
  mqtt.setSocketTimeout(2);
  Serial.println("Robô inicializado. Aguardando comandos MQTT e sensores.");
}

void loop() {
  if (interrupcaoSolicitada) {
    interrupcaoSolicitada = false;
    roboLigado = !roboLigado;
    if (!roboLigado) {
      comandoAtual = "PARADO";
      soltarServo();
      Serial.println("\n>> [EMERGÊNCIA] interrupção ativada no joystick local");
    } else {
      executarComando("PARADO");
      Serial.println("\n>> Robô reativado pelo joystick local");
    }
    publicarStatusRobo();
  }

  manterMqtt();
  mqtt.loop();
  lerJoystickLocal();

  if (roboLigado && comandoAtual != "PARADO" &&
      millis() - ultimoCmdMs > TIMEOUT_CMD) {
    comandoAtual = "PARADO";
    if (!joystickNoComando) executarComando("PARADO");
    Serial.println(">> Sem comandos do controle remoto (failsafe)");
  }

  if (millis() - ultimaLeituraMs >= INTERVALO_LEITURA) {
    ultimaLeituraMs = millis();
    lerSensoresEDecidir();
  }
}

