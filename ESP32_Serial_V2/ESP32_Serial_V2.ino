#include "SerialTransfer.h"
#include "FS.h"
#include "SD_MMC.h"// SD Card ESP32
#include "SPI.h"
#include "cammera_methods.h"
#include "dual_server.h"


static const size_t N_DATA = 16;

//Códigos de la sensores y variables a guardar:
enum KeyCode : uint8_t {
  T1_K = 0x20,  // Temp ambiente
  T2_K = 0x21,  // Temp caja interna
  T3_K = 0x22,  // Placa fría 1
  T4_K = 0x23,  // Placa fría 2
  T5_K = 0x24,  // Temp media fría
  T6_K = 0x25,  // Temp objetivo
  H1_K = 0x26,  // Humedad externa
  H2_K = 0x27,  // Humedad interna
  E1_K = 0x28,  // Error
  E2_K = 0x29,  // Error acumulado
  P1_K = 0x2A,  // Punto de rocío
  P2_K = 0x2B,   // PWM aplicado
  I1_K = 0x2C,   //Corriente uno
  I2_K = 0x2D,   //Corriente dos
  I3_K = 0x2E,   //Corriente tres
  W1_K = 0x2F    //Peso del agua
};

//Códigos de la trama:
enum Command : uint8_t {
  CMD_TAKE_PHOTO    = 0x01,
  CMD_SAVE_EVENT    = 0x02,
  CMD_SAVE_DATA     = 0x03,
  CMD_LOCKARDUINO   = 0x05,
  CMD_UNLOCKARDUINO = 0x06
};


// Códigos de los eventos
enum EventCode : uint8_t {
  BOOT        = 0x80,
  BIRD        = 0x81,
  PERIODIC    = 0x82
};

//Método para decodificar el sensor enviado
const char* keyLabel(uint8_t key) {
  switch (key) {
    case T1_K: return "Temperatura ambiente";
    case T2_K: return "Temperatura caja interna";
    case T3_K: return "Placa fria 1";
    case T4_K: return "Placa fria 2";
    case T5_K: return "Temperatura media fria";
    case T6_K: return "Temperatura objetivo";
    case H1_K: return "Humedad externa";
    case H2_K: return "Humedad interna";
    case E1_K: return "Error";
    case E2_K: return "Error acumulado";
    case P1_K: return "Punto de rocio";
    case P2_K: return "PWM aplicado";
    case I1_K: return "Corriente de Panel";   //Corriente uno
    case I2_K: return "Corriente de Turbina";   //Corriente dos
    case I3_K: return "Corriente de Bateria";   //Corriente tres
    case W1_K: return "Peso del agua";   //Corriente tres
    default:   return "Key desconocida";
  }
}


SerialTransfer linkReciever;

void setup()
{

  Serial.begin(115200);
  linkReciever.begin(Serial);
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  // Initialize the camera
  Serial.print("Initializing the camera module...");
  configESPCamera();
  Serial.println("Camera OK!");
  

  //Inicializar SD
  initMicroSDCard_Server();
  //initMicroSDCard();
  //saveToSD("test");
  bool intialize = initializeConfig();

  startHttpRoutes();
  server.begin();
  Serial.println("HTTP server started");
}


void loop()
{


  if(linkReciever.available()) //Solo atiende serial si la sd está libre
  {
    //índice: Donde va la lecutra
    uint16_t idxx = 0;
    uint16_t ts[6]; //Timestamp
    uint8_t cmd ; //Qué comando llegó
    String line_to_sd = "";
    
    //Primero lee el timestamp
    idxx = linkReciever.rxObj(ts, idxx);
    char ts_string[20];
    snprintf(ts_string, sizeof(ts_string),
         "%02u-%02u-%02uT%02u-%02u-%02u",
         ts[0], ts[1], ts[2], ts[3], ts[4], ts[5]);
    
    
    //Luego lee el CMD
    idxx = linkReciever.rxObj(cmd, idxx);
    
    switch (cmd){
       //tomar la foto perro
      case CMD_TAKE_PHOTO:{

        String path_image = String("/image_") + String(ts_string);
        takeNewPhoto(path_image);
        sendACK(ts, cmd, 1);
        break;
      }
      //Guaradar el evento en la SD
      
      case CMD_SAVE_EVENT: {
        uint8_t evento;
        const char* eveto_str;
        
        idxx = linkReciever.rxObj(evento, idxx);
        //decode el evento a string
        switch (evento){
          case BOOT:      eveto_str = "BOOT"; break;
          case BIRD:      eveto_str = "BIRD"; break;
          case PERIODIC:  eveto_str = "PERIODIC"; break;
          default:        eveto_str = "INVALID_EV"; break;
        }
            

        line_to_sd += ts_string;
        line_to_sd += ",";
        line_to_sd += eveto_str;
        line_to_sd += ",";
        line_to_sd += "-";
        line_to_sd += "0";
        //guardar "evento" en la sd
        saveToSD (line_to_sd);
        sendACK(ts, cmd, 1);
        break;
      }

      //Guaradar un dato de sensor en la SD
      case CMD_SAVE_DATA:{
        
        uint8_t sensor;
        float sensor_value;
        char sensor_value_str[24];
        idxx = linkReciever.rxObj(sensor, idxx);
        idxx = linkReciever.rxObj(sensor_value, idxx);
        const char* sensor_decoded = keyLabel(sensor);
        snprintf(sensor_value_str, sizeof(sensor_value_str),"%.3f", sensor_value);

        line_to_sd = "";
        line_to_sd += ts_string;
        line_to_sd += ",";
        line_to_sd += "-";
        line_to_sd += ",";
        line_to_sd += sensor_decoded;
        line_to_sd += ",";
        line_to_sd += sensor_value_str;

        saveToSD (line_to_sd);
        sendACK(ts, cmd, 1);
        break;
      }


      // Código si no coincide con ningún "case
      default:
        line_to_sd = "Invalid Format";
        saveToSD (line_to_sd);
        break;
    }

  }
  delay(50); //Para que le de el tiempo
  server.handleClient();
}



void sendACK(uint16_t ts[6], uint8_t cmd,  uint8_t status){
  delay(5);//Porque aja
  uint16_t len;
  len = 0;
  len = linkReciever.txObj(ts,  len, 6 * sizeof(uint16_t));
  len = linkReciever.txObj(cmd, len);
  len = linkReciever.txObj(status, len);
  
  linkReciever.sendData(len); //Enviar el evento
}

void saveToSD(const String &data) {
  Serial.print("Saving in sd card");
  File file = SD_MMC.open("/log.txt", FILE_APPEND);
  if (!file) {
    // Solo crear si NO existe; si existe, evitar truncar
    if (!SD_MMC.exists("/log.txt")) {
      file = SD_MMC.open("/log.txt", FILE_WRITE);  // crea nuevo
    } else {
      Serial.println(" (append failed; avoiding truncate)");
      return; // o reintenta más tarde
    }
  }
  if (!file) { Serial.println("No pude crear/abrir log.txt"); return; }
  file.println(data);
  file.close();
}

void send_lock_arduino(){
  uint16_t ts_dummy[6] = {0,0,0,0,0,0}; //timestamp feka
  uint16_t len;
  uint8_t cmd = CMD_LOCKARDUINO;

  len = 0;
  len = linkReciever.txObj(ts_dummy,  len, 6 * sizeof(uint16_t));
  len = linkReciever.txObj(cmd, len);  
  linkReciever.sendData(len); //Enviar el evento
}



void send_unlock_arduino(){
  uint16_t ts_dummy[6] = {0,0,0,0,0,0}; //timestamp feka
  uint16_t len;
  uint8_t cmd = CMD_UNLOCKARDUINO;

  len = 0;
  len = linkReciever.txObj(ts_dummy,  len, 6 * sizeof(uint16_t));
  len = linkReciever.txObj(cmd, len);  
  linkReciever.sendData(len); //Enviar el evento
}
