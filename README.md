# Interacción con el ESP32 mediante `curl`

Este documento reúne los comandos `curl` más comunes para consultar el estado del ESP32, cambiar la red cliente, gestionar archivos en la SD y descargar logs o imágenes.

Para establecer una primera conexión con el dispositivo, se debe de conectar primero a la red WiFi emitida por el mismo.
**SSID**: ESP32_AP
**password**: 12345678

## 1. Verificar si el ESP32 está accesible
```bash
curl http://192.168.4.1/
```


## 2. Cambiar la red cliente del ESP32
```bash
curl -X POST "http://192.168.4.1/wifi" -H "Content-Type: application/json" -u admin:admin -d '{"ssid": "Familia Orozco2","password": "jazz9954"}'
```
	
## 3. Listar archivos de la SD
⚠ Nota: Este método puede ser pesado si la SD tiene muchos archivos.
```bash
curl http://192.168.4.1/files
```

## 4. Descargar archivos (logs, imágenes, etc.)
```bash
curl -o log.txt "http://192.168.4.1/download?file=log.txt"
```

```bash
curl -o image_2025-11-17T23-26-20_3.jpg "http://192.168.4.1/download?file=image_2025-11-17T23-26-20_3.jpg"
```
## 5. Eliminar un archivo de la SD
```bash
curl -X DELETE "http://192.168.4.1/files?file=image_2025-11-17T23-26-20_3.jpg"
```

**Nota:** Solamente eliminar archivos después de descargarlos.

