

//Para mirar si el ESP32 está conectado como cliente
curl http://192.168.4.1/

//Para cambiar la red cliente del ESP32 (Ej: Univalle)
curl -X POST "http://192.168.4.1/wifi" \
-H "Content-Type: application/json" \
-u admin:admin \
  -d '{
    "ssid": "Familia Orozco2",
    "password": "jazz9954"
  }'
  
//Para listar los archivos de la sd: OJO -> método pesado
curl http://192.168.4.1/files

//Para descargar archivo log. Query Param file
curl -o log.txt http://192.168.4.1/download?file=log.txt

//Ej para descargar cualquier foto. Query Param file
curl -o uwu.jpg http://192.168.4.1:80/download?file=image_2025-11-17T23-26-20_3.jpg


curl -X  DELETE "http://192.168.4.1:80/files?file=image_2025-11-17T23-26-20_3.jpg"



 
