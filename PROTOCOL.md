# Protocolo de comunicación Arduino ↔ ESP32-CAM — Brumaire V2

## Arquitectura general

El sistema tiene dos microcontroladores con roles separados:

- **Arduino** — cerebro sensor/decisor: lee sensores, detecta aves, maneja el RTC, y envía comandos al ESP32.
- **ESP32-CAM** — módulo de captura/almacenamiento/WiFi: recibe comandos, dispara la cámara, escribe en SD, y expone un servidor HTTP.

La comunicación entre ellos es **serial UART** usando la librería [SerialTransfer](https://github.com/PowerBroker2/SerialTransfer).

---

## Capa física

| Parámetro | Valor |
|-----------|-------|
| Velocidad | 115 200 baud |
| Librería  | SerialTransfer (framing + CRC automático) |
| Dirección principal | Arduino → ESP32 |
| Dirección secundaria | ESP32 → Arduino (solo SET_TIME) |

---

## Estructura del paquete

Cada paquete (en cualquier dirección) tiene la forma:

```
[ timestamp: 6 × uint16 ]  [ cmd: uint8 ]  [ payload opcional ]
   YY  MM  DD  HH  MM  SS
```

El timestamp proviene del RTC del Arduino y se incluye en todos los mensajes.  
SerialTransfer serializa los campos en orden; el receptor los lee en el mismo orden con `rxObj`.

---

## Comandos Arduino → ESP32

| Hex  | Nombre            | Payload adicional          | Efecto en el ESP32 |
|------|-------------------|----------------------------|--------------------|
| 0x01 | CMD_TAKE_PHOTO    | —                          | Encola 3 EVT_TAKE_PHOTO en `cameraQueue` (una por cada foto del grupo) |
| 0x02 | CMD_SAVE_EVENT    | `uint8` event_code         | Encola EVT_SAVE_LOG en `sdQueue` con la línea de evento |
| 0x03 | CMD_SAVE_DATA     | `uint8` sensor_key + `float` value | Encola EVT_SAVE_LOG en `sdQueue` con la línea de sensor |
| 0x04 | CMD_HELLO         | —                          | Handshake / keepalive |
| 0x05 | CMD_LOCKARDUINO   | —                          | (reservado) |
| 0x06 | CMD_UNLOCKARDUINO | —                          | (reservado) |

### Detalle CMD_TAKE_PHOTO (0x01)

El ESP32 encola `numPhotosPerMessage = 3` eventos de cámara, cada uno con nombre de archivo:

```
/image_YY-MM-DDTHH-MM-SS_0.jpg
/image_YY-MM-DDTHH-MM-SS_1.jpg
/image_YY-MM-DDTHH-MM-SS_2.jpg
```

`vTaskCamera` captura cada frame y lo pasa a `vTaskSDCard` para escribirlo en la SD.

### Detalle CMD_SAVE_EVENT (0x02)

Payload: un byte `event_code` (ver tabla de EventCode más abajo).  
El ESP32 formatea la línea CSV y la encola para `vTaskSDCard`.

### Detalle CMD_SAVE_DATA (0x03)

Payload: `uint8 sensor_key` seguido de `float value` (4 bytes, IEEE 754).  
El ESP32 decodifica la clave con `keyLabel()` y formatea la línea CSV.

---

## ACK — ESP32 → Arduino

El ESP32 responde a **cada comando** con:

```
[ timestamp (mismo que el recibido): 6 × uint16 ]  [ cmd: uint8 ]  [ status: uint8 = 1 ]
```

`status = 1` indica éxito. No hay código de error definido en V2 (los errores se logean por ESP_LOGE pero no se retransmiten).

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
[ timestamp nuevo: 6 × uint16 ]  [ CMD_HELLO: 0x04 ]  [ mode: 0xA0 ]
```

`0xA0` es la constante `CMD_HELLO_SET_TIME`. El Arduino interpreta este modo y corrige su RTC.

**Flujo completo:**
1. App envía `POST /set_time` con `{"ts": [YY,MM,DD,HH,MM,SS]}`.
2. ESP32 guarda el timestamp en `pendingTs` y activa `pendingSetTime = true`.
3. En el próximo ciclo de `vTaskSerial`, el ESP32 transmite el CMD_HELLO + SET_TIME al Arduino.
4. Arduino corrige el RTC. El siguiente BOOT event ya tendrá timestamp válido.

---

## Códigos de eventos (EventCode)

| Hex  | Nombre   | Cuándo lo envía el Arduino |
|------|----------|---------------------------|
| 0x80 | BOOT     | En cada reinicio del sistema. **Delimita sesiones en el log.** |
| 0x81 | BIRD     | Al detectar un ave (sensor de presencia/movimiento). |
| 0x82 | PERIODIC | Log periódico (heartbeat programado). |

---

## Códigos de sensores (KeyCode)

| Hex  | Clave | Descripción |
|------|-------|-------------|
| 0x20 | T1_K  | Temperatura ambiente |
| 0x21 | T2_K  | Temperatura caja interna |
| 0x22 | T3_K  | Placa fría 1 |
| 0x23 | T4_K  | Placa fría 2 |
| 0x24 | T5_K  | Temperatura media fría |
| 0x25 | T6_K  | Temperatura objetivo |
| 0x26 | H1_K  | Humedad externa |
| 0x27 | H2_K  | Humedad interna |
| 0x28 | E1_K  | Error (proporcional) |
| 0x29 | E2_K  | Error acumulado |
| 0x2A | P1_K  | Punto de rocío |
| 0x2B | P2_K  | PWM aplicado |
| 0x2C | I1_K  | Corriente 1 |
| 0x2D | I2_K  | Corriente 2 |
| 0x2E | I3_K  | Corriente 3 |
| 0x2F | I4_K  | Corriente filtrada |
| 0x30 | W1_K  | Peso del agua |

Claves desconocidas se decodifican como `Z0_K`.

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
26-05-07T08-00-05,1,-,T1_K,24.500
26-05-07T08-00-05,2,-,H1_K,68.000
26-05-07T08-30-00,3,BIRD,-,0
26-05-07T10-00-00,140,BOOT,-,0     ← reinicio, seq continúa
26-05-07T10-00-10,141,-,T1_K,25.100
```

Tras un `/reset_log` (log.txt borrado, seq = 141 en NVS):

```
26-05-07T11-00-00,142,BOOT,-,0     ← nuevo archivo, seq no retrocede
```

---

## Contador seq

- Tipo: `uint32` guardado en NVS (namespace `"log_conf"`, clave `"log_seq"`).
- Sube 1 con cada línea escrita. **Nunca se resetea**, ni con `/reset_log`, ni tras reinicios.
- Se persiste en NVS cada 10 entradas (y cada 5 minutos por `vTaskSyncCounters`).
- Es la clave única global para el backend: permite ordenar filas aunque el RTC esté corrupto.
- Capacidad: uint32 ≈ 4.2 × 10⁹. Con un log cada 30 s → ~4 000 años.

---

## Validez del timestamp — sesiones

Una **sesión** es el conjunto de líneas entre dos BOOT events consecutivos.

Una sesión es **VÁLIDA** si el timestamp de su BOOT es estrictamente mayor al del último BOOT válido conocido. Si retrocede o es `00-00-00T00-00-00` → sesión **INCIERTA**.

```
Sesión 1  BOOT ts=26-05-07T08-00-00  ✓  baseline
Sesión 2  BOOT ts=26-05-07T10-30-00  ✓  avanza → válida
Sesión 3  BOOT ts=00-00-00T00-00-00  ✗  inválido → incierta
Sesión 4  BOOT ts=26-05-07T11-00-00  ✗  retrocede respecto a sesión 2 → incierta
Sesión 5  BOOT ts=26-05-09T07-15-00  ✓  avanza respecto a sesión 2 → válida
```

Las sesiones inciertas no se descartan: conservan sus datos y se pueden acotar temporalmente entre la última sesión válida anterior y la siguiente sesión válida posterior.

---

## Flujo de sync con la app móvil

```
Arduino ──[CMD_SAVE_EVENT / CMD_SAVE_DATA / CMD_TAKE_PHOTO]──► ESP32
                                                               │
                                                          SD: log.txt
                                                          SD: image_*.jpg

App ──[GET /list]──────────────────────────────────────────► ESP32
App ◄──[{files:[...]}]─────────────────────────────────────── ESP32

App ──[GET /download?file=log.txt]─────────────────────────► ESP32
App ◄──[contenido CSV]─────────────────────────────────────── ESP32

App ──[GET /download?file=image_*.jpg]─────────────────────► ESP32
App ◄──[bytes JPEG]────────────────────────────────────────── ESP32

App ──[GET /delete?file=image_*.jpg]───────────────────────► ESP32
App ──[POST /reset_log]────────────────────────────────────► ESP32  (log.txt borrado, seq intacto)

App ──[POST /set_time {"ts":[...]}]────────────────────────► ESP32
ESP32 ──[CMD_HELLO + SET_TIME + ts]────────────────────────► Arduino (corrige RTC)
```

---

## Archivos clave

| Rol | Path |
|-----|------|
| Firmware ESP32 | `ESP32_Serial_V2/rtos/rtos.ino` |
| Emulador Arduino (Python) | `ESP32_Serial_V2/rtos/emulator/arduino_emulator.py` |
| Flusher (descarga SD simulada) | `ESP32_Serial_V2/rtos/emulator/flush_files.py` |
| Formato log V2 (detalle) | `Protocol_Log_V2` |
