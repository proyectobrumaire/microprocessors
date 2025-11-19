# Interacción con el ESP32 mediante `curl`

Este documento reúne los comandos `curl` más comunes para consultar el estado del ESP32, cambiar la red cliente, gestionar archivos en la SD y descargar logs o imágenes.

## 1. Verificar si el ESP32 está accesible
curl http://192.168.4.1/

## 2. Cambiar la red cliente del ESP32
curl -X POST "http://192.168.4.1/wifi" -H "Content-Type: application/json" -u admin:admin -d '{"ssid": "Familia Orozco2","password": "jazz9954"}'

## 3. Listar archivos de la SD
⚠ Nota: Este método puede ser pesado si la SD tiene muchos archivos.
curl http://192.168.4.1/files

## 4. Descargar archivos (logs, imágenes, etc.)
curl -o log.txt "http://192.168.4.1/download?file=log.txt"
curl -o image_2025-11-17T23-26-20_3.jpg "http://192.168.4.1/download?file=image_2025-11-17T23-26-20_3.jpg"

## 5. Eliminar un archivo de la SD
curl -X DELETE "http://192.168.4.1/files?file=image_2025-11-17T23-26-20_3.jpg"

**Nota:** Solamente eliminar archivos después de descargarlos.

