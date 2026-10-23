// Controle ESP32-S3 no Velxio para o robo fisico.
// Requer a biblioteca PubSubClient. Nao requer secrets.h.
// Compativel com robo_fisico.ino: movimento reenviado a cada 500 ms.

#include <WiFi.h>
#include <PubSubClient.h>

const char* WIFI_SSID = "Velxio-GUEST";
const char* MQTT_BROKER = "broker.hivemq.com";
const int MQTT_PORT = 1883;
const char* TOPICO_COMANDO = "cimatec/iot/monitorargado/robo/cmd";

WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

const int PIN_LED_VERDE = 15;
const int PIN_LED_VERMELHO = 16;
const int PIN_BUTTON_TOGGLE = 14;
const int PIN_HOR = 4;
const int PIN_VER = 5;
const int PIN_SEL = 6;

const int CENTRO = 2047;
const int TOLERANCIA = 500;
const unsigned long ATRASO_DEBOUNCE = 50;
const unsigned long INTERVALO_RECONEXAO_MQTT = 2000;
const unsigned long INTERVALO_REENVIO_JOYSTICK = 500;

bool roboLigado = true;
bool ultimoEstadoLeitura = HIGH;
bool estadoEstavelBotao = HIGH;
unsigned long ultimoTempoDebounce = 0;
unsigned long ultimaTentativaMQTT = 0;
unsigned long ultimoEnvioJoystick = 0;
String ultimoComandoJoystick = "";

void enviarComando(const char* comando) {
  if (mqtt.connected()) {
    bool enviado = mqtt.publish(TOPICO_COMANDO, comando);
    Serial.print(enviado ? "MQTT enviado: " : "Falha MQTT: ");
    Serial.println(comando);
  } else {
    Serial.print("MQTT desconectado. Comando nao enviado: ");
    Serial.println(comando);
  }
}

void conectarWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID);
  Serial.print("Conectando ao Wi-Fi Velxio");
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }
  Serial.println("\nWi-Fi conectado!");
  Serial.println(WiFi.localIP());
}

void reconectarMQTT() {
  if (mqtt.connected()) return;
  if (millis() - ultimaTentativaMQTT < INTERVALO_RECONEXAO_MQTT) return;
  ultimaTentativaMQTT = millis();
  String id = "controle-robo-" + String((uint32_t)ESP.getEfuseMac(), HEX);
  Serial.print("Conectando ao MQTT... ");
  if (mqtt.connect(id.c_str())) {
    Serial.println("conectado!");
    ultimoComandoJoystick = "";
    ultimoEnvioJoystick = 0;
  } else {
    Serial.print("erro: ");
    Serial.println(mqtt.state());
  }
}

void atualizarLEDs() {
  digitalWrite(PIN_LED_VERDE, roboLigado ? HIGH : LOW);
  digitalWrite(PIN_LED_VERMELHO, roboLigado ? LOW : HIGH);
}

void setup() {
  Serial.begin(115200);
  delay(500);
  pinMode(PIN_LED_VERDE, OUTPUT);
  pinMode(PIN_LED_VERMELHO, OUTPUT);
  pinMode(PIN_BUTTON_TOGGLE, INPUT_PULLUP);
  pinMode(PIN_HOR, INPUT);
  pinMode(PIN_VER, INPUT);
  pinMode(PIN_SEL, INPUT_PULLUP);
  atualizarLEDs();
  ultimoEstadoLeitura = digitalRead(PIN_BUTTON_TOGGLE);
  estadoEstavelBotao = ultimoEstadoLeitura;
  conectarWiFi();
  mqtt.setServer(MQTT_BROKER, MQTT_PORT);
  mqtt.setKeepAlive(30);
  Serial.println("Controlador iniciado. Aguardando MQTT...");
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.reconnect();
    delay(10);
    return;
  }
  reconectarMQTT();
  mqtt.loop();

  bool leituraAtual = digitalRead(PIN_BUTTON_TOGGLE);
  if (leituraAtual != ultimoEstadoLeitura) {
    ultimoTempoDebounce = millis();
  }
  if (millis() - ultimoTempoDebounce > ATRASO_DEBOUNCE) {
    if (leituraAtual != estadoEstavelBotao) {
      estadoEstavelBotao = leituraAtual;
      if (estadoEstavelBotao == LOW) {
        roboLigado = !roboLigado;
        atualizarLEDs();
        if (roboLigado) {
          Serial.println("Comando: LIGAR");
          enviarComando("LIGAR");
        } else {
          Serial.println("Comando: DESLIGAR");
          enviarComando("DESLIGAR");
        }
        ultimoComandoJoystick = "";
      }
    }
  }
  ultimoEstadoLeitura = leituraAtual;

  if (roboLigado && mqtt.connected()) {
    int valorX = analogRead(PIN_HOR);
    int valorY = analogRead(PIN_VER);
    String comandoAtual = "PARADO";
    if (valorY > CENTRO + TOLERANCIA) {
      comandoAtual = "Frente";
    } else if (valorY < CENTRO - TOLERANCIA) {
      comandoAtual = "Tras";
    } else if (valorX > CENTRO + TOLERANCIA) {
      comandoAtual = "Direita";
    } else if (valorX < CENTRO - TOLERANCIA) {
      comandoAtual = "Esquerda";
    }

    unsigned long agora = millis();
    bool comandoMudou = comandoAtual != ultimoComandoJoystick;
    bool reenviarMovimento = comandoAtual != "PARADO" &&
        agora - ultimoEnvioJoystick >= INTERVALO_REENVIO_JOYSTICK;
    if (comandoMudou || reenviarMovimento) {
      if (comandoMudou) {
        Serial.print("Joystick: ");
        Serial.println(comandoAtual);
      }
      enviarComando(comandoAtual.c_str());
      ultimoComandoJoystick = comandoAtual;
      ultimoEnvioJoystick = agora;
    }
  }
  delay(20);
}
