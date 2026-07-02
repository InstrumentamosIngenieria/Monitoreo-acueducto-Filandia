#define TINY_GSM_MODEM_SIM800
#include <TinyGsmClient.h>
#include <PubSubClient.h>
#include <HardwareSerial.h>

// ── DEFINICIÓN DE HARDWARE Y PINES ────────────────────────────────────
HardwareSerial SerialAT(2);
TinyGsm modem(SerialAT);
TinyGsmClient gsmClient(modem);
PubSubClient mqtt(gsmClient);

// Pines UART hacia el SIM800L (los que ya tienes funcionando, sin tocar)
#define SIM800_RX_PIN 16
#define SIM800_TX_PIN 17

// Tu módulo no expone PWRKEY, solo VCC/RST/TXD/RXD/NET/GND.
// El apagado/encendido real se hace cortando la alimentación con un
// MOSFET (ej. IRLZ44N o AO3400) puesto en la línea de GND del SIM800L,
// controlado por este pin. Ver explicación de cableado en el chat.
#define POWER_SWITCH_PIN 4

// Pin conectado al RST del SIM800L (opcional, para forzar un reinicio
// limpio justo después de energizar el módulo). Déjalo en -1 si no
// quieres usarlo.
#define RST_PIN 5

// ── CONFIGURACIÓN GPRS (Claro Colombia) ───────────────────────────────
const char APN[]        = "internet.comcel.com.co";
const char APN_USER[]   = "";
const char APN_PASS[]   = "";

// ── CREDENCIALES DE ADAFRUIT IO ────────────────────────────────────────
const char BROKER[]     = "io.adafruit.com";
const int  PORT         = 1883;
const char MQTT_USER[]  = "esp32_cliente";

// Recuerda poner aquí tu clave real completa de Adafruit
const char MQTT_PASS[]  = "aio_........";
const char CLIENT_ID[]  = "ESP32_Acueducto_Filandia_SIM";

// ── TÓPICOS ─────────────────────────────────────────────────────────
const char TOPIC_NIVEL_SIMULADO[]   = "esp32_cliente/feeds/Nivel_Simulado";
const char TOPIC_CAUDAL_SIMULADO[]  = "esp32_cliente/feeds/Caudal_Simulado";

// ── TEMPORIZACIÓN ───────────────────────────────────────────────────
const unsigned long INTERVALO_CAPTURA = 30000UL;    // toma una lectura cada 30 s (ajustable)
const unsigned long INTERVALO_ENVIO   = 300000UL;   // enciende el módem y envía cada 5 min
unsigned long lastCaptura = 0;
unsigned long lastEnvio   = 0;

// ── ACUMULADORES DE MUESTRAS (para promediar entre envíos) ──────────
float sumaNivel  = 0;
float sumaCaudal = 0;
unsigned int muestras = 0;

bool modemEncendido = false;

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  // Callback vacío
}

// ── CONTROL DE ENCENDIDO / APAGADO DEL SIM800L (vía MOSFET) ──────────
// El GND del SIM800L pasa por un MOSFET N-channel (IRLZ44N / AO3400).
// POWER_SWITCH_PIN en HIGH = MOSFET conduce = módulo energizado.
// POWER_SWITCH_PIN en LOW  = MOSFET corta   = módulo sin alimentación real.
void encenderModem() {
  if (modemEncendido) return;

  Serial.println("[PWR] Energizando SIM800L (MOSFET ON)...");
  digitalWrite(POWER_SWITCH_PIN, HIGH);
  delay(3000);   // tiempo de arranque interno del módulo (auto-boot al recibir GND)

  #if RST_PIN >= 0
    // Pulso de reset opcional para asegurar un arranque limpio
    pinMode(RST_PIN, OUTPUT);
    digitalWrite(RST_PIN, HIGH);
    delay(100);
    digitalWrite(RST_PIN, LOW);
    delay(300);
    digitalWrite(RST_PIN, HIGH);
    delay(2000);
  #endif

  SerialAT.begin(57600, SERIAL_8N1, SIM800_RX_PIN, SIM800_TX_PIN);
  modem.restart();
  modemEncendido = true;
}

void apagarModem() {
  if (!modemEncendido) return;

  Serial.println("[PWR] Cortando alimentación del SIM800L (MOSFET OFF)...");
  if (mqtt.connected()) mqtt.disconnect();
  modem.gprsDisconnect();
  delay(300);
  digitalWrite(POWER_SWITCH_PIN, LOW);   // corte físico de energía: consumo ~0
  modemEncendido = false;
}

// ── CONEXIÓN MQTT ─────────────────────────────────────────────────────
bool mqttReconnect() {
  int intentos = 0;
  while (!mqtt.connected() && intentos < 3) {
    Serial.print("Conectando a Adafruit IO... ");
    if (mqtt.connect(CLIENT_ID, MQTT_USER, MQTT_PASS)) {
      Serial.println("OK");
      return true;
    }
    Serial.print("FALLO rc="); Serial.print(mqtt.state());
    Serial.println(" reintentando en 5s...");
    delay(5000);
    intentos++;
  }
  return false;
}

// ── CAPTURA DE DATOS (corre siempre, sin depender del módem) ─────────
// Aquí reemplazas las líneas random() por tus lecturas reales del
// HX711 (presión/nivel) y el HC-SR04 (nivel por ultrasonido / caudal).
void capturarDatos() {
  int nivelMapeadoSimulado = random(0, 101);
  int caudalSimulado       = random(20, 251);

  sumaNivel  += nivelMapeadoSimulado;
  sumaCaudal += caudalSimulado;
  muestras++;

  Serial.print("[CAPTURA] Nivel: "); Serial.print(nivelMapeadoSimulado);
  Serial.print("%  Caudal: "); Serial.print(caudalSimulado);
  Serial.print(" L/min  (muestras acumuladas: "); Serial.print(muestras); Serial.println(")");
}

// ── ENCIENDE EL MÓDEM, ENVÍA EL PROMEDIO ACUMULADO, Y LO APAGA ────────
void enviarDatos() {
  if (muestras == 0) {
    Serial.println("[ENVÍO] Sin muestras acumuladas en este ciclo, se omite.");
    return;
  }

  int nivelProm  = round(sumaNivel  / muestras);
  int caudalProm = round(sumaCaudal / muestras);

  Serial.println("[WEB] Iniciando transmisión...");
  encenderModem();

  Serial.println("Esperando red celular (máx. 3 intentos)...");
  bool redLista = false;
  for (int i = 0; i < 3; i++) {
    if (modem.waitForNetwork(10000)) {
      redLista = true;
      break;
    }
    Serial.print("Intento "); Serial.print(i + 1); Serial.println(" fallido, reintentando...");
  }

  if (!redLista) {
    Serial.println("Sin red disponible. Se descartan los datos de este ciclo.");
    apagarModem();
    sumaNivel = 0; sumaCaudal = 0; muestras = 0;
    return;
  }

  Serial.print("Conectando GPRS... ");
  if (!modem.gprsConnect(APN, APN_USER, APN_PASS)) {
    Serial.println("FALLO CRÍTICO GPRS");
    apagarModem();
    sumaNivel = 0; sumaCaudal = 0; muestras = 0;
    return;
  }
  Serial.println("¡CONECTADO A INTERNET!");

  mqtt.setServer(BROKER, PORT);
  mqtt.setCallback(mqttCallback);

  if (mqttReconnect()) {
    Serial.print("[LOCAL] Enviando -> Nivel: "); Serial.print(nivelProm);
    Serial.print("%, Caudal: "); Serial.print(caudalProm); Serial.println(" L/min");

    mqtt.publish(TOPIC_NIVEL_SIMULADO, String(nivelProm).c_str());
    delay(500);
    mqtt.publish(TOPIC_CAUDAL_SIMULADO, String(caudalProm).c_str());
    delay(500);
    mqtt.loop();

    Serial.println("[MQTT] Transmisión exitosa.");
  } else {
    Serial.println("[MQTT] No se pudo conectar. Datos de este ciclo descartados.");
  }

  // Se reinician los acumuladores para el siguiente ciclo de 5 minutos
  sumaNivel = 0;
  sumaCaudal = 0;
  muestras = 0;

  apagarModem();
}

void setup() {
  Serial.begin(115200);
  randomSeed(analogRead(34));  // pin flotante, solo para variar la simulación

  // Importante: dejar el MOSFET en OFF desde el primer instante,
  // para que el SIM800L no reciba energía hasta el primer envío.
  pinMode(POWER_SWITCH_PIN, OUTPUT);
  digitalWrite(POWER_SWITCH_PIN, LOW);

  Serial.println("Sistema iniciado.");
  Serial.println("Captura cada 30s | Envío + corte de energía del SIM800L cada 5 min.");

  lastCaptura = millis();
  lastEnvio   = millis();

  // El módem arranca APAGADO a propósito: solo se enciende dentro de enviarDatos()
}

void loop() {
  unsigned long now = millis();

  // 1) Captura de datos: siempre activa, no depende del estado del módem
  if (now - lastCaptura >= INTERVALO_CAPTURA) {
    lastCaptura = now;
    capturarDatos();
  }

  // 2) Envío por MQTT cada 5 minutos: enciende el módem, publica, apaga
  if (now - lastEnvio >= INTERVALO_ENVIO) {
    lastEnvio = now;
    enviarDatos();
  }
}
