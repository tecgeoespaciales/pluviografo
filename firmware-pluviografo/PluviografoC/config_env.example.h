#ifndef CONFIG_ENV_EXAMPLE_H
#define CONFIG_ENV_EXAMPLE_H

/* Ejemplo de configuración sensible.
 * Copia este archivo a `config_env.h` y completa los valores reales.
 * Asegúrate de NO subir `config_env.h` al repositorio.
 */

// --- CONFIGURACIÓN DE RED CELULAR (CLARO) ---
#define SECRET_APN "your.apn.here"
#define SECRET_GPRS_USER ""
#define SECRET_GPRS_PASS ""

// --- CONFIGURACIÓN DEL BROKER MQTT (SERVIDOR) ---
#define SECRET_MQTT_SERVER "mqtt.example.com"
#define SECRET_MQTT_PORT 1883
#define SECRET_MQTT_USER "your_mqtt_user"
#define SECRET_MQTT_PASS "your_mqtt_password"
#define SECRET_MQTT_TOPIC "pluviografo/yourtopic"

// --- CONFIGURACIÓN DE ACCESO LOCAL (MODO AP) ---
#define SECRET_WIFI_AP_PASS "changeme123"

// --- SEGURIDAD DE DATOS (CIFRADO) ---
#define SECRET_AES_KEY "0123456789abcdef"

#endif
