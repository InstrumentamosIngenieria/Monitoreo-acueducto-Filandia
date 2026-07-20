#define TINY_GSM_MODEM_SIM800
#include <TinyGsmClient.h>
#include <PubSubClient.h>
#include <HardwareSerial.h>

// ── DEFINICIÓN DE HARDWARE ──────────────────────
HardwareSerial SerialAT(2);
TinyGsm modem(SerialAT);
TinyGsmClient gsmClient(modem);        
PubSubClient mqtt(gsmClient);

// ── CONFIGURACIÓN GPRS (Claro Colombia) ──────────────────────────────
const char APN[]        = "internet.comcel.com.co";
const char APN_USER[]   = "";
const char APN_PASS[]   = "";

// ── CONFIGURACIÓN DE SU BROKER EMQX (Google Cloud) ──────────────────
const char BROKER[]     = "34.123.241.15"; 
const int  PORT         = 1883;                          
const char MQTT_USER[]  = "admin";
const char MQTT_PASS[]  = "Instrumentamos2026*";
const char CLIENT_ID[]  = "ESP32_Acueducto_Filandia_SIM";   

// ── TÓPICOS MQTT ────────────────────────────────
const char TOPIC_NIVEL[]   = "sensores/tanque/nivel";
const char TOPIC_CAUDAL[]  = "sensores/tanque/caudal";
const char TOPIC_DIAG[]    = "sensores/tanque/diagnostico";

// ── TEMPORIZADOR DE ENVÍO ────────────────────────
const long INTERVALO_ENVIO = 60000UL;
unsigned long lastEnvio = 0;

// ── BACKOFF PROGRESIVO PARA RECONEXIÓN ───────────────────
const unsigned long BACKOFF_PASOS[] = {5000, 10000, 30000, 60000, 300000UL};
const int NUM_PASOS_BACKOFF = 5;
int indiceBackoff = 0;
unsigned long ultimoIntentoConexion = 0;

// ── WATCHDOG DE RECUPERACIÓN TOTAL ───────────────────────
unsigned long inicioDesconexion = 0;
bool contandoDesconexion = false;
const unsigned long TIEMPO_MAX_SIN_CONEXION = 1800000UL; // 30 minutos

// ── AUTO-ACTIVACIÓN DE DATOS POR USSD ────────────────────
const char USSD_ACTIVACION[] = "*611#";
int fallosConsecutivosGPRS = 0;
const int MAX_FALLOS_ANTES_USSD = 5;
unsigned long ultimoIntentoUSSD = 0;
const unsigned long COOLDOWN_USSD = 21600000UL;

// ── ALERTA POR SMS (DESACTIVADA EN ESTA FASE DE PROTOTIPO) ──────
const bool SMS_ALERTA_HABILITADA = false;   // <-- Cambiar a true en la versión final de campo
const char NUMERO_ALERTA[] = "+573117861303";
bool alertaSMSEnviada = false;
const int UMBRAL_FALLOS_ALERTA_SMS = 8;

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  // Reservado para futuras funciones de control remoto
}

int leerCalidadSenal() {
  return modem.getSignalQuality();
}

void evaluarYAlertarPorSMS() {
  if (!SMS_ALERTA_HABILITADA) return;   // En prototipo, no hace nada
  if (alertaSMSEnviada) return;

  int csq = leerCalidadSenal();
  bool senalBuena = (csq != 99 && csq >= 10);

  if (senalBuena && fallosConsecutivosGPRS >= UMBRAL_FALLOS_ALERTA_SMS) {
    String mensaje = "ALERTA Acueducto Filandia: sin datos hace rato. CSQ=" + String(csq) + ". Posible paquete agotado.";
    Serial.println("[SMS] Enviando alerta: " + mensaje);
    modem.sendSMS(NUMERO_ALERTA, mensaje);
    alertaSMSEnviada = true;
  }
}

void intentarActivacionUSSD() {
  if (ultimoIntentoUSSD != 0 && millis() - ultimoIntentoUSSD < COOLDOWN_USSD) {
    Serial.println("[USSD] En cooldown, se omite intento.");
    return;
  }
  Serial.println("[USSD] Muchos fallos seguidos. Intentando activar paquete de datos...");
  String respuesta = modem.sendUSSD(USSD_ACTIVACION);
  Serial.print("[USSD] Respuesta: ");
  Serial.println(respuesta);
  ultimoIntentoUSSD = millis();
  fallosConsecutivosGPRS = 0;
}

bool asegurarGPRS() {
  if (modem.isGprsConnected()) {
    alertaSMSEnviada = false;
    return true;
  }
  Serial.println("[GPRS] Desconectado. Intentando reconectar...");
  int csq = leerCalidadSenal();
  Serial.print("[DIAGNOSTICO] Calidad de señal (CSQ): ");
  Serial.println(csq);

  bool ok = modem.gprsConnect(APN, APN_USER, APN_PASS);
  if (ok) {
    Serial.println("[GPRS] Reconectado exitosamente.");
    fallosConsecutivosGPRS = 0;
    alertaSMSEnviada = false;
  } else {
    Serial.println("[GPRS] Fallo al reconectar.");
    fallosConsecutivosGPRS++;
    evaluarYAlertarPorSMS();
    if (fallosConsecutivosGPRS >= MAX_FALLOS_ANTES_USSD) {
      intentarActivacionUSSD();
    }
  }
  return ok;
}

bool intentarConexionMQTT() {
  if (!asegurarGPRS()) {
    return false;
  }

  Serial.print("[MQTT] Conectando a su Broker EMQX... ");
  if (mqtt.connect(CLIENT_ID, MQTT_USER, MQTT_PASS)) {
    Serial.println("OK");
    indiceBackoff = 0;
    contandoDesconexion = false;

    int csq = leerCalidadSenal();
    String diag = "reconectado,csq=" + String(csq);
    mqtt.publish(TOPIC_DIAG, diag.c_str());

    return true;
  } else {
    Serial.print("FALLO rc=");
    Serial.println(mqtt.state());
    return false;
  }
}

void mantenerConexion() {
  if (mqtt.connected()) {
    return;
  }

  if (!contandoDesconexion) {
    contandoDesconexion = true;
    inicioDesconexion = millis();
  }

  if (millis() - inicioDesconexion >= TIEMPO_MAX_SIN_CONEXION) {
    Serial.println("[WATCHDOG] 30 min sin conexión. Reiniciando módem...");
    modem.restart();
    delay(3000);
    modem.gprsConnect(APN, APN_USER, APN_PASS);
    inicioDesconexion = millis();
    indiceBackoff = 0;
    return;
  }

  unsigned long espera = BACKOFF_PASOS[indiceBackoff];
  if (millis() - ultimoIntentoConexion < espera) {
    return;
  }

  ultimoIntentoConexion = millis();
  bool exito = intentarConexionMQTT();

  if (!exito && indiceBackoff < NUM_PASOS_BACKOFF - 1) {
    indiceBackoff++;
  }
}

void setup() {
  Serial.begin(115200);
  SerialAT.begin(57600, SERIAL_8N1, 16, 17);//Baudios,Datos,RX,TX
  delay(5000);

  Serial.println("Iniciando Telemetría hacia Google Cloud...");
  modem.restart();

  if (modem.gprsConnect(APN, APN_USER, APN_PASS)) {
    Serial.println("¡GPRS CONECTADO!");
  } else {
    Serial.println("[DIAGNOSTICO] GPRS no conectó en el arranque.");
  }

  mqtt.setServer(BROKER, PORT);
  mqtt.setCallback(mqttCallback);

  intentarConexionMQTT();
}

void loop() {
  // CORRECCIÓN 1: esta llamada faltaba por completo.
  // Sin ella, toda la lógica de backoff/watchdog/USSD nunca se ejecutaba,
  // y el ESP32 solo constataba la desconexión sin intentar repararla.
  mantenerConexion();

  // CORRECCIÓN 2: PubSubClient necesita esto en cada ciclo para
  // procesar keep-alives y detectar desconexiones a tiempo.
  mqtt.loop();

  if (millis() - lastEnvio >= INTERVALO_ENVIO) {
    lastEnvio = millis();

    if (mqtt.connected()) {
      // Generación de datos simulados (reemplazar luego por lecturas reales de sensores)
      int nivel = random(0, 101);
      int caudal = random(20, 251);

      // CORRECCIÓN 3: se vuelve a publicar en los dos tópicos separados
      // con valores numéricos planos, tal como esperan las reglas de EMQX
      // (regla_hehz y regla_nfzp usan float(payload), que falla silenciosamente
      // si le llega un JSON en vez de un número plano).
      mqtt.publish(TOPIC_NIVEL, String(nivel).c_str());
      mqtt.publish(TOPIC_CAUDAL, String(caudal).c_str());

      Serial.print("[MQTT] Enviado -> nivel=");
      Serial.print(nivel);
      Serial.print(", caudal=");
      Serial.print(caudal);
      Serial.println(" -> Transmisión completada.");
    } else {
      Serial.println("[MQTT] Sin conexión, envío omitido este ciclo.");
    }
  }
}
