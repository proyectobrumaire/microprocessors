# Brumaire — firmware (microprocessors)

Estación Brumaire: condensador atmosférico (celdas Peltier) que alimenta un bebedero para aves y fotografía a las aves que llegan. Un solo sistema en 3 repos:

- **microprocessors** (este): Arduino Mega (`Full_Condenser/`) + ESP32-CAM (`ESP32_Serial_V2/rtos/rtos.ino`).
- **mobile**: app Flutter que descarga la SD del ESP32 por WiFi y sube a S3.
- **bird-classification**: AWS (Terraform) — clasificación de especies y telemetría en DynamoDB.

Código, comentarios y commits en español.

## Arquitectura

- **Arduino Mega**: sensores, control PI de la Peltier, ventiladores, servos del plato, RTC PCF8583, ultrasonido (detección de aves). Envía eventos al ESP32 por `Serial3` con SerialTransfer.
- **ESP32-CAM**: FreeRTOS; toma 3 fotos por BIRD, escribe `log.txt` en la SD, servidor HTTP (`/list`, `/download`, `/delete`, `/reset_log`, `/set_time`, `/wifi`).
- Protocolo serial, eventos, claves de sensor y formato del log: **`PROTOCOL.md`** (mantenerlo al día si cambia el firmware).

## Compilar y cargar

- Mega: `arduino-cli compile --fqbn arduino:avr:mega Full_Condenser` / `upload -p /dev/ttyACM0`.
- Librerías: las de `libraries.zip` (instalar en `~/Arduino/libraries`). **PCF8583 es una versión modificada** (tiene `set_timer`, `clear_timer_flags`); no usar la del gestor de librerías. `Servo`, `EEPROM`, `Wire`, `SPI` vienen con el core AVR.
- Abrir el puerto serie reinicia el Mega (DTR). Solo un programa puede tener el puerto abierto (cerrar el Monitor Serie del IDE antes de cargar).
- `Full_Condenser/Debug.h`: **`DEBUG 0` en operación** (`Serial.print` bloquea aunque no haya USB; con DEBUG 1 el Serial USB va a 115200).
- `tests/`: sketches de diagnóstico (`tc_diag`, `seguro_test`, `tare_balanza`). Dejan la Peltier y los ventiladores apagados. Después de usarlos, volver a cargar `Full_Condenser`.

## Datos de hardware verificados

- Pines: ver `ctrlCom` y `pinsCom` en `Full_Condenser.ino`. Servos: seguro = pin 10, volcado = pin 11, válvula = pin 13.
- **Seguro: 0° = trabado, 90° = suelto** (`SEGURO_TRABADO`/`SEGURO_SUELTO`, verificado con `tests/seguro_test`).
- **Termocuplas (MAX31855)**: las puntas tocan la placa fría y el chip marca "corto a GND" de forma intermitente (ciclo externo de ~29 s, independiente del Arduino y de la Peltier). La lectura es válida → se usa `setFaultChecks(MAX31855_FAULT_OPEN)`. Pendiente aislar las puntas.
- **Balanza (HX711)**: unidades en gramos (escala −422.55). Tara guardada en EEPROM; si cambia lo que va sobre la celda, re-tarar con `tests/tare_balanza` (celda vacía).
- **ACS712**: la corriente sale negativa (sensor montado al revés).
- **RTC**: se sincroniza con la hora local del teléfono (Colombia, UTC−5) vía `/set_time`. El año se envía como `YY` (`p.year % 100`). La librería no actualiza `year_base` al desbordar el contador de 2 bits (p. ej. 1-ene-2028); se corrige en la siguiente sincronización.
- El punto de rocío depende del DHT externo (`dht2`); si da NaN, el control no corre.

## Decisiones tomadas (no re-proponer)

- Si un sensor del control da NaN, la Peltier queda a **PWM 255** (decisión consciente).
- No se detectan fallas de la balanza; si se desconecta, `W1` repite el último valor y el resto sigue logueando.
- El relleno del plato es por tiempo (válvula 3 s), **sin** usar la balanza.
- Sensor de lluvia eliminado.

## Pendientes conocidos

- `volcado_interval_min = 2` es valor de prueba; el vaciado solo se evalúa en cada tick del RTC (5 min) y usa `millis()` (se reinicia con cada reset). Para producción: hora fija del día con el RTC.
- Secuencia de servos del vaciado: hay ventanas en que el plato no está sujeto por ningún servo (soltar seguro antes de conectar volcado; rellenar con el seguro suelto). Propuesta: conectar volcado → soltar → volcar → trabar → desconectar → rellenar. Pendiente probar con el volcado acoplado.
- Anti-windup: el integral sigue acumulando con el PWM saturado.
- ESP32: `seq` puede retroceder tras un reset inesperado (proponer +10 al arrancar); `download_file_handler` no libera `SDMutex` si falla el `malloc`; `sendACK` envía basura como timestamp (recibe `ts` como puntero).
