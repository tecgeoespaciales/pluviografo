#include <Arduino.h>
#include <esp_task_wdt.h>  
#include "driver/gpio.h" 
#include "driver/rtc_io.h" 

// --- SISTEMAS DE ARCHIVOS ---
#include "FS.h"
#include "SD.h"
#include "SPI.h"
#include <LittleFS.h> 

// --- LIBRERÍAS PARA EL DS3231 ---
#include <Wire.h>
#include <RTClib.h>

// --- LIBRERÍAS PARA EL PORTAL WI-FI ---
#include <WiFi.h>
#include <WebServer.h>

// --- 1. CONFIGURACIÓN DEL MÓDEM A7670G ---
#define TINY_GSM_MODEM_SIM7600  
#define MODEM_TX             26 
#define MODEM_RX             27
#define PIN_MODEM_POWER      12 // Llave maestra de energía (SD y Módem)
#define PIN_MODEM_PWRKEY     4  

// --- 2. CONFIGURACIÓN DE SENSORES Y PINES ---
#define PIN_REED_SWITCH      GPIO_NUM_32  // Balancín del pluviómetro
#define PIN_BOTON_WIFI       GPIO_NUM_34   // Botón BOOT para extracción
#define BOTON_WIFI_MASK      (1ULL << 34) 
#define PIN_BATTERY          35 

// --- PINES SPI PARA MICRO SD ---
#define PIN_SD_MISO          2
#define PIN_SD_MOSI          15
#define PIN_SD_SCLK          14
#define PIN_SD_CS            13 

#include <TinyGsmClient.h>
#include <PubSubClient.h>

// --- 3. CREDENCIALES ---
const char apn[]        = "internet.comcel.com.co"; 
const char gprsUser[]   = "";
const char gprsPass[]   = "";
const char* mqtt_server = "38.242.158.7";
const int   mqtt_port   = 1883;
const char* mqtt_user   = "elheim";
const char* mqtt_pass   = "clave";
const char* mqtt_topic  = "pluviografo/sgc"; 

// --- 4. TIEMPOS Y WATCHDOG ---
#define uS_TO_S_FACTOR 1000000ULL 
#define TIEMPO_DORMIR_SEG 60       // 10 minutos
#define WDT_TIMEOUT 120             

// --- 5. VARIABLES RTC (PERSISTENTES EN SLEEP) ---
RTC_DATA_ATTR int contador_lluvia = 0;
RTC_DATA_ATTR int dia_anterior = -1;
RTC_DATA_ATTR unsigned long registro_numero = 0;

HardwareSerial SerialAT(1);
TinyGsm modem(SerialAT);
TinyGsmClient client(modem);
PubSubClient mqtt(client);
WebServer server(80);
RTC_DS3231 rtc; 

bool modo_wifi_activo = false;

// =========================================================================
//                   FUNCIONES DE APOYO (BATERÍA Y HORA)
// =========================================================================

int obtenerPorcentajeBateria() {
  pinMode(PIN_BATTERY, INPUT);
  long suma_adc = 0;
  for(int i = 0; i < 10; i++) { suma_adc += analogRead(PIN_BATTERY); delay(10); }
  float adc_promedio = suma_adc / 10.0;
  float voltaje_bateria = (adc_promedio / 4095.0) * 3.3 * 2.0 * 1.097; 
  int porcentaje = map(voltaje_bateria * 100, 320, 420, 0, 100);
  return constrain(porcentaje, 0, 100);
}

void obtenerFechaHora(char* buffer_fecha, char* buffer_hora) {
  bool hora_actualizada = false;

  // Como el módem acaba de encender, intentamos sacar la hora de la red
  String dt = modem.getGSMDateTime(DATE_FULL); 
  if (dt.length() >= 17) {
    int aa = dt.substring(0, 2).toInt();
    int mm = dt.substring(3, 5).toInt();
    int dd = dt.substring(6, 8).toInt();
    int hh = dt.substring(9, 11).toInt();
    int min = dt.substring(12, 14).toInt();
    int ss = dt.substring(15, 17).toInt();

    rtc.adjust(DateTime(2000 + aa, mm, dd, hh, min, ss));
    sprintf(buffer_fecha, "%02d/%02d/%02d", dd, mm, aa);
    sprintf(buffer_hora, "%02d:%02d:%02d", hh, min, ss);
    
    // LÓGICA DE MEDIANOCHE (Reset de lluvia)
    if (dia_anterior != -1 && dd != dia_anterior) {
      contador_lluvia = 0;
      Serial.println(">>> MEDIANOCHE DETECTADA POR RED. Contador reiniciado a 0.");
    }
    dia_anterior = dd;
    hora_actualizada = true;
  }

  // Si no hay red, usamos el reloj local DS3231
  if (!hora_actualizada) {
    DateTime now = rtc.now();
    if (now.year() >= 2020) {
      sprintf(buffer_fecha, "%02d/%02d/%02d", now.day(), now.month(), now.year() % 100);
      sprintf(buffer_hora, "%02d:%02d:%02d", now.hour(), now.minute(), now.second());
      
      if (dia_anterior != -1 && now.day() != dia_anterior) {
        contador_lluvia = 0;
        Serial.println(">>> MEDIANOCHE DETECTADA POR RTC. Contador reiniciado a 0.");
      }
      dia_anterior = now.day();
    } else {
      strcpy(buffer_fecha, "00/00/00");
      strcpy(buffer_hora, "00:00:00");
    }
  }
}

// =========================================================================
//                       PORTAL WI-FI DE DIAGNÓSTICO
// =========================================================================

void configurarServidorWeb() {
  server.on("/", HTTP_GET, []() {
    String uuid_esp32 = WiFi.softAPmacAddress();
    int bat = obtenerPorcentajeBateria();
    
    SPI.begin(PIN_SD_SCLK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
    bool sd_ok = SD.begin(PIN_SD_CS, SPI);
    if(sd_ok) SD.end();

    // Sumar pulso manual por si el usuario está probando el balancín con la web abierta
    if(digitalRead(PIN_REED_SWITCH) == LOW){
       delay(50);
       if(digitalRead(PIN_REED_SWITCH) == LOW){
          contador_lluvia++;
          while(digitalRead(PIN_REED_SWITCH) == LOW) { delay(10); } 
       }
    }

    String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'><title>Pluviografo</title>";
    html += "<style>body{font-family:sans-serif; background:#eef2f3; padding:20px;} .panel{background:white; border-radius:10px; padding:20px; margin-bottom:20px; box-shadow:0 2px 5px rgba(0,0,0,0.1);} ";
    html += "h1{text-align:center; color:#2c3e50;} .uuid{text-align:center; color:#7f8c8d; font-family:monospace; margin-bottom:20px;} ";
    html += "a{display:block; background:#2196F3; color:white; padding:15px; text-align:center; text-decoration:none; border-radius:8px; margin:10px 0; font-weight:bold;} ";
    html += ".btn-exit{background:#e74c3c;}</style></head><body>";
    
    html += "<h1>Nodo Pluviografo</h1>";
    html += "<p class='uuid'>S/N: " + uuid_esp32 + "</p>";

    html += "<div class='panel'><h2>Estado en Vivo</h2>";
    html += "<p style='background-color:#e8f4f8; padding:10px; border-radius:5px;'><strong>☔ Acumulado Hoy:</strong> " + String(contador_lluvia) + " pulsos</p>";
    html += "<p style='font-size:12px; color:#7f8c8d;'>* Recarga la pagina para actualizar</p>";
    html += "<hr>";
    html += "<p><strong>Bateria:</strong> " + String(bat) + "%</p>";
    html += "<p><strong>Modem 4G:</strong> [APAGADO] Ahorro de Energia</p>";
    html += "<p><strong>SD Card:</strong> " + String(sd_ok ? "OK" : "ERROR") + "</p>";
    html += "</div>";

    html += "<div class='panel'><h2>Descargar Datos</h2>";
    html += "<a href='/descargar_sd'>1. Respaldo MicroSD (.txt)</a>";
    html += "<a href='/descargar_interno'>2. Respaldo LittleFS (.txt)</a>";
    html += "</div>";

    html += "<a href='/salir' class='btn-exit'>Apagar Wi-Fi y Dormir</a>";
    html += "</body></html>";
    server.send(200, "text/html", html);
  });

  server.on("/descargar_sd", HTTP_GET, []() {
    SPI.begin(PIN_SD_SCLK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
    if (!SD.begin(PIN_SD_CS, SPI)) { server.send(500, "text/plain", "Error SD"); return; }
    File file = SD.open("/backup_lluvia.txt", FILE_READ);
    server.streamFile(file, "application/octet-stream");
    file.close(); SD.end();
  });

  server.on("/descargar_interno", HTTP_GET, []() {
    if (!LittleFS.begin(true)) { server.send(500, "text/plain", "Error FS"); return; }
    File file = LittleFS.open("/backup_interno.txt", FILE_READ);
    server.streamFile(file, "application/octet-stream");
    file.close(); LittleFS.end();
  });

  server.on("/salir", HTTP_GET, []() { server.send(200, "text/plain", "Cerrando..."); delay(1000); modo_wifi_activo = false; });
}

void iniciarPortalMantenimiento() {
  pinMode(PIN_REED_SWITCH, INPUT_PULLUP);
  WiFi.disconnect(true); WiFi.mode(WIFI_OFF); delay(100);
  WiFi.persistent(false); WiFi.mode(WIFI_AP);
  WiFi.softAP("Pluviografo_Datos", "12345678");
  configurarServidorWeb();
  server.begin();
  modo_wifi_activo = true;
  while (modo_wifi_activo) { esp_task_wdt_reset(); server.handleClient(); delay(10); }
  WiFi.softAPdisconnect(true); WiFi.mode(WIFI_OFF);
}

// =========================================================================
//                   RESPALDO Y TRANSMISIÓN MQTT
// =========================================================================

void transmitirDatos() {
  Serial.println("\n--- INICIANDO PROTOCOLO DE TRANSMISION ---");
  
  // Como el módem acaba de recibir energía, le damos tiempo para auto-iniciar
  SerialAT.begin(115200, SERIAL_8N1, MODEM_RX, MODEM_TX);
  delay(1000);
  modem.init();
  modem.sendAT("+CTZU=1"); 
  modem.waitResponse(5000L);

  if (!modem.isNetworkConnected()) {
    Serial.println("Esperando red Claro...");
    modem.waitForNetwork(60000L); 
  }
  esp_task_wdt_reset();

  char fecha[12] = "00/00/00";
  char hora[12]  = "00:00:00";
  obtenerFechaHora(fecha, hora);

  registro_numero++;
  char payload[128];
  sprintf(payload, "{\"f\":\"%s\",\"h\":\"%s\",\"d\":\"%d\",\"b\":\"%03d\"}", 
          fecha, hora, contador_lluvia, obtenerPorcentajeBateria());

  // 1. Guardar en SD (El pin 12 ya está encendido y la SD tuvo tiempo de arrancar)
  SPI.begin(PIN_SD_SCLK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
  if (SD.begin(PIN_SD_CS, SPI)) {
    File f = SD.open("/backup_lluvia.txt", FILE_APPEND);
    if(f) { 
      f.println("Registro: " + String(registro_numero) + " | " + String(payload)); 
      f.close(); 
      Serial.println("[OK] Guardado en SD.");
    }
    SD.end();
  } else {
    Serial.println("[ERROR] No se pudo escribir en la SD.");
  }

  // 2. Guardar en LittleFS
  if (LittleFS.begin(true)) {
    File f = LittleFS.open("/backup_interno.txt", FILE_APPEND);
    if(f) { 
      f.println("Registro: " + String(registro_numero) + " | " + String(payload)); 
      f.close(); 
      Serial.println("[OK] Guardado en Memoria Interna.");
    }
    LittleFS.end();
  }

  // 3. Enviar por MQTT
  if (modem.isNetworkConnected()) {
    if (modem.gprsConnect(apn, gprsUser, gprsPass)) {
      mqtt.setServer(mqtt_server, mqtt_port);
      if (mqtt.connect("NodoPluvio", mqtt_user, mqtt_pass)) {
        mqtt.publish(mqtt_topic, payload);
        Serial.println("[OK] MQTT Publicado.");
        delay(1000);
        mqtt.disconnect();
      }
    }
  }
}

// =========================================================================
//                             SETUP PRINCIPAL
// =========================================================================

void setup() {
  esp_task_wdt_config_t wdt_config;
  wdt_config.timeout_ms = WDT_TIMEOUT * 1000; 
  wdt_config.idle_core_mask = 3;              
  wdt_config.trigger_panic = true;            
  esp_task_wdt_reconfigure(&wdt_config);      
  esp_task_wdt_add(NULL);

  Serial.begin(115200);
  Wire.begin();
  if(!rtc.begin()) { Serial.println("[WARN] DS3231 no encontrado."); }

  esp_sleep_wakeup_cause_t causa = esp_sleep_get_wakeup_cause();

  switch (causa) {
    
    // CASO 1: LLOVIÓ (Extrema rapidez, cero consumo extra)
    case ESP_SLEEP_WAKEUP_EXT0:
      contador_lluvia++; 
      Serial.println("PULSO DETECTADO. Total hoy: " + String(contador_lluvia));
      // NO encendemos el Pin 12, volvemos a dormir directamente.
      break;

    // CASO 2: BOTÓN DE EXTRACCIÓN (Portal Web)
    case ESP_SLEEP_WAKEUP_EXT1:
      Serial.println("Iniciando Portal Web...");
      // Encendemos el LDO para que la SD funcione
      gpio_hold_dis((gpio_num_t)PIN_MODEM_POWER);
      pinMode(PIN_MODEM_POWER, OUTPUT);
      digitalWrite(PIN_MODEM_POWER, HIGH);
      delay(2000); // Esperar que la SD estabilice voltaje
      iniciarPortalMantenimiento();
      break;

    // CASO 3: REPORTE DE RUTINA (Cada 10 mins)
    case ESP_SLEEP_WAKEUP_TIMER:
      Serial.println("Ciclo completado. Procesando transmision...");
      gpio_hold_dis((gpio_num_t)PIN_MODEM_POWER);
      pinMode(PIN_MODEM_POWER, OUTPUT);
      digitalWrite(PIN_MODEM_POWER, HIGH); // Encender Módem y SD
      Serial.println("Esperando 10s para arranque del modem...");
      delay(10000); 
      transmitirDatos();
      break;

    // CASO 4: ARRANQUE INICIAL (Poner batería por primera vez)
    default:
      Serial.println("\n--- ARRANQUE EN FRIO ---");
      contador_lluvia = 0;
      dia_anterior = -1;
      gpio_hold_dis((gpio_num_t)PIN_MODEM_POWER);
      pinMode(PIN_MODEM_POWER, OUTPUT);
      digitalWrite(PIN_MODEM_POWER, HIGH);
      Serial.println("Esperando 10s para arranque del modem...");
      delay(10000);
      transmitirDatos();
      break;
  }

  // --- CONFIGURAR SUEÑO Y APAGAR PERIFÉRICOS ---
  
  // Alarma 1: Balancín
  esp_sleep_enable_ext0_wakeup(PIN_REED_SWITCH, 0); 
  
  // Alarma 2: Botón Wi-Fi
  rtc_gpio_pullup_en(PIN_BOTON_WIFI);
  rtc_gpio_pulldown_dis(PIN_BOTON_WIFI);
  esp_sleep_enable_ext1_wakeup(BOTON_WIFI_MASK, ESP_EXT1_WAKEUP_ALL_LOW);
  
  // Alarma 3: Temporizador
  esp_sleep_enable_timer_wakeup(TIEMPO_DORMIR_SEG * uS_TO_S_FACTOR);

  // EL SECRETO DEL AHORRO: Forzar el Pin 12 a LOW antes de dormir
  pinMode(PIN_MODEM_POWER, OUTPUT);
  digitalWrite(PIN_MODEM_POWER, LOW); 
  
  // Poner el candado físico para que siga en LOW (0V) mientras el procesador duerme
  gpio_hold_en((gpio_num_t)PIN_MODEM_POWER);
  gpio_deep_sleep_hold_en();
  
  Serial.println("Entrando en Deep Sleep con Modem Apagado... ZZzz");
  Serial.flush();
  esp_deep_sleep_start();
}

void loop() {}
