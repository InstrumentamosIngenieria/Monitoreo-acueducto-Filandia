#define TINY_GSM_MODEM_SIM800
#include <TinyGsmClient.h>
#include <PubSubClient.h>
#include <HardwareSerial.h>

HardwareSerial SerialAT(2);
TinyGsm modem(SerialAT);
TinyGsmClient gsmClient(modem);        // Sin SSL — puerto 1883
PubSubClient mqtt(gsmClient);

// ── Configuración ─────────────────────────────────
const char APN[]        = "internet.comcel.com.co";
const char APN_USER[]   = "";
const char APN_PASS[]   = "";

const char BROKER[]     = "f1af063f.ala.dedicated.aws.emqxcloud.com";
const int  PORT         = 1883;
const char MQTT_USER[]  = "esp32_cliente";
const char MQTT_PASS[]  = "Esp32_Acueducto2026!";
const char CLIENT_ID[]  = "ESP32_Filandia_001";

// ── Tópicos ───────────────────────────────────────
const char TOPIC_STATUS[] = "acueducto/filandia/status";
const char TOPIC_DATA[]   = "acueducto/filandia/sensores";

unsigned long lastPublish = 0;
const long INTERVALO = 30000;  // 30 segundos

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Mensaje recibido [");
  Serial.print(topic);
  Serial.print("]: ");
  for (unsigned int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
}

bool mqttReconnect() {
  int intentos = 0;
  while (!mqtt.connected() && intentos < 3) {
    Serial.print("Conectando MQTT... ");
    if (mqtt.connect(CLIENT_ID, MQTT_USER, MQTT_PASS)) {
      Serial.println("OK");
      mqtt.publish(TOPIC_STATUS, "ESP32 online - Acueducto Filandia");
      mqtt.subscribe(TOPIC_DATA);
      return true;
    }
    Serial.print("FALLO rc=");
    Serial.print(mqtt.state());
    Serial.println(" reintentando en 5s...");
    delay(5000);
    intentos++;
  }
  return false;
}

void setup() {
  Serial.begin(115200);
  SerialAT.begin(57600, SERIAL_8N1, 26, 27);
  delay(8000);

  Serial.println("Reiniciando modem...");
  modem.restart();
  Serial.print("Modem: ");
  Serial.println(modem.getModemInfo());

  Serial.print("Esperando red");
  while (!modem.waitForNetwork(30000)) {
    Serial.println(" -> reintentando...");
  }
  Serial.println(" -> OK");

  Serial.print("Conectando GPRS... ");
  if (modem.gprsConnect(APN, APN_USER, APN_PASS)) {
    Serial.println("OK");
    Serial.print("IP: ");
    Serial.println(modem.localIP());
  } else {
    Serial.println("FALLO GPRS — reiniciando...");
    delay(5000);
    ESP.restart();
  }

  mqtt.setServer(BROKER, PORT);
  mqtt.setCallback(mqttCallback);
  mqtt.setKeepAlive(60);
  mqttReconnect();
}

void loop() {
  if (!mqtt.connected()) {
    Serial.println("MQTT desconectado — reconectando...");
    if (!modem.isGprsConnected()) {
      Serial.println("GPRS caído — reconectando...");
      modem.gprsConnect(APN, APN_USER, APN_PASS);
    }
    mqttReconnect();
  }
  mqtt.loop();

  unsigned long now = millis();
  if (now - lastPublish >= INTERVALO) {
    lastPublish = now;
    String payload = "{\"dispositivo\":\"Filandia\","
                     "\"uptime\":" + String(millis()/1000) +
                     ",\"rssi\":" + String(modem.getSignalQuality()) +
                     ",\"sensor\":0}";
    Serial.print("Publicando: ");
    Serial.println(payload);
    mqtt.publish(TOPIC_DATA, payload.c_str());
  }
}
