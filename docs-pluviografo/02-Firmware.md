# Firmware — Pluviógrafo

Resumen
- Plataforma: ESP32 (ESP-IDF / Arduino core)
- Módem: TinyGSM compatible (SIM7600 / A7670G)

Archivos importantes
- `firmware-pluviografo/PluviografoC/PluviografoC.ino` — firmware principal.
- `firmware-pluviografo/PluviografoC/config_env.example.h` — plantilla de credenciales (no subir secretos).
- `firmware-pluviografo/PluviografoC/config_env.h` — archivo local con secretos (gitignored).

Pins y conexiones
- `MODEM_TX` = GPIO 26
- `MODEM_RX` = GPIO 27
- `PIN_MODEM_POWER` = GPIO 12 (control de alimentación)
- `PIN_MODEM_PWRKEY` = GPIO 4 (PWRKEY)
- `PIN_REED_SWITCH` = GPIO 32 (ext0 wake)
- SD SPI: MISO=2, MOSI=15, SCLK=14, CS=13
- `PIN_BATTERY` = ADC pin 35 (lectura de batería)

Compilación y despliegue
- Requiere Arduino-ESP32 board en el IDE o PlatformIO.
- Incluir librerías: TinyGSM, PubSubClient, RTClib, LittleFS.
- Flujo básico (Arduino IDE): abrir `PluviografoC.ino`, configurar board (LilyGo A7670G/ESP32), y subir.

Configuración
- Copia `config_env.example.h` → `config_env.h` y completa `SECRET_*`.
- `TIEMPO_DORMIR_SEG` controla el intervalo de reporte (por defecto 60s en repo; ajustar según necesidades y batería).

Comportamiento
- En Deep Sleep el ESP32 despierta por timer o por reed switch (contador de lluvia).
- Al levantar por timer enciende el módem, obtiene hora, envía MQTT y apaga el módem.
- Portal Wi‑Fi: se activa por botón y permite descargar respaldos.

Notas
- No incluir secretos en commits.
- Ver `docs-pluviografo/06-Security.md` para prácticas recomendadas.
