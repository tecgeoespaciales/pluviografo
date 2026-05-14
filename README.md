# Pluviógrafo

Proyecto: nodo pluviógrafo de bajo consumo para registro y transmisión de precipitación.

Resumen
- Plataforma: ESP32 con soporte para módulos 4G (TinyGSM compatible — e.g., SIM7600 / A7670G).
- Objetivo: contar pulsos de un pluviómetro (reed switch), almacenar respaldos en MicroSD/LittleFS y enviar reportes por MQTT usando GPRS.
- Enfoque: consumo ultra-bajo usando Deep Sleep, diseño robusto para campo, portal Wi‑Fi para mantenimiento.

Características principales
- Conteo persistente de pulsos con memoria RTC (`RTC_DATA_ATTR`).
- Reportes periódicos por MQTT (GPRS) y almacenamiento local en MicroSD y LittleFS.
- Portal Wi‑Fi para diagnóstico y descarga de registros.
- Gestión inteligente del módem (encendido/apagado para ahorrar energía).
- Plantilla de configuración separada (`config_env.example.h`) y manejo seguro de secretos.

Estructura del repositorio
- `docs-pluviografo/` — documentación del proyecto.
- `firmware-pluviografo/` — código fuente del firmware (ESP32).
- `hardware-pluviografo/` — esquemáticos, PCB, BOM.
- `mechanical-pluviografo/` — piezas mecánicas y modelos CAD.

Documentación (leer en orden recomendado)
- Overview: [docs-pluviografo/01-Overview.md](docs-pluviografo/01-Overview.md)
- Firmware: [docs-pluviografo/02-Firmware.md](docs-pluviografo/02-Firmware.md)
- Hardware: [docs-pluviografo/03-Hardware.md](docs-pluviografo/03-Hardware.md)
- Mechanical: [docs-pluviografo/04-Mechanical.md](docs-pluviografo/04-Mechanical.md)
- Build & Deploy: [docs-pluviografo/05-Build-Deploy.md](docs-pluviografo/05-Build-Deploy.md)
- Security: [docs-pluviografo/06-Security.md](docs-pluviografo/06-Security.md)
- Contributing: [docs-pluviografo/07-Contributing.md](docs-pluviografo/07-Contributing.md)
- FAQ: [docs-pluviografo/08-FAQ.md](docs-pluviografo/08-FAQ.md)
- Changelog: [docs-pluviografo/09-Changelog.md](docs-pluviografo/09-Changelog.md)

Rápida descripción del firmware
- Archivo principal: `firmware-pluviografo/PluviografoC/PluviografoC.ino` — contiene la lógica de conteo, gestión de energía, backups y transmisión MQTT.
- Configuración sensible: copia `config_env.example.h` → `config_env.h` y rellena `SECRET_*` (archivo gitignored).
- Pines clave: `MODEM_TX`=26, `MODEM_RX`=27, `PIN_MODEM_POWER`=12, `PIN_REED_SWITCH`=32, SD SPI pines definidoss en el firmware.

Cómo empezar (Quickstart)
1. Clona el repositorio: `git clone https://github.com/tecgeoespaciales/pluviografo.git`
2. Abre `firmware-pluviografo/PluviografoC/PluviografoC.ino` en Arduino IDE o PlatformIO.
3. Copia `config_env.example.h` → `firmware-pluviografo/PluviografoC/config_env.h` y completa `SECRET_*`.
4. Selecciona la placa ESP32 adecuada y sube el firmware.

Seguridad y manejo de secretos
- Nunca comitees `config_env.h`. Usa `config_env.example.h` como plantilla.
- Si un secreto fue expuesto, rota las credenciales y purga el historial (ver `docs-pluviografo/06-Security.md`).

Contribuir
- Abre issues para problemas o mejoras.
- Crea ramas por feature y haz pull requests hacia `profesional`.
- Sigue las guías en [docs-pluviografo/07-Contributing.md](docs-pluviografo/07-Contributing.md).

Licencia
- Revisa el archivo `LICENSE` en la raíz del repositorio para detalles de licencia.

Contacto
- Equipo/Autor: TecGeo Espaciales (ver historial de commits para autoría).

¿Quieres desplegar la documentación como sitio estático (GitHub Pages / MkDocs)? Abro un PR con la configuración si lo deseas.
