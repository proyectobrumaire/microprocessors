# Para instalar la board del esp32 CAM en el IDE de Arduino:
https://circuitdigest.com/microcontroller-projects/how-to-program-esp32-cam-using-arduino

# Interacción con el ESP32 mediante `curl`

Este documento reúne los comandos `curl` más comunes para gestionar el ESP32.

## Modos WiFi

El ESP32 intenta conectarse a la red guardada (STA). Si falla, levanta un AP de configuración.

- **STA**: accesible en `http://esp32cam.local` (mDNS) o por su IP
- **AP**: SSID `ESP32_CONFIG_CAM`, password `12345678`, IP `192.168.4.1`

Los endpoints `/list`, `/download`, `/delete` y `/reset_log` solo están disponibles en modo STA.
`/wifi` y `/set_time` están disponibles siempre.

---

## 1. Configurar red WiFi
```bash
curl -X POST "http://192.168.4.1/wifi" \
  -H "Content-Type: application/json" \
  -d '{"ssid": "MiRed", "password": "MiPassword"}'
```
El ESP32 guarda las credenciales en NVS y reinicia.

## 2. Sincronizar hora
```bash
curl -X POST "http://esp32cam.local/set_time" \
  -H "Content-Type: application/json" \
  -d '{"ts": [26, 5, 10, 14, 30, 0]}'
```
Formato: `[YY, MM, DD, HH, MM, SS]`

## 3. Listar archivos de la SD
```bash
curl "http://esp32cam.local/list"
```
Respuesta: `{"files":[{"name":"...","size":N},...], "count":N, "truncated":false}`

Máximo 20 archivos por respuesta.

## 4. Descargar un archivo
```bash
curl -o log.txt "http://esp32cam.local/download?file=log.txt"
curl -o foto.jpg "http://esp32cam.local/download?file=image_26-05-10T10-00-00_0.jpg"
```

## 5. Eliminar un archivo
```bash
curl "http://esp32cam.local/delete?file=image_26-05-10T10-00-00_0.jpg"
```
⚠ Solo eliminar después de descargar.

## 6. Resetear el log
```bash
curl -X POST "http://esp32cam.local/reset_log"
```
Borra `/log.txt` de la SD. El contador `seq` **no se resetea** — continúa desde donde estaba para garantizar unicidad en el backend.

---

## Formato del log (`log.txt`)

Cada línea es un CSV de 5 columnas:

```
YY-MM-DDTHH-MM-SS,seq,event_o_dash,sensor_o_dash,valor
```

- **Evento:** `26-05-10T10-00-00,42,BOOT,-,0`
- **Sensor:**  `26-05-10T10-01-00,43,-,T1_K,22.500`

`seq` es un contador global que nunca se resetea (persiste en NVS). Permite ordenar filas y detectar sesiones aunque el RTC esté corrupto. Las sesiones se delimitan por eventos `BOOT`.

### Eventos posibles
| Código | Significado |
|--------|-------------|
| BOOT | Arranque del sistema |
| BIRD | Detección de ave |
| PERIODIC | Log periódico |

### Sensores
| Clave | Descripción |
|-------|-------------|
| T1_K | Temp. ambiente |
| T2_K | Temp. caja interna |
| T3_K | Placa fría 1 |
| T4_K | Placa fría 2 |
| T5_K | Temp. media fría |
| T6_K | Temp. objetivo |
| H1_K | Humedad externa |
| H2_K | Humedad interna |
| E1_K | Error |
| E2_K | Error acumulado |
| P1_K | Punto de rocío |
| P2_K | PWM aplicado |
| I1_K | Corriente Panel |
| I2_K | Corriente Turbina |
| I3_K | Corriente Batería |
| I4_K | Corriente filtrada |
| W1_K | Peso del agua |
