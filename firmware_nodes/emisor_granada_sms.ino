/* Este proyecto de monitoreo de tres tanques (Granada, Bambuco y Fachadas) en el Acueducto Regional Rural del municipio de Filandia, departamento del Quindio,
se firmó el contrato por $12'000.000, que incluye 3 receptores GPRS-GSM con SIM800 y un receptro en la oficina con pantalla LED y via GPRS-GSM con SIM800, el día
29 de mayo del 2025, y se proyecta instalar el próximo jueves, 6 de noviembre.
Se trató de contratar 4 planes con Claro de SMS ilimitado, pero durante casi un mes no se logró que activaran un buen servicio, para lo cual se compararon 4 SimCard
en Claro y se pagaron 4 planes prepago de SMS ilimitado por $32.000/mes, sieno ilimitado 3000 SMS mensuales, implicando que para lograr lo anterior, tenia que recibir
datos cada 45 minutos para un total de 2976 SMS del receptor y 992 del Emisor.
Se alimenta el equipo con el regulador LM2586 de 5V, ya que los diferentes módulos que alimenta son: ESP32 = 4.5V; Presion Aire 40Kpa = 4.5V; HCSR04 = 4.5V; LCD 128x64 = 4.5V;
MicroSD = 4.5V; SIM800L = 4.5V
Se realizó el código con la ayuda de IAs, en especial Claude, para lo cual se logró el código que realizara:
1. Alertar mediante ALERTA en LCD y con LED el estado del tanque, ya sea rebose (H < 20 cms), vaciado (H > 300 cms) o no instalar memoria SD
2. Se graba en SD los datos, incluyendo el Estado y la fecha y hora en que se reinicie el equipo
3. Se tiene un led verde de encendido
4. El LED rojo, se enciende durante unos 5 segundos cuando recibe un mensaje
5. CLaude generó el manual de operación y mantenimiento, así como las recomendaciones periódicas de revisión.
6. Para el Emisor, el LED parpadea 2 veces y luego se sostiene por 15 seg, indicando envío de los datos
Fecha de actualización : Febrero 10 del 2026- (Duración: 5 meses y medio)
*/

#include "HX711.h"
#include <HardwareSerial.h>

// ===== CONFIGURACIÓN SIM800 =====
#define SIM_RX 17 // Pin 17 del ESP32 con el TX del SIM800
#define SIM_TX 16 // Pin 16 del ESP32 con RX del SIM800
HardwareSerial SIM800(2);

// ===== CONFIGURACIÓN SENSORES =====
HX711 scale; // Sensor de presiön diferencial
// Blanco VCC; Negro GND; Amarillo OUT = DAT; Rojo SCK = CLK
#define LED_BUILTIN 2
#define DAT 21 // Sensor de presión diferencial
#define CLK 22 // Sensor de presión diferencial
#define TRIG_PIN 33
#define ECHO_PIN 32

// ===== CONFIGURACIÓN DEL TANQUE (CAMBIAR SEGÚN INSTALACIÓN) =====
String tanque = "TANQUE GRANADA"; // Cel 3113278391
//String tanque = "TANQUE BAMBUCO"; // Cel 3113278374
//String tanque = "TANQUE FACHADAS"; // Cel 3113278377
// Receptor oficina Acueducto Cel 3113278382

float Kplaca = 0.16117; // Macromedidor de 4 pulg Granada
//float Kplaca = 0.06969; // Macromedidor de 2.5 pulg Bambuco
//float Kplaca = 0.05010; // Macromedidor de 2 pulg Fachadas

// ===== VARIABLES DE MEDICIÓN =====
const int datosLectura = 30;
//const unsigned long intervaloLectura = 270000; // 45 minutos = 2700000
const unsigned long intervaloLectura = 120000; // 2 minutos para pruebas

float datoPresion = 0;
float promPresion = 0;
float sumaPresion = 0;

float promNivel = 0;
float sumaNivel = 0;

float caudal = 0;
float volumenParcial = 0;
float volumenTotal = 0;

unsigned long ultimaLectura = 0;
int contadorInicializacion = 0;

String mensajeSMS = "";
bool smsEnviado = false;

// ===================================================
void setup() {
  Serial.begin(115200);
  delay(2000);
  pinMode(LED_BUILTIN, OUTPUT);
  // Configuracion pines Sensor de nivel impermeable: Blanco VCC; Negro GND; Amarillo TRIG; Rojo ECHO
  pinMode(TRIG_PIN, OUTPUT); 
  pinMode(ECHO_PIN, INPUT);
  
  Serial.println("\n========================================");
  Serial.println("SISTEMA DE MONITOREO DE TANQUES");
  Serial.println("Acueducto Regional Filandia - Quindío");
  Serial.println("========================================");
  Serial.println("Tanque: " + tanque);
  Serial.println("Intervalo: " + String(intervaloLectura/60000) + " minutos");
  Serial.println("========================================\n");
  
  // Inicializar SIM800
  // ================================
  Serial.println("\n📱 Inicializando módulo SIM800...");
  InicioSIM800();
  
  Serial.println("\n✓ Sistema iniciado correctamente");
  Serial.println("Esperando primera lectura...\n");
  
  // Realizar primera lectura inmediatamente
  ultimaLectura = millis() - intervaloLectura;

  // Inicializar sensor de presión
  // ================================
  Serial.println("📊 Inicializando Sensor de Presión...");
  scale.begin(DAT, CLK);
  scale.set_gain(128); // gain(128) para 20 mV
  
  Serial.print("   Esperando respuesta ");
  int intentos = 0;
  while (!scale.is_ready() && intentos < 50) {
    Serial.print(".");
    delay(100);
    intentos++;
  }
  
  if (scale.is_ready()) {
    Serial.println(" ✓ Listo!");
  } else {
    Serial.println(" ✗ Error!");
  }
  
  delay(1000);
  Monitoreo();
  delay(60000); // Retrazo de un minuto para que se ajuste el SIM800 con la señal.
}
// ===================================================
void loop() {
  unsigned long tiempoActual = millis();
  
  // Verificar si es momento de hacer lectura
  if (tiempoActual - ultimaLectura >= intervaloLectura) {
    ultimaLectura = tiempoActual;
    
    Serial.println("📊 INICIANDO CICLO DE LECTURA");
    
    // Parpadeo LED indicando inicio de ciclo
    for (int i = 0; i < 3; i++) {
      digitalWrite(LED_BUILTIN, HIGH);
      delay(100);
      digitalWrite(LED_BUILTIN, LOW);
      delay(100);
    }
    
    Monitoreo();
    Serial.println("✓ Ciclo completado");
    Serial.println("⏱️  Próxima lectura en " + String(intervaloLectura/60000) + " minutos");
    Serial.println("========================================\n");
  }
  delay(200); // Pequeño delay para no saturar el CPU
}

void Monitoreo() {
  leerNivel();
  leerPresion();
  calcularVariables();
  mostrarDatos(); 
  enviarSMS();
}

// ===== INICIALIZACIÓN SIM800 =====
void InicioSIM800() {
  SIM800.begin(9600, SERIAL_8N1, SIM_RX, SIM_TX);
  delay(70);
  // Limpiar buffer
  while (SIM800.available()) SIM800.read();
  
  // 1. Test de comunicación
  Serial.print("   Test comunicación...");
  SIM800.println("AT");
  delay(1000);
  if (SIM800.available()) {
    String resp = SIM800.readString();
    Serial.println(" ✓ OK");
  } else {
    Serial.println(" ✗ Error");
  }
  
  // 2. Desactivar eco
  SIM800.println("ATE0");
  delay(500);
  while (SIM800.available()) SIM800.read();
  
  // 3. Modo texto SMS
  Serial.print("   Configurando modo SMS...");
  SIM800.println("AT+CMGF=1");
  delay(1000);
  while (SIM800.available()) SIM800.read();
  Serial.println(" ✓ OK");
  
  // 4. Charset GSM
  SIM800.println("AT+CSCS=\"GSM\"");
  delay(500);
  while (SIM800.available()) SIM800.read();
  
  // 5. Verificar señal
  Serial.print("   Verificando señal...");
  SIM800.println("AT+CSQ");
  delay(1000);
  if (SIM800.available()) {
    String signal = SIM800.readString();
    Serial.println(" " + signal);
  }
  
  Serial.println("   ✓ SIM800 configurado correctamente");
}

// ===== LECTURA DE NIVEL (ULTRASONIDO) =====
void leerNivel() {
  Serial.println("\n📏 Leyendo nivel del tanque...");
  sumaNivel = 0;
  int lecturasValidas = 0;
  
  for (int k = 0; k < datosLectura; k++) {
    digitalWrite(TRIG_PIN, LOW);
    delayMicroseconds(2);
    digitalWrite(TRIG_PIN, HIGH);
    delayMicroseconds(30);
    digitalWrite(TRIG_PIN, LOW);
    
    float duracion = pulseIn(ECHO_PIN, HIGH, 30000); // Timeout 30ms
    
    if (duracion > 0) {
      float distancia = duracion * 0.01715;
      
      // Filtrar lecturas anómalas (< 2cm o > 400cm)
      if (distancia >= 2 && distancia <= 400) {
        sumaNivel += distancia;
        lecturasValidas++;
      }
    }
    delay(100);
  }
  
  if (lecturasValidas > 0) {
    promNivel = sumaNivel / lecturasValidas;
    Serial.println("   ✓ Nivel promedio: " + String(promNivel, 1) + " cm (" + 
                   String(lecturasValidas) + "/" + String(datosLectura) + " lecturas válidas)");
  } else {
    promNivel = 0;
    Serial.println("   ✗ No se obtuvieron lecturas válidas");
  }
}

// ===== LECTURA DE CAUDAL (PRESIÓN) =====
void leerPresion() {
  Serial.println("\n💧 Leyendo presión del caudal...");
  sumaPresion = 0;
  int validas = 0;
// *********************************************************************
  for (int i = 0; i < datosLectura; i++) {
    if (scale.is_ready()) {
      sumaPresion += (scale.read() + 3000000); //AJUSTE A LECTURAS POSITIVAS
      validas++;
    }
    delay(100);
  }

  if (validas > 0) {
    promPresion = sumaPresion / validas;
    Serial.println("   ✓ Presión leída: " + String(promPresion, 0) + 
                   " (" + String(validas) + "/" + String(datosLectura) + " lecturas)");
    } else {
    promPresion = 0;
    Serial.println("   ✗ No se pudo leer presión");
  }
}

// ===== CALCULAR VARIABLES FINALES =====
void calcularVariables() {
  Serial.println("\n🔢 Calculando variables...");
  
  // Ajustar promedio de nivel si es negativo
  if (promNivel < 0) {
    promNivel = 0;
  }
  
  // Calcular caudal con ajuste a cero
  // ***************************************************************************************
  caudal = Kplaca * sqrt(promPresion/419.43) - 3.38; // 1 Pascal = 419.43 lecturas de presión Y AJUSTAR CAUDAL CERO

  if (caudal < 0) {
    caudal = 0;
  }
  
  // Calcular volumen parcial y acumulado
  volumenParcial = caudal * intervaloLectura / 1000000; // m³
  volumenTotal += volumenParcial;
  
  Serial.println("   ✓ Cálculos completados");
}

// ===== MOSTRAR DATOS =====
void mostrarDatos() {
  Serial.println("\n📊 DATOS PROCESADOS:");
  Serial.println("   Tanque: " + tanque);
  Serial.println("   Caudal: " + String(caudal, 2) + " lps");
  Serial.println("   Volumen acumulado: " + String(volumenTotal, 1) + " m³");
  Serial.println("   Nivel: " + String(promNivel, 0) + " cm");
  
  // Preparar mensaje SMS
  mensajeSMS = tanque + ";" + String(caudal, 2) + ";" + 
               String(volumenTotal, 1) + ";" + String(promNivel, 0);
  
  Serial.println("\n📱 Mensaje SMS preparado:");
  Serial.println("   " + mensajeSMS);
}

// ===== ENVIAR SMS =====
void enviarSMS() {
  Serial.println("\n📤 ENVIANDO SMS...");
  digitalWrite(LED_BUILTIN, HIGH);
  
  // Número de destino principal
  //String numeroDestino = "+573113278382";
  String numeroDestino = "+573117861303";

  Serial.println("   Destinatario: " + numeroDestino);
  
  // Limpiar buffer
  while (SIM800.available()) SIM800.read();
  
  // Enviar comando de SMS
  SIM800.println("AT+CMGS=\"" + numeroDestino + "\"");
  delay(1000);
  
  // Verificar respuesta > simbolo anterior entre comillas
  bool listo = false;
  unsigned long timeout = millis();
  while (millis() - timeout < 5000) {
    if (SIM800.available()) {
      char c = SIM800.read();
      Serial.print(c);
      if (c == '>') {
        listo = true;
        break;
      }
    }
  }
  
  if (listo) {
    // Enviar mensaje
    SIM800.println(mensajeSMS);
    delay(500);
    
    // Enviar Ctrl+Z
    SIM800.write(26);
    delay(3000);
    
    // Verificar respuesta
    String respuesta = "";
    timeout = millis();
    while (millis() - timeout < 10000) {
      if (SIM800.available()) {
        respuesta += (char)SIM800.read();
      }
    }
    
    Serial.println(respuesta);
    
    if (respuesta.indexOf("OK") >= 0) {
      Serial.println("   ✓ SMS enviado correctamente");
      smsEnviado = true;
    } else {
      Serial.println("   ✗ Error al enviar SMS");
      smsEnviado = false;
    }
    
    // LIMPIAR MEMORIA SMS DESPUÉS DE ENVIAR
    delay(500);
    Serial.println("\n🗑️  Limpiando memoria SMS...");
    SIM800.println("AT+CMGD=1,4");
    delay(1000);
    
    while (SIM800.available()) {
      Serial.print((char)SIM800.read());
    }
    Serial.println("\n   ✓ Memoria limpiada");
    
  } else {
    Serial.println("   ✗ Módulo SIM800 no respondió");
    smsEnviado = false;
  }
  
  digitalWrite(LED_BUILTIN,LOW);
}
