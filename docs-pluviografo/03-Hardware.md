# Hardware — Pluviógrafo

Contenido
- Esquemáticos: en `hardware-pluviografo/schematics-pluviografo/`
- PCB y gerbers: en `hardware-pluviografo/pcb-pluviografo/` y `hardware-pluviografo/gerbers-pluviografo/`
- BOM: `hardware-pluviografo/bom-pluviografo.csv`

Resumen de interfaces
- Alimentación: batería + LDO controlado por `PIN_MODEM_POWER`.
- Interfaz del pluviómetro: reed switch conectado a `PIN_REED_SWITCH` con pull-up.
- Módem 4G: UART hacia `MODEM_RX`/`MODEM_TX` y control de PWRKEY.
- MicroSD: SPI (pines definidos en firmware).

Recomendaciones de diseño
- Añadir protección transitoria en líneas externas (TVS) para protección contra sobretensiones.
- Filtros y condensadores cercanos al LDO para estabilidad.
- Asegurar que la conexión del reed esté debidamente mecanizada para evitar rebotes.

Pruebas hardware
- Verificar niveles UART con osciloscopio o comprobador de TTL.
- Test de consumo en Deep Sleep (mide µA).
