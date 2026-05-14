# Pluviógrafo — Overview

Resumen
- Proyecto: Nodo pluviógrafo basado en ESP32 + módem 4G (SIM7600 / A7670G module).
- Objetivo: medir pulsos de un pluviómetro (reed switch) y reportar por MQTT; almacenar backups en SD y LittleFS.

Estructura del repositorio
- `firmware-pluviografo/` — código del microcontrolador (ESP32).
- `hardware-pluviografo/` — esquemáticos, PCB y BOM.
- `mechanical-pluviografo/` — piezas mecánicas y modelos.
- `docs-pluviografo/` — documentación (esta carpeta).

Principales características
- Conteo persistente de pulsos en deep sleep (RTC_DATA_ATTR para persistencia).
- Reportes periódicos vía MQTT con conexión GPRS.
- Portal Wi‑Fi de mantenimiento para descarga de datos y diagnóstico.
- Registro local en MicroSD y LittleFS.

Licencia
- Revisa el archivo `LICENSE` en la raíz del repositorio.
