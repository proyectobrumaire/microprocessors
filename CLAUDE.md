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
- **Volcado: reposo 80° (78 calibrado + margen), volcado 0°**; **válvula: 90° = cerrada, 0° = abierta** (calibrado con `tests/volcado_test`).
- **Rutina de vaciado** (`volcar_plato_y_renovar`, ~37 s): volcado toma el plato en reposo → soltar seguro → 5 s → volcar → 5 s → reposo → 5 s → trabar → soltar volcado → válvula 15 s. El plato nunca queda sin sujeción y la válvula solo abre con el plato trabado.
- **Control**: objetivo de la placa = rocío real − `MARGEN_BAJO_ROCIO` (8 °C), nunca menor a `TEMP_PLACA_MIN` (2 °C, contra escarcha). `P1_K` reporta el rocío real.
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

Antes de desplegar en campo:
- `Debug.h`: `DEBUG 0`.
- `volcado_interval_min = 2` es valor de prueba; el vaciado solo se evalúa en cada tick del RTC (5 min) y usa `millis()` (se reinicia con cada reset). Para producción: hora fija del día con el RTC.
- Verificar un vaciado completo de `Full_Condenser` con agua.

Arduino:
- Válvula en el pin 13 (LED de la placa): el bootloader lo hace parpadear en cada reinicio y la válvula puede moverse; mover a otro pin libre (A2 o D8).
- Anti-windup: el integral sigue acumulando con el PWM saturado.
- Pulso del ultrasonido por `millis()` en vez de `delay(60)`.

ESP32:
- `seq` puede retroceder tras un reset inesperado (proponer +10 al arrancar).
- Si la cámara falla al iniciar (NACK del sensor por SCCB), queda sin cámara hasta el próximo reset y los ACK siguen diciendo OK. Proponer reintentar el init y reinicializar si falla una captura.
- Si falla una escritura en la SD (p. ej. `sdmmc_read_blocks failed (0x107)`), no se recupera. Proponer remontar la SD y reintentar.
- Ambos síntomas aparecen alimentando el ESP32-CAM desde el USB del PC (alimentación débil; el detector de brownout está desactivado en `setup()`). Probar con fuente de 5 V ≥ 2 A.

## API HTTP del ESP32

Contrato con la app en `~/Brumaire/.claude/contracts/esp32-http.md`. `/list` responde `500 Failed to open Dir` cuando la SD no responde; `POST /reboot` reinicia la placa (guarda `seq` antes). La pila del servidor HTTP es de 8 KB (`STACK_HTTP`).

## Pruebas del ESP32

- `ESP32_Serial_V2/rtos/emulator/test_protocolo.py`: emula al Arduino por serial y verifica cada ACK (cmd, status y timestamp). `--foto` incluye un BIRD; `--host` verifica `log.txt` por HTTP. Escribe eventos de prueba en el `log.txt` real.
- `arduino_emulator.py`: menú interactivo para disparar eventos a mano.
- Entorno: `python3 -m venv .venv && .venv/bin/pip install pyserial pySerialTransfer requests` dentro de `emulator/`.
- Programar el ESP32-CAM: `arduino-cli upload --fqbn esp32:esp32:esp32cam -p /dev/ttyUSB0` (placa con adaptador tipo ESP32-CAM-MB; si es un FTDI suelto, puentear IO0–GND al subir).
