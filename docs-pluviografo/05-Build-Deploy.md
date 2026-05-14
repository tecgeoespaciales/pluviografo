# Build & Deploy

Requisitos
- Arduino IDE con core `esp32` o PlatformIO.
- Librerías: `TinyGSM`, `PubSubClient`, `RTClib`, `LittleFS`.

Pasos (Arduino IDE)
1. Abrir `firmware-pluviografo/PluviografoC/PluviografoC.ino`.
2. Copiar `config_env.example.h` → `config_env.h` y editar `SECRET_*`.
3. Seleccionar placa compatible (e.g., LilyGo A7670G / ESP32 board).
4. Compilar y subir.

Pasos (PlatformIO)
- Añadir `platformio.ini` con `platform = espressif32` y `board = esp32dev` (o el board específico).
- `pio run -t upload` para compilar y flashear.

Notas de despliegue
- Pruebas locales: usar monitor serial a 115200 baudios.
- Antes de desplegar en campo, verificar la conexión GPRS y el tópico MQTT.
- Ajustar `TIEMPO_DORMIR_SEG` para optimizar consumo.
