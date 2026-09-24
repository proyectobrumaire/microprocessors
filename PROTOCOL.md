# Protocolo de comunicación Arduino ↔ ESP32-CAM — Brumaire V2

## Arquitectura general

El sistema tiene dos microcontroladores con roles separados:

- **Arduino Mega** (`Full_Condenser/`) — cerebro sensor/decisor: lee sensores, ejecuta el control de las celdas Peltier, detecta aves con el ultrasonido, maneja el RTC (PCF8583) y envía comandos al ESP32.
- **ESP32-CAM** (`ESP32_Serial_V2/rtos/rtos.ino`) — módulo de captura/almacenamiento/WiFi: recibe comandos, dispara la cámara, escribe en la SD y expone un servidor HTTP para la app móvil.

La comunicación entre ellos es **serial UART** usando la librería [SerialTransfer](https://github.com/PowerBroker2/SerialTransfer).

---

## Capa física

| Parámetro | Valor |
|-----------|-------|
| Puerto Arduino | `Serial3` (Mega) |
| Puerto ESP32 | `Serial` (UART0) |
| Velocidad | 115 200 baud |
| Librería  | SerialTransfer (framing + CRC automático) |
| Dirección principal | Arduino → ESP32 (comandos) |
| Dirección secundaria | ESP32 → Arduino (ACK y SET_TIME) |

El `Serial` del Arduino (9600 baud) es solo para depuración por USB.

---

## Estructura del paquete

Cada paquete (en cualquier dirección) tiene la forma:

```
[ timestamp: 6 × uint16 ]  [ cmd: uint8 ]  [ payload opcional ]
   YY  MM  DD  HH  MM  SS
```

- El timestamp proviene del RTC del Arduino y se incluye en todos los mensajes.
- Todos los paquetes de un mismo evento comparten el mismo timestamp (se lee el RTC una sola vez por evento).
- SerialTransfer serializa los campos en orden; el receptor los lee en el mismo orden con `rxObj`.
- El RTC guarda **hora local** (la app lo sincroniza con la hora del teléfono; en Colombia, UTC−5). El backend convierte a UTC con `RTC_UTC_OFFSET_H`.

> El año siempre se envía como `YY` (`p.year % 100`). La librería PCF8583 devuelve el año con el mismo formato con que se configuró (26 o 2026), porque lo guarda como `year_base` en la RAM del RTC más un contador de 2 bits.
>
> ⚠ Ese contador solo cubre 4 años desde `year_base` y la librería no actualiza la base al desbordarse: si el RTC se configuró en 2024–2027, el 1 de enero de 2028 volvería a marcar `24`. Se corrige solo en la siguiente sincronización de hora desde la app.

---

## Comandos Arduino → ESP32

| Hex  | Nombre         | Payload adicional                  | Efecto en el ESP32 |
|------|----------------|------------------------------------|--------------------|
| 0x01 | CMD_TAKE_PHOTO | —                                  | Encola 3 EVT_TAKE_PHOTO en `cameraQueue` (una por cada foto del grupo) |
| 0x02 | CMD_SAVE_EVENT | `uint8` event_code                 | Encola EVT_SAVE_LOG en `sdQueue` con la línea de evento |
| 0x03 | CMD_SAVE_DATA  | `uint8` sensor_key + `float` value | Encola EVT_SAVE_LOG en `sdQueue` con la línea de sensor |
| 0x04 | CMD_HELLO      | `uint8` modo                       | Solo en dirección ESP32 → Arduino (ver SET_TIME) |

Cualquier otro código se ignora (`ESP_LOGE "Invalid Format recieved for command"`, sin ACK).

### Detalle CMD_TAKE_PHOTO (0x01)

El ESP32 encola `numPhotosPerMessage = 3` eventos de cámara, cada uno con nombre de archivo:

```
/image_YY-MM-DDTHH-MM-SS_0.jpg
/image_YY-MM-DDTHH-MM-SS_1.jpg
/image_YY-MM-DDTHH-MM-SS_2.jpg
```

`vTaskCamera` captura cada frame (UXGA, JPEG calidad 10, con 1 s entre fotos) y lo pasa a `vTaskSDCard` para escribirlo en la SD.

### Detalle CMD_SAVE_EVENT (0x02)

Payload: un byte `event_code` (ver tabla de EventCode más abajo).
El ESP32 formatea la línea CSV y la encola para `vTaskSDCard`.

### Detalle CMD_SAVE_DATA (0x03)

Payload: `uint8 sensor_key` seguido de `float value` (4 bytes, IEEE 754).
El ESP32 decodifica la clave con `keyLabel()` y formatea la línea CSV.

---

## Secuencia de un evento (Arduino)

`CondenserCom::when_event(TYPE, valores)` envía, en este orden y con el mismo timestamp:

1. `CMD_TAKE_PHOTO` — **solo si** `TYPE == BIRD`.
2. `CMD_SAVE_EVENT` con `TYPE`.
3. 11 × `CMD_SAVE_DATA`, uno por sensor, en el orden de `kAllKeys` (ver tabla de sensores).

Los valores de sensores son el **promedio de la ventana** desde el último reporte (`CondenserControl::promediar`). Si no hubo muestras nuevas desde el reporte anterior, se reenvía el último promedio válido.

Tras cada paquete el Arduino espera el ACK (`wait_for_ack`, timeout 10 s). Si no llega, **sigue enviando igual** (no reintenta). Si falla el ACK del primer `SAVE_DATA`, deja de esperar ACK en los 10 restantes. Peor caso con el ESP32 caído: ~30 s de bloqueo en un BIRD, ~20 s en los demás eventos.

---

## ACK — ESP32 → Arduino

El ESP32 responde a cada comando válido con:

```
[ timestamp (mismo que el recibido): 6 × uint16 ]  [ cmd: uint8 ]  [ status: uint8 = 1 ]
```

`status = 1` indica que el comando fue encolado. No hay código de error definido en V2: los errores (cola llena, SD no disponible, fallo de cámara) se logean con `ESP_LOGE` pero no se retransmiten. En particular, un ACK de `CMD_TAKE_PHOTO` **no** garantiza que las fotos se hayan escrito.

```cpp
void sendACK(uint16_t ts[6], uint8_t cmd, uint8_t status) {
    delay(5);
    uint16_t len = 0;
    len = linkReciever.txObj(ts, len, 6 * sizeof(uint16_t));
    len = linkReciever.txObj(cmd, len);
    len = linkReciever.txObj(status, len);
    linkReciever.sendData(len);
}
```

---

## Comando especial ESP32 → Arduino: SET_TIME

Cuando la app móvil sincroniza la hora vía HTTP (`POST /set_time`), el ESP32 reenvía la hora al Arduino por serial:

```
[ timestamp nuevo: 6 × uint16 ]  [ CMD_HELLO: 0x04 ]  [ modo: 0xA0 ]
```

| Hex  | Modo (HelloModes) | Estado |
|------|-------------------|--------|
| 0xA0 | SET_TIME          | Implementado: el Arduino escribe el timestamp en el RTC |
| 0xA1 | STATUS            | Definido en el Arduino, sin implementar |

**Flujo completo:**
1. App envía `POST /set_time` con `{"ts": [YY,MM,DD,HH,MM,SS]}` (hora local del teléfono).
2. ESP32 guarda el timestamp en `pendingTs` y activa `pendingSetTime = true`.
3. En el próximo ciclo de `vTaskSerial`, el ESP32 transmite el CMD_HELLO + SET_TIME al Arduino.
4. El Arduino lo atiende en `recieve_commands()` (al inicio de cada `loop()`) y corrige el RTC. No hay ACK.

---

## Códigos de eventos (EventCode)

| Hex  | Nombre      | Cuándo lo envía el Arduino |
|------|-------------|----------------------------|
| 0x80 | BOOT        | Al final de `setup()`. **Delimita sesiones en el log.** |
| 0x81 | BIRD        | El ultrasonido detecta un objeto (eco entre 50 y 2500 µs ≈ 1–43 cm). Cooldown de 6 s entre detecciones. Incluye fotos. |
| 0x82 | PERIODIC    | En cada interrupción del timer del RTC (cada 5 min). |
| 0x83 | PELTIER_ON  | El control pasa a condición viable y activa el PI. También cuando un sensor del control da NaN (la celda queda a PWM 255). |
| 0x84 | PELTIER_OFF | La condensación deja de ser viable (`T_amb ≥ rocío + ~19 °C`). Histéresis de 1 °C para volver a PELTIER_ON. |
| 0x85 | VOLCADO     | Tras terminar la secuencia de vaciado y relleno del plato (se evalúa en cada tick del timer). |

En el ESP32, un código desconocido se registra como `INVALID_EV`.

`PELTIER_ON/OFF` reflejan la **viabilidad** del control, no si el PWM es mayor que 0: con el PI activo el PWM puede valer 0. La pausa del vaciado no genera PELTIER_OFF.

---

## Códigos de sensores (KeyCode)

El Arduino envía estas 11 claves, en este orden, en cada evento:

| Hex  | Clave | Descripción | Fuente |
|------|-------|-------------|--------|
| 0x20 | T1_K  | Temperatura ambiente (°C) | DHT22 externo |
| 0x21 | T2_K  | Temperatura caja interna (°C) | DHT22 interno |
| 0x22 | T3_K  | Placa fría 1 (°C) | Termocupla MAX31855 #1 |
| 0x23 | T4_K  | Placa fría 2 (°C) | Termocupla MAX31855 #2 |
| 0x24 | T5_K  | Temperatura media fría (°C) | Promedio T3/T4 |
| 0x26 | H1_K  | Humedad externa (%) | DHT22 externo |
| 0x27 | H2_K  | Humedad interna (%) | DHT22 interno |
| 0x2A | P1_K  | Punto de rocío (°C, con ajuste −5 °C) | Calculado (Magnus) |
| 0x2B | P2_K  | PWM aplicado a la Peltier (0–255) | Salida del control |
| 0x2F | I4_K  | Corriente filtrada (A) | ACS712 20 A |
| 0x30 | W1_K  | Peso del agua | Balanza HX711 (unidades según calibración) |

Claves desconocidas se decodifican como `Z0_K`. Los códigos 0x25 (T6_K), 0x28 (E1_K), 0x29 (E2_K) y 0x2C–0x2E (I1_K–I3_K) pertenecen a versiones anteriores y ya no se envían; la app conserva sus etiquetas para leer logs antiguos.

---

## Formato del log en SD — V2

El archivo es `/log.txt` en la raíz de la SD, en modo append.

### Línea de evento

```
YY-MM-DDTHH-MM-SS,<seq>,<EVENT_NAME>,-,0
```

### Línea de sensor

```
YY-MM-DDTHH-MM-SS,<seq>,-,<SENSOR_KEY>,<value>
```

`value` se formatea con 3 decimales (`%.3f`).

### Ejemplos

```
26-05-07T08-00-00,0,BOOT,-,0
26-05-07T08-00-00,1,-,T1_K,24.500
26-05-07T08-00-00,2,-,T2_K,27.800
...
26-05-07T08-05-00,12,PERIODIC,-,0
26-05-07T08-05-00,13,-,T1_K,24.600
...
26-05-07T08-30-00,48,BIRD,-,0
26-05-07T10-00-00,140,BOOT,-,0     ← reinicio, seq continúa
```

Tras un `/reset_log` (log.txt borrado, próximo seq = 151):

```
26-05-07T11-00-00,151,PERIODIC,-,0     ← nuevo archivo, seq no retrocede
```

---

## Contador seq

- Tipo: `uint32` guardado en NVS (namespace `"log_conf"`, clave `"log_seq"`).
- Sube 1 con cada línea escrita. **No se resetea** con `/reset_log` ni en reinicios normales.
- Se persiste en NVS cada 10 entradas y cada 5 minutos (`vTaskSyncCounters`).
- Es la clave única para ordenar filas aunque el RTC esté corrupto. La app descarta las líneas con `seq ≤` al máximo que ya tiene guardado.
- Capacidad: uint32 ≈ 4.2 × 10⁹ líneas.

> ⚠ Limitación conocida: tras un reinicio inesperado (corte de luz, watchdog) el contador puede retroceder hasta 9 valores (los no persistidos). Esas líneas repiten `seq` y la app las descarta.

---

## Validez del timestamp — sesiones

Una **sesión** es el conjunto de líneas entre dos BOOT events consecutivos.

Regla de diseño: una sesión es **VÁLIDA** si el timestamp de su BOOT es estrictamente mayor al del último BOOT válido conocido. Si retrocede o es `00-00-00T00-00-00` → sesión **INCIERTA**.

```
Sesión 1  BOOT ts=26-05-07T08-00-00  ✓  baseline
Sesión 2  BOOT ts=26-05-07T10-30-00  ✓  avanza → válida
Sesión 3  BOOT ts=00-00-00T00-00-00  ✗  inválido → incierta
Sesión 4  BOOT ts=26-05-07T11-00-00  ✗  retrocede respecto a sesión 2 → incierta
Sesión 5  BOOT ts=26-05-09T07-15-00  ✓  avanza respecto a sesión 2 → válida
```

Las sesiones inciertas no se descartan: conservan sus datos y se pueden acotar temporalmente entre la última sesión válida anterior y la siguiente sesión válida posterior.

> Estado actual: esta regla **no está implementada**. La app solo valida cada línea por separado (año 2020–2035, mes, día y hora en rango) y marca `timestamp_valid = false` si no cumple. El backend (`log_processor`) descarta las líneas con timestamp inválido.

---

## Flujo de sync con la app móvil

La app separa la sincronización en dos botones:

**1. "Descargar SD"** (teléfono en la misma red que el ESP32)

```
App ──[POST /set_time {"ts":[...]}]────────────────────────► ESP32
ESP32 ──[CMD_HELLO + SET_TIME + ts]────────────────────────► Arduino (corrige RTC)

App ──[GET /list]──────────────────────────────────────────► ESP32
App ◄──[{files:[...], count, truncated}]──────────────────── ESP32   (máx. 20 archivos)

Por cada foto:
App ──[GET /download?file=image_*.jpg]─────────────────────► ESP32
App ◄──[bytes JPEG]────────────────────────────────────────── ESP32
App ──[GET /delete?file=image_*.jpg]───────────────────────► ESP32

Si log.txt aparece en /list:
App ──[GET /download?file=log.txt]─────────────────────────► ESP32
App ◄──[contenido CSV]─────────────────────────────────────── ESP32
App ──[POST /reset_log]────────────────────────────────────► ESP32  (log.txt borrado, seq intacto)
```

Todo queda en SQLite local. `/list` no pagina: si la SD tiene más de 20 archivos, `log.txt` puede no aparecer y se descarga en una sincronización posterior (las fotos se van borrando en cada una).

**2. "Subir al servidor"** (con internet): sube a S3 las fotos y líneas de log pendientes, usando URLs firmadas del presigner.

---

## Archivos clave

| Rol | Path |
|-----|------|
| Firmware Arduino | `Full_Condenser/Full_Condenser.ino`, `Communication.*`, `CondenserControl.*` |
| Firmware ESP32 | `ESP32_Serial_V2/rtos/rtos.ino` |
| Emulador Arduino (Python) | `ESP32_Serial_V2/rtos/emulator/arduino_emulator.py` |
| Flusher (descarga SD simulada) | `ESP32_Serial_V2/rtos/emulator/flush_files.py` |
| Formato log V2 (detalle) | `Protocol_Log_V2` |
