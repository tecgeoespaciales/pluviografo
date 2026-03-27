#include <Arduino.h>
#include "driver/gpio.h" 

// --- 1. CONFIGURACIÓN DEL MÓDEM A7670G ---
#define TINY_GSM_MODEM_SIM7600  
#define MODEM_TX             26
#define MODEM_RX             27
#define PIN_MODEM_POWER      12 // Llave maestra de energía (¡Se quedará siempre HIGH!)

// --- 2. CONFIGURACIÓN DE SENSORES ---
#define PIN_REED_SWITCH      GPIO_NUM_32 
#define PIN_BATTERY          35 

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

// --- 4. CONFIGURACIÓN DE TIEMPOS ---
#define uS_TO_S_FACTOR 1000000ULL 
#define TIEMPO_DORMIR_SEG 600      // 10 minutos de Deep Sleep directo

// --- 5. MEMORIA RTC ---
RTC_DATA_ATTR int contador_lluvia = 0;
RTC_DATA_ATTR int dia_anterior = -1;  // Día previo para detectar medianoche

HardwareSerial SerialAT(1);
TinyGsm modem(SerialAT);
TinyGsmClient client(modem);
PubSubClient mqtt(client);

// --- FUNCIÓN: LEER BATERÍA CALIBRADA ---
String obtenerNivelBateria() {
  pinMode(PIN_BATTERY, INPUT);
  long suma_adc = 0;
  for(int i = 0; i < 10; i++) {
    suma_adc += analogRead(PIN_BATTERY);
    delay(10);
  }
  float adc_promedio = suma_adc / 10.0;
  float FACTOR_CALIBRACION = 1.097; 
  float voltaje_pin = (adc_promedio / 4095.0) * 3.3;
  float voltaje_bateria = voltaje_pin * 2.0 * FACTOR_CALIBRACION;

  int porcentaje = map(voltaje_bateria * 100, 320, 420, 0, 100);
  if (porcentaje > 100) porcentaje = 100;
  if (porcentaje < 0) porcentaje = 0;

  char bat_str[4];
  sprintf(bat_str, "%03d", porcentaje);
  return String(bat_str);
}

// --- FUNCIÓN: OBTENER FECHA Y HORA DE LA RED MÓVIL (TinyGSM) ---
// Llena los buffers fecha (dd/mm/aa) y hora (hh:mm:ss).
// Detecta cambio de día para reiniciar el acumulado a medianoche.
void obtenerFechaHoraRed(char* fecha, char* hora) {
  // getGSMDateTime(DATE_FULL) → "yy/MM/dd,hh:mm:ss±zz"
  String dt = modem.getGSMDateTime(DATE_FULL);
  Serial.print("getGSMDateTime: "); Serial.println(dt);

  if (dt.length() >= 17) {
    String aa  = dt.substring(0, 2);
    String mm  = dt.substring(3, 5);
    String dd  = dt.substring(6, 8);
    String hms = dt.substring(9, 17);

    sprintf(fecha, "%s/%s/%s", dd.c_str(), mm.c_str(), aa.c_str());
    strcpy(hora, hms.c_str());

    // Detectar cambio de día (medianoche) → reiniciar acumulado
    int dia_actual = dd.toInt();
    if (dia_anterior != -1 && dia_actual != dia_anterior) {
      contador_lluvia = 0;
      Serial.println(">> Medianoche detectada. Acumulado reiniciado a 0.");
    }
    dia_anterior = dia_actual;

    Serial.print("Fecha: "); Serial.println(fecha);
    Serial.print("Hora:  "); Serial.println(hora);
  } else {
    strcpy(fecha, "00/00/00");
    strcpy(hora, "00:00:00");
    Serial.println("ADVERTENCIA: No se pudo obtener hora de la red.");
  }
}

// --- FUNCIÓN DE TRANSMISIÓN EXPRÉS (Sin reiniciar nada) ---
void transmitirDatos() {
  delay(2500);
  Serial.println("\n--- INICIANDO TRANSMISIÓN MQTT EXPRÉS ---");
  
  // 1. Iniciar el puerto serial
  SerialAT.begin(115200, SERIAL_8N1, MODEM_RX, MODEM_TX);
  delay(500);

  // 2. Despertar el UART del módem
  for (int i = 0; i < 3; i++) {
    SerialAT.println("AT");
    delay(150);
  }

  // 3. Revisar conexión Celular (Claro)
  if (!modem.isNetworkConnected()) {
    Serial.print("Reconectando a la red Claro...");
    if (!modem.waitForNetwork(60000L)) {
      Serial.println(" FALLO. Se intentará en el próximo ciclo.");
      return; 
    }
    Serial.println(" OK");
  }

  // 4. Revisar conexión GPRS (Internet)
  if (!modem.isGprsConnected()) {
    Serial.print("Conectando GPRS...");
    if (!modem.gprsConnect(apn, gprsUser, gprsPass)) {
      Serial.println(" FALLO de APN.");
      return;
    }
    Serial.println(" OK");
  }

  // 5. Obtener fecha y hora de la red móvil
  char fecha[12] = "00/00/00";
  char hora[12]  = "00:00:00";
  obtenerFechaHoraRed(fecha, hora);

  // 6. Enviar MQTT con nuevo payload
  mqtt.setServer(mqtt_server, mqtt_port);
  Serial.print("Conectando al broker MQTT...");
  
  if (mqtt.connect("NodoPluviografo", mqtt_user, mqtt_pass)) {
    Serial.println(" CONECTADO!");
    
    String nivel_bat = obtenerNivelBateria();
    char payload[120];
    sprintf(payload, "{\"f\":\"%s\",\"h\":\"%s\",\"d\":\"%d\",\"b\":\"%s\"}",
            fecha, hora, contador_lluvia, nivel_bat.c_str());
    
    Serial.print("Enviando: ");
    Serial.println(payload);
    mqtt.publish(mqtt_topic, payload);
    delay(1000); 
  } else {
    Serial.println(" FALLO MQTT.");
  }

  // 7. Cierre Limpio
  mqtt.disconnect();
  Serial.println("Transmisión finalizada. Módem se queda encendido.");
}

// --- BUCLE PRINCIPAL (SETUP) ---
void setup() {
  // --- TRUCO CRÍTICO PARA EVITAR REINICIOS ---
  // Imponemos el HIGH *ANTES* de quitar el candado para que no haya bajones de voltaje
  pinMode(PIN_MODEM_POWER, OUTPUT);
  digitalWrite(PIN_MODEM_POWER, HIGH); 
  gpio_hold_dis((gpio_num_t)PIN_MODEM_POWER);
  
  Serial.begin(115200);
  delay(100);

  esp_sleep_wakeup_cause_t causa_despertar = esp_sleep_get_wakeup_cause();

  switch (causa_despertar) {
    
    case ESP_SLEEP_WAKEUP_EXT0:
      contador_lluvia++;
      Serial.println("PULSO DE LLUVIA. Total: " + String(contador_lluvia));
      break;

    case ESP_SLEEP_WAKEUP_TIMER:
      Serial.println("Despertar por temporizador (10 min). Transmitiendo...");
      transmitirDatos();
      break;

    default:
      Serial.println("\n--- ARRANQUE EN FRÍO ---");
      contador_lluvia = 0;
      dia_anterior = -1;
      
      // Como es el primer encendido físico, le damos tiempo al módem de arrancar solo
      Serial.println("Esperando 10 segundos para que el módem haga Auto-Boot...");
      delay(10000);
      
      // Inicializamos el módem UNA SOLA VEZ
      SerialAT.begin(115200, SERIAL_8N1, MODEM_RX, MODEM_TX);
      Serial.print("Inicializando software del módem...");
      modem.init();
      Serial.println(" Listo. A partir de ahora quedará encendido permanentemente.");

      // Habilitar sincronización automática de hora por red celular
      modem.sendAT("+CTZU=1");
      modem.waitResponse(5000L);
      Serial.println("CTZU=1 habilitado (sync hora por red).");
      break;
  }
  
  // Preparar alarmas
  esp_sleep_enable_ext0_wakeup(PIN_REED_SWITCH, 0);
  esp_sleep_enable_timer_wakeup(TIEMPO_DORMIR_SEG * uS_TO_S_FACTOR);

  Serial.println("Entrando en Deep Sleep...");
  Serial.flush(); 

  // PONER CANDADO: Forzamos físicamente a que el Pin 12 siga enviando 3.3V al LDO
  gpio_hold_en((gpio_num_t)PIN_MODEM_POWER);
  gpio_deep_sleep_hold_en(); 

  esp_deep_sleep_start();
}

void loop() {
}
