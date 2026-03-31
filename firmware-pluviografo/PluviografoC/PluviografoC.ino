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
#define PIN_MODEM_POWER      12 // Llave maestra de energía (¡Se quedará siempre HIGH!)

// --- 2. CONFIGURACIÓN DE SENSORES Y PINES ---
#define PIN_REED_SWITCH      GPIO_NUM_32  // Entrada del balancín
#define PIN_BOTON_WIFI       GPIO_NUM_0   // Botón BOOT para extraer datos
#define BOTON_WIFI_MASK      (1ULL << 0) 
#define PIN_BATTERY          35 

// --- PINES SPI PERSONALIZADOS PARA LILYGO T-A7670 (MICRO SD) ---
#define PIN_SD_MISO          2
#define PIN_SD_MOSI          15
#define PIN_SD_SCLK          14
#define PIN_SD_CS            13 

#include <TinyGsmClient.h>
#include <PubSubClient.h>

// --- 3. CREDENCIALES DE RED Y MQTT ---
const char apn[]      = "internet.comcel.com.co"; 
const char gprsUser[] = "";
const char gprsPass[] = "";

const char* mqtt_server = "38.242.158.7";
const int   mqtt_port   = 1883;
const char* mqtt_user   = "elheim";
const char* mqtt_pass   = "clave";
const char* mqtt_topic  = "pluviografo/sibundoy"; 

// --- 4. CONFIGURACIÓN DE TIEMPOS Y WATCHDOG ---
#define uS_TO_S_FACTOR 1000000ULL 
#define TIEMPO_DORMIR_SEG 600       // 10 minutos
#define WDT_TIMEOUT 120             // 120 segundos

// --- 5. OBJETOS GLOBALES Y MEMORIA RTC ---
RTC_DATA_ATTR unsigned long registro_numero = 0;
RTC_DATA_ATTR int contador_lluvia = 0;
RTC_DATA_ATTR int dia_anterior = -1;  

HardwareSerial SerialAT(1);
TinyGsm modem(SerialAT);
TinyGsmClient client(modem);
PubSubClient mqtt(client);
WebServer server(80);
RTC_DS3231 rtc; 

bool modo_wifi_activo = false;

// =========================================================================
//                   FUNCIONES DE LECTURA (BATERÍA)
// =========================================================================

int obtenerPorcentajeBateria() {
  delay(100); 
  pinMode(PIN_BATTERY, INPUT);
  long suma_adc = 0;
  for(int i = 0; i < 10; i++) {
    suma_adc += analogRead(PIN_BATTERY); delay(10);
  }
  float adc_promedio = suma_adc / 10.0;
  float voltaje_bateria = (adc_promedio / 4095.0) * 3.3 * 2.0 * 1.097;
  int porcentaje = map(voltaje_bateria * 100, 320, 420, 0, 100);
  if (porcentaje > 100) porcentaje = 100;
  if (porcentaje < 0) porcentaje = 0;
  return porcentaje;
}

// =========================================================================
//                       ZONA DEL SERVIDOR WEB (WI-FI)
// =========================================================================

void configurarServidorWeb() {
  server.on("/", HTTP_GET, []() {
    
    // Si el balancín se movió mientras el usuario tenía la página abierta (y luego refrescó)
    // El ESP32 registrará el pulso extra porque el PIN_REED_SWITCH es evaluado aquí:
    if(digitalRead(PIN_REED_SWITCH) == LOW){
       // Pequeño antirrebote
       delay(50);
       if(digitalRead(PIN_REED_SWITCH) == LOW){
          contador_lluvia++;
          // Esperamos a que el imán se aleje para no contar doble
          while(digitalRead(PIN_REED_SWITCH) == LOW) { delay(10); } 
       }
    }

    // A. Batería
    int bat_nivel = obtenerPorcentajeBateria();
    String bat_str = String(bat_nivel) + "%";
    if (bat_nivel < 15) bat_str += " (Critico)";
    else if (bat_nivel >= 20) bat_str += " (Optimo)";

    // B. Red 4G
    // Como en el pluviógrafo dejamos el módem siempre encendido:
    String red_str = "[OK] Modem Energizado Continuamente";

    // C. MicroSD
    SPI.begin(PIN_SD_SCLK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
    bool sd_ok = SD.begin(PIN_SD_CS, SPI);
    String sd_str = sd_ok ? "[OK] Operativa" : "[ERROR] No detectada o Danada";
    if (sd_ok) SD.end(); 
    
    // D. Memoria Interna (LittleFS)
    String fs_str = "[ERROR] Al leer particion";
    if (LittleFS.begin(true)) {
      float fs_usado = LittleFS.usedBytes() / 1024.0; 
      fs_str = "[OK] " + String(fs_usado, 1) + " KB / Limite: 500.0 KB"; 
      LittleFS.end();
    }

    // E. RTC DS3231
    String rtc_str = "[ERROR] Modulo no responde";
    Wire.begin();
    if (rtc.begin()) {
      DateTime now = rtc.now();
      char buffer_rtc[30];
      sprintf(buffer_rtc, "[OK] %02d/%02d/%04d - %02d:%02d:%02d", 
              now.day(), now.month(), now.year(), 
              now.hour(), now.minute(), now.second());
      rtc_str = String(buffer_rtc);
    }

    // F. PLUVIÓMETRO (Lectura en Vivo)
    String lluvia_str = "[OK] " + String(contador_lluvia) + " pulsos registrados hoy";

    // --- CONSTRUIR HTML LIMPÍO ---
    String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'><title>Pluviógrafo Sibundoy</title>";
    html += "<style>body{font-family:'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; background-color:#eef2f3; margin:0; padding:20px; color:#333;} ";
    html += "h1{text-align:center; color:#2c3e50;} ";
    html += ".panel{background:white; border-radius:10px; padding:20px; margin-bottom:20px; box-shadow:0 4px 8px rgba(0,0,0,0.1);} ";
    html += ".panel p{margin:10px 0; font-size:16px; border-bottom:1px solid #eee; padding-bottom:10px;} ";
    html += ".panel p:last-child{border-bottom:none;} ";
    html += "a{display:block; width:100%; max-width:400px; margin:10px auto; padding:15px; text-align:center; text-decoration:none; color:white; border-radius:8px; font-weight:bold; font-size:16px; box-sizing:border-box;} ";
    html += ".btn-sd{background-color:#4CAF50;} .btn-int{background-color:#2196F3;} .btn-old{background-color:#607D8B;} .btn-exit{background-color:#e74c3c; margin-top:30px;}</style></head><body>";
    
    html += "<h1>Nodo Pluviógrafo Sibundoy</h1>";
    
    html += "<div class='panel'>";
    html += "<h2>Diagnóstico del Sistema</h2>";
    html += "<p><strong>Batería:</strong> " + bat_str + "</p>";
    html += "<p><strong>Módem 4G:</strong> " + red_str + "</p>";
    html += "<p><strong>Reloj RTC:</strong> " + rtc_str + "</p>";
    html += "<p><strong>MicroSD:</strong> " + sd_str + "</p>";
    html += "<p><strong>Mem. Interna:</strong> " + fs_str + "</p>";
    // Mostramos el conteo del balancín
    html += "<p style='background-color:#e8f4f8; padding:10px; border-radius:5px;'><strong>☔ Pluviómetro:</strong> " + lluvia_str + "</p>"; 
    html += "</div>";

    html += "<div class='panel'>";
    html += "<h2>Extracción de Datos</h2>";
    html += "<a href='/descargar_sd' class='btn-sd'>1. Descargar MicroSD</a>";
    html += "<a href='/descargar_interno' class='btn-int'>2. Respaldo Interno (Reciente)</a>"; 
    html += "<a href='/descargar_viejo' class='btn-old'>3. Respaldo Interno (Antiguo)</a>";
    html += "</div>";

    html += "<a href='/salir' class='btn-exit'>Apagar Wi-Fi y Dormir</a>";
    html += "</body></html>";
    
    server.send(200, "text/html", html);
  });

  server.on("/descargar_sd", HTTP_GET, []() {
    SPI.begin(PIN_SD_SCLK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
    if (!SD.begin(PIN_SD_CS, SPI)) { server.send(500, "text/plain", "[ERROR] No se puede leer la MicroSD."); return; }
    File file = SD.open("/backup_lluvia.txt", FILE_READ);
    if (!file) { server.send(404, "text/plain", "El archivo en la SD aun no existe."); SD.end(); return; }
    server.sendHeader("Content-Disposition", "attachment; filename=\"respaldo_lluvia_SD.txt\"");
    server.streamFile(file, "application/octet-stream");
    file.close(); SD.end();
  });

  server.on("/descargar_interno", HTTP_GET, []() {
    if (!LittleFS.begin(true)) { server.send(500, "text/plain", "[ERROR] Fallo critico LittleFS."); return; }
    File file = LittleFS.open("/backup_interno.txt", FILE_READ);
    if (!file) { server.send(404, "text/plain", "El archivo interno actual aun no existe."); LittleFS.end(); return; }
    server.sendHeader("Content-Disposition", "attachment; filename=\"interno_lluvia_reciente.txt\"");
    server.streamFile(file, "application/octet-stream");
    file.close(); LittleFS.end();
  });

  server.on("/descargar_viejo", HTTP_GET, []() {
    if (!LittleFS.begin(true)) { server.send(500, "text/plain", "[ERROR] Fallo critico LittleFS."); return; }
    File file = LittleFS.open("/backup_viejo.txt", FILE_READ);
    if (!file) { server.send(404, "text/plain", "Aun no hay datos rotados suficientes."); LittleFS.end(); return; }
    server.sendHeader("Content-Disposition", "attachment; filename=\"interno_lluvia_antiguo.txt\"");
    server.streamFile(file, "application/octet-stream");
    file.close(); LittleFS.end();
  });

  server.on("/salir", HTTP_GET, []() {
    server.send(200, "text/html", "<html><body style='text-align:center; font-family:Arial; background-color:#eef2f3; padding:50px;'><h1>Apagando Wi-Fi...</h1><p>Ya puedes cerrar esta ventana. El equipo volvera a dormir y reanudara su ciclo normal.</p></body></html>");
    delay(1500); modo_wifi_activo = false; 
  });
}

void iniciarPortalMantenimiento() {
  Serial.println("\n>>> INICIANDO PORTAL DE EXTRACCION WI-FI <<<");
  
  // Configuramos el pin del Reed Switch para que sea sensible mientras el Wi-Fi está vivo
  pinMode(PIN_REED_SWITCH, INPUT_PULLUP);

  // --- ESCUDO CONTRA EL ERROR 12308 (NVS FLASH) ---
  WiFi.disconnect(true);       
  WiFi.mode(WIFI_OFF);         
  delay(100);                  
  WiFi.persistent(false);      
  WiFi.mode(WIFI_AP);          
  // ------------------------------------------------

  WiFi.softAP("Pluviografo_Datos", "12345678"); 
  IPAddress IP = WiFi.softAPIP();
  Serial.print("Red creada. Conectate a 'Pluviografo_Datos' y entra a: http://");
  Serial.println(IP);

  configurarServidorWeb();
  server.begin();
  modo_wifi_activo = true;
  
  unsigned long tiempo_inicio = millis();
  unsigned long tiempo_ultima_conexion = millis();
  const unsigned long TIMEOUT_ABSOLUTO = 600000;    
  const unsigned long TIMEOUT_INACTIVIDAD = 60000;  

  while (modo_wifi_activo && (millis() - tiempo_inicio < TIMEOUT_ABSOLUTO)) {
    esp_task_wdt_reset(); 
    server.handleClient();
    
    if (WiFi.softAPgetStationNum() > 0) {
      tiempo_ultima_conexion = millis();
    } else if (millis() - tiempo_ultima_conexion > TIMEOUT_INACTIVIDAD) {
      Serial.println("Tiempo de inactividad superado. Abortando portal Wi-Fi.");
      modo_wifi_activo = false; 
    }
    delay(10); 
  }

  server.close();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
}

// =========================================================================
//                   ZONA DE FUNCIONES: SD, LITTLEFS Y RTC
// =========================================================================

bool guardarEnMicroSD(String payload) {
  SPI.begin(PIN_SD_SCLK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
  if (!SD.begin(PIN_SD_CS, SPI)) return false;

  String ruta_archivo = "/backup_lluvia.txt";
  File file;

  if (!SD.exists(ruta_archivo)) {
    file = SD.open(ruta_archivo, FILE_WRITE); 
    if (file) {
      file.println("--- INICIO DE REGISTRO EXTERNO (MICROSD) ---");
      file.close();
    } else {
      SD.end(); return false;
    }
  }

  file = SD.open(ruta_archivo, FILE_APPEND);
  if (file) {
    String linea = "Registro: " + String(registro_numero) + " | " + payload;
    file.println(linea);
    file.close(); 
    SD.end();     
    return true;  
  }
  SD.end();
  return false;
}

bool guardarEnMemoriaInterna(String payload) {
  if (!LittleFS.begin(true)) return false; 

  String ruta_actual = "/backup_interno.txt";
  String ruta_vieja = "/backup_viejo.txt";
  
  if (LittleFS.exists(ruta_actual)) {
    File checkFile = LittleFS.open(ruta_actual, FILE_READ);
    size_t tamano_archivo = checkFile.size();
    checkFile.close();

    if (tamano_archivo > 500000) {
      Serial.println("Rotacion: Archivo interno supero 500KB. Archivando...");
      if (LittleFS.exists(ruta_vieja)) { LittleFS.remove(ruta_vieja); }
      LittleFS.rename(ruta_actual, ruta_vieja);
    }
  }

  File file;
  if (!LittleFS.exists(ruta_actual)) {
    file = LittleFS.open(ruta_actual, FILE_WRITE); 
    if (file) {
      file.println("--- INICIO DE REGISTRO INTERNO (LITTLEFS) ---");
      file.close();
    } else {
      LittleFS.end(); return false;
    }
  }

  file = LittleFS.open(ruta_actual, FILE_APPEND);
  if (file) {
    String linea = "Registro: " + String(registro_numero) + " | " + payload;
    file.println(linea);
    file.close(); 
    LittleFS.end();     
    return true;  
  }
  LittleFS.end();
  return false;
}

void obtenerFechaHora(char* buffer_fecha, char* buffer_hora) {
  bool hora_actualizada = false;

  // En el pluviógrafo asumimos que el módem celular siempre está energizado
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
    
    // Reseteo a medianoche
    if (dia_anterior != -1 && dd != dia_anterior) {
      contador_lluvia = 0;
      Serial.println(">> Medianoche detectada por Red 4G. Acumulado reiniciado a 0.");
    }
    dia_anterior = dd;

    Serial.println("[OK] Hora obtenida de la red 4G.");
    hora_actualizada = true;
  }

  if (!hora_actualizada) {
    DateTime now = rtc.now();
    if (now.year() >= 2020) {
      sprintf(buffer_fecha, "%02d/%02d/%02d", now.day(), now.month(), now.year() % 100);
      sprintf(buffer_hora, "%02d:%02d:%02d", now.hour(), now.minute(), now.second());
      
      // Reseteo a medianoche si falla la red y dependemos del RTC
      if (dia_anterior != -1 && now.day() != dia_anterior) {
        contador_lluvia = 0;
        Serial.println(">> Medianoche detectada por RTC local. Acumulado reiniciado a 0.");
      }
      dia_anterior = now.day();

      Serial.println("[OK] Hora obtenida desde el RTC DS3231.");
    } else {
      strcpy(buffer_fecha, "00/00/00");
      strcpy(buffer_hora, "00:00:00");
    }
  }
}

// =========================================================================
//                   LA FUNCIÓN MAESTRA DE TRANSMISIÓN EXPRÉS
// =========================================================================

void transmitirDatos() {
  Serial.println("\n--- INICIANDO PROTOCOLO DE TRANSMISION MQTT EXPRES ---");
  
  // 1. OBTENER BATERÍA
  int pct_bateria = obtenerPorcentajeBateria();
  Serial.print("Nivel de Bateria: "); Serial.print(pct_bateria); Serial.println("%");
  
  // En el pluviógrafo, el módem siempre está energizado por el pin 12.
  // Solo despertamos el puerto serial UART
  SerialAT.begin(115200, SERIAL_8N1, MODEM_RX, MODEM_TX);
  delay(500);
  for (int i = 0; i < 3; i++) { SerialAT.println("AT"); delay(150); }

  if (!modem.isNetworkConnected()) {
    Serial.println("Esperando red Claro...");
    modem.waitForNetwork(60000L); 
  }
  esp_task_wdt_reset(); 

  // 2. RECUPERAR HORA Y REVISAR MEDIANOCHE (Red o RTC)
  char fecha[12] = "00/00/00";
  char hora[12]  = "00:00:00";
  obtenerFechaHora(fecha, hora);

  // 3. INCREMENTAR REGISTRO GLOBAL Y CREAR PAYLOAD
  registro_numero++; 
  char payload[120];
  sprintf(payload, "{\"f\":\"%s\",\"h\":\"%s\",\"d\":\"%d\",\"b\":\"%03d\"}", fecha, hora, contador_lluvia, pct_bateria);
  
  // 4. GUARDAR EN MICRO SD
  if (guardarEnMicroSD(String(payload))) {
    Serial.println("[OK] Dato respaldado en SD externa.");
  } else {
    Serial.println("[ERROR] Fallo al guardar en SD externa.");
  }

  // 5. GUARDAR EN LITTLEFS
  if (guardarEnMemoriaInterna(String(payload))) {
    Serial.println("[OK] Dato respaldado en Memoria Interna (LittleFS).");
  } else {
    Serial.println("[ERROR] Fallo al guardar en Memoria Interna.");
  }

  // 6. TRANSMISIÓN MQTT
  if (!modem.isGprsConnected()) {
    Serial.println("Conectando GPRS...");
    if (!modem.gprsConnect(apn, gprsUser, gprsPass)) return;
  }
  esp_task_wdt_reset(); 

  mqtt.setServer(mqtt_server, mqtt_port);
  if (mqtt.connect("NodoPluviografo", mqtt_user, mqtt_pass)) {
    Serial.println("[OK] MQTT Conectado! Enviando...");
    mqtt.publish(mqtt_topic, payload);
    delay(1000); 
  } else {
    Serial.println("[ERROR] Fallo MQTT.");
  }
  mqtt.disconnect();
}

// =========================================================================
//                             BUCLE PRINCIPAL
// =========================================================================

void setup() {
  esp_task_wdt_config_t wdt_config;
  wdt_config.timeout_ms = WDT_TIMEOUT * 1000; 
  wdt_config.idle_core_mask = 3;              
  wdt_config.trigger_panic = true;            
  esp_task_wdt_reconfigure(&wdt_config);      
  esp_task_wdt_add(NULL);                     

  // --- TRUCO CRÍTICO PARA EL PLUVIÓGRAFO ---
  // Imponemos el HIGH *ANTES* de quitar el candado para que no haya bajones de voltaje en el módem
  pinMode(PIN_MODEM_POWER, OUTPUT);
  digitalWrite(PIN_MODEM_POWER, HIGH); 
  gpio_hold_dis((gpio_num_t)PIN_MODEM_POWER);

  pinMode(PIN_MODEM_PWRKEY, OUTPUT);
  digitalWrite(PIN_MODEM_PWRKEY, LOW); 
  gpio_hold_dis((gpio_num_t)PIN_MODEM_PWRKEY); 

  pinMode(MODEM_TX, OUTPUT);
  digitalWrite(MODEM_TX, HIGH); 
  gpio_hold_dis((gpio_num_t)MODEM_TX);

  Serial.begin(115200);
  delay(100);

  // --- INICIALIZAR RTC DS3231 ---
  Wire.begin(); 
  if (!rtc.begin()) {
    Serial.println("[WARN] No se encontro DS3231.");
  } else if (rtc.lostPower()) {
    Serial.println("[WARN] RTC perdio energia. Configurando fecha base...");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  esp_sleep_wakeup_cause_t causa_despertar = esp_sleep_get_wakeup_cause();

  switch (causa_despertar) {
    
    // --- ESTE ES EL CORAZÓN DEL PLUVIÓMETRO (BALANCÍN) ---
    case ESP_SLEEP_WAKEUP_EXT0:
      contador_lluvia++;
      Serial.println("PULSO DE LLUVIA. Total de hoy: " + String(contador_lluvia));
      // No transmitimos aquí, simplemente anotamos el número y volvemos a dormir de inmediato
      break;

    // --- EL BOTÓN PARA EXTRAER DATOS POR WI-FI ---
    case ESP_SLEEP_WAKEUP_EXT1:
      Serial.println("[INFO] Boton presionado! Solicitud de Extraccion de Datos...");
      iniciarPortalMantenimiento();
      break;

    // --- LA ALARMA DE CADA 10 MINUTOS PARA ENVIAR AL SERVIDOR ---
    case ESP_SLEEP_WAKEUP_TIMER:
      Serial.println("[INFO] Ciclo completado. Procesando datos acumulados...");
      transmitirDatos();
      break;

    default:
      Serial.println("\n--- ARRANQUE EN FRIO ---");
      registro_numero = 0; 
      contador_lluvia = 0;
      dia_anterior = -1;
      
      Serial.println("Esperando 10s para arranque inicial automatico del modem...");
      delay(5000); esp_task_wdt_reset(); delay(5000);
      
      SerialAT.begin(115200, SERIAL_8N1, MODEM_RX, MODEM_TX);
      modem.init();
      modem.sendAT("+CTZU=1"); 
      modem.waitResponse(5000L);
      
      Serial.println("Health Check Inicial...");
      transmitirDatos();
      break;
  }
  
  // --- PREPARAR INTERRUPCIONES PARA DORMIR ---
  
  // 1. Botón Wi-Fi
  rtc_gpio_pullup_en(PIN_BOTON_WIFI); 
  rtc_gpio_pulldown_dis(PIN_BOTON_WIFI);
  esp_sleep_enable_ext1_wakeup(BOTON_WIFI_MASK, ESP_EXT1_WAKEUP_ALL_LOW);
  
  // 2. Balancín (EXT0)
  esp_sleep_enable_ext0_wakeup(PIN_REED_SWITCH, 0); // 0 significa que despierta al irse a GND

  // 3. Temporizador
  esp_sleep_enable_timer_wakeup(TIEMPO_DORMIR_SEG * uS_TO_S_FACTOR);

  Serial.println("Entrando en Deep Sleep... ZZzz");
  Serial.flush(); 
  
  SerialAT.end();
  
  // Candados para evitar glitches, ahorrar energía y mantener el módem celular encendido
  gpio_hold_en((gpio_num_t)PIN_MODEM_POWER);
  gpio_hold_en((gpio_num_t)PIN_MODEM_PWRKEY); 
  pinMode(MODEM_TX, OUTPUT);
  digitalWrite(MODEM_TX, HIGH); 
  gpio_hold_en((gpio_num_t)MODEM_TX); 

  gpio_deep_sleep_hold_en(); 
  esp_deep_sleep_start();
}

void loop() {
}
