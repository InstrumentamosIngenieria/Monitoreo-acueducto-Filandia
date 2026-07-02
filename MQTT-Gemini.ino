#define TINY_GSM_MODEM_SIM800
#include <TinyGsmClient.h>
#include <PubSubClient.h>
#include <HardwareSerial.h>

// ── DEFINICIÓN DE HARDWARE Y PINES (CORREGIDOS) ──────────────────────
HardwareSerial SerialAT(2);
TinyGsm modem(SerialAT);
TinyGsmClient gsmClient(modem);        
PubSubClient mqtt(gsmClient);

// ── CONFIGURACIÓN GPRS (Claro Colombia) ──────────────────────────────
const char APN[]        = "internet.comcel.com.co";
const char APN_USER[]   = "";
const char APN_PASS[]   = "";

// ── CREDENCIALES REALES DE ADAFRUIT IO ───────────────────────────────
const char BROKER[]     = "io.adafruit.com"; 
const int  PORT         = 1883;                                
const char MQTT_USER[]  = "esp32_cliente"; 

// CORRECCIÓN: Recuerda poner aquí tu clave real completa de Adafruit (la generada en tu cuenta)
const char MQTT_PASS[]  = "aio_........"; 
const char CLIENT_ID[]  = "ESP32_Acueducto_Filandia_SIM";   

// ── TÓPICOS PARA LA SIMULACIÓN ───────────────────────────────────────
const char TOPIC_NIVEL_SIMULADO[]   = "esp32_cliente/feeds/Nivel_Simulado";
const char TOPIC_CAUDAL_SIMULADO[]  = "esp32_cliente/feeds/Caudal_Simulado";

// ── TEMPORIZADOR (1 Minuto) ──────────────────────────────────────────
const long INTERVALO_ENVIO          = 60000;  
unsigned long lastEnvio             = 0;

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  // Callback vacío
}

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

void setup() {
  Serial.begin(115200);
  
  // CORRECCIÓN CRÍTICA DE PINES: Configurados exactamente en el 16 (RX2) y 17 (TX2)
  SerialAT.begin(57600, SERIAL_8N1, 16, 17); 
  delay(5000);

  Serial.println("Iniciando Telemetría (Pines 16/17 Corregidos)...");
  
  modem.restart();
  
  Serial.println("Esperando red celular (Máximo 3 intentos)...");
  int intentosRed = 0;
  bool redLista = false;
  
  while (intentosRed < 3) {
    if (modem.waitForNetwork(10000)) { 
      redLista = true;
      break; 
    }
    intentosRed++;
    Serial.print("Intento "); Serial.print(intentosRed); Serial.println(" fallido, reintentando...");
  }

  if (redLista) {
    Serial.println(" Red celular detectada por software. OK");
  } else {
    Serial.println(" Alerta: Forzando paso por estado físico de LED (3s)...");
  }

  // Conectar GPRS con hardware enrutado correctamente
  Serial.print("Conectando GPRS... ");
  if (modem.gprsConnect(APN, APN_USER, APN_PASS)) {
    Serial.println("¡CONECTADO A INTERNET EXITOSAMENTE!");
  } else {
    Serial.println("FALLO CRÍTICO GPRS");
  }
  
  mqtt.setServer(BROKER, PORT);
  mqtt.setCallback(mqttCallback);
  mqttReconnect();
}

void loop() {
  unsigned long now = millis();

  if (mqtt.connected()) {
    mqtt.loop();
  }

  if (now - lastEnvio >= INTERVALO_ENVIO) {
    lastEnvio = now;
    
    Serial.println("[WEB] Iniciando transmisión...");
    
    if (!mqtt.connected()) {
      Serial.println("[MQTT] Canal caído. Reintentando...");
      if (!modem.isGprsConnected()) { 
        modem.gprsConnect(APN, APN_USER, APN_PASS); 
      }
      mqttReconnect();
    }
    
    if (mqtt.connected()) {
      // Generación de simulación
      int nivelMapeadoSimulado = random(0, 101); 
      int caudalSimulado       = random(20, 251); 
      
      Serial.print("[LOCAL] Enviando -> Nivel: ");
      Serial.print(nivelMapeadoSimulado); Serial.print("%, Caudal: ");
      Serial.print(caudalSimulado); Serial.println(" L/min");

      // Publicación en Adafruit
      mqtt.publish(TOPIC_NIVEL_SIMULADO, String(nivelMapeadoSimulado).c_str());
      delay(2000); 
      mqtt.publish(TOPIC_CAUDAL_SIMULADO, String(caudalSimulado).c_str());
      
      Serial.println("[MQTT] Transmisión exitosa.");
    }
  }
}
