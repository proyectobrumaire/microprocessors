#include <Arduino.h>
#include "FS.h"
#include "SD_MMC.h"// SD Card ESP32
#include "SPI.h"
#include "soc/soc.h"           // Disable brownour problems
#include "soc/rtc_cntl_reg.h"  // Disable brownour problems
#include "driver/rtc_io.h"
#include "esp_camera.h"
#include "SerialTransfer.h"
#include <WiFi.h>
#include <Preferences.h> // Librería para usar NVS de forma fácil
#include <esp_http_server.h>
#include <ArduinoJson.h>
#include <nvs_flash.h>
#include <ESPmDNS.h>

//FreeRTOS
//#define STACK_SIZE 1024*8
#define CAMERA_POLLING_INTERVAL 1000
#define SERIAL_POLLIGN_INTERVAL 50
#define numPhotosPerMessage 3
#define MAX_FILES_TO_LIST 20

#define STACK_SERIAL 4096
#define STACK_CAMERA 8192
#define STACK_SD     8192
#define STACK_HTTP   8192

// Pin definition for CAMERA_MODEL_AI_THINKER
#define PWDN_GPIO_NUM 32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 0
#define SIOD_GPIO_NUM 26
#define SIOC_GPIO_NUM 27
#define Y9_GPIO_NUM 35
#define Y8_GPIO_NUM 34
#define Y7_GPIO_NUM 39
#define Y6_GPIO_NUM 36
#define Y5_GPIO_NUM 21
#define Y4_GPIO_NUM 19
#define Y3_GPIO_NUM 18
#define Y2_GPIO_NUM 5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM 23
#define PCLK_GPIO_NUM 22

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
  I4_K = 0x2F,   //Corriente filtrada
  W1_K = 0x30    //Peso del agua
};

//Códigos de la trama
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
    case T1_K: return "T1_K";
    case T2_K: return "T2_K";
    case T3_K: return "T3_K";
    case T4_K: return "T4_K";
    case T5_K: return "T5_K";
    case T6_K: return "T6_K";
    case H1_K: return "H1_K";
    case H2_K: return "H2_K";
    case E1_K: return "E1_K";
    case E2_K: return "E2_K";
    case P1_K: return "P1_K";
    case P2_K: return "P2_K";
    case I1_K: return "I1_K";   //Corriente uno
    case I2_K: return "I2_K";   //Corriente dos
    case I3_K: return "I3_K";   //Corriente tres
    case I4_K: return "I4_K";   //Corriente cuatro
    case W1_K: return "W1_K";   //Corriente tres
    default:   return "Z0_K";
  }
}

//Tipos de eventos para las Tasks (FreeRTOS)
typedef enum {
  EVT_GET_SERIAL_CMD,
  EVT_TAKE_PHOTO,
  EVT_SAVE_PHOTO,
  EVT_SAVE_LOG,
  EVT_ERROR
} EventType;

//Estructura de los eventos de las tasks (FreeRTOS)
typedef struct {
  EventType type; //Tipo de evento
  char filename[64]; //Nombre de la foto
  void *data; //Puntero de la foto
  char msg[128]; //Mensaje para guardar en la SD
} Event;

SerialTransfer linkReciever;
Preferences prefs;

QueueHandle_t cameraQueue = 0;
QueueHandle_t sdQueue = 0;
SemaphoreHandle_t SDMutex = 0;

httpd_handle_t server = NULL;
bool is_sta_mode = false;

const char *tag = "Main";

esp_err_t init_led(void);
esp_err_t create_tasks(void);
void vTaskR( void *pvParameters );
void vTaskSerial ( void *pvParameters );
void vTaskCamera ( void *pvParameters );
void vTaskSDCard ( void *pvParameters );
void configESPCamera(); //Configurar ESP32 Cam

void setup() {
  // Inicialización manual de la NVS
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);
  Serial.begin(115200);
  linkReciever.begin(Serial);
  initMicroSDCard();
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); //??
  configESPCamera();
  create_tasks();

  String ssid, pass;
  loadCredentials(ssid, pass);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());
  unsigned long startAttemptTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
    delay(500);
  }
  if (WiFi.status() == WL_CONNECTED) {
    ESP_LOGW(tag, "Conected in STA mode");
    is_sta_mode = true;
    
    if (!MDNS.begin("esp32cam")) { 
        ESP_LOGE(tag, "Error configurando mDNS");
    } else {
        MDNS.addService("http", "tcp", 80);
    }

  } else {
    ESP_LOGE(tag, "STA Mode failed. Starting AP mode for configuration");
    WiFi.mode(WIFI_AP);
    WiFi.softAP("ESP32_CONFIG_CAM", "12345678");
    is_sta_mode = false;
  }
  startServer();
}

void loop() {
  delay(1);
}

//Métodos de la cámra
void configESPCamera() {
  // Object to store the camera configuration parameters
  camera_config_t config;

  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;  // Choices are YUV422, GRAYSCALE, RGB565, JPEG
  config.frame_size = FRAMESIZE_UXGA;    // FRAMESIZE_ + QVGA|CIF|VGA|SVGA|XGA|SXGA|UXGA
  config.jpeg_quality = 10;              // 10-63 lower number means higher quality
  config.fb_count = 2;

  // Initialize the Camera
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }

  // Camera quality adjustments
  sensor_t *s = esp_camera_sensor_get();

  s->set_framesize(s, FRAMESIZE_UXGA);      // FRAMESIZE_[QQVGA|HQVGA|QVGA|CIF|VGA|SVGA|XGA|SXGA|UXGA|QXGA(ov3660)]);
  s->set_quality(s, 10);                    // 10 to 63
  s->set_brightness(s, 0);                  // -2 to 2
  s->set_contrast(s, 0);                    // -2 to 2
  s->set_saturation(s, 0);                  // -2 to 2
  s->set_special_effect(s, 0);              // 0 to 6 (0 - No Effect, 1 - Negative, 2 - Grayscale, 3 - Red Tint, 4 - Green Tint, 5 - Blue Tint, 6 - Sepia)
  s->set_whitebal(s, 1);                    // aka 'awb' in the UI; 0 = disable , 1 = enable
  s->set_awb_gain(s, 1);                    // 0 = disable , 1 = enable
  s->set_wb_mode(s, 0);                     // 0 to 4 - if awb_gain enabled (0 - Auto, 1 - Sunny, 2 - Cloudy, 3 - Office, 4 - Home)
  s->set_exposure_ctrl(s, 1);               // 0 = disable , 1 = enable
  s->set_aec2(s, 0);                        // 0 = disable , 1 = enable
  s->set_ae_level(s, 0);                    // -2 to 2
  s->set_aec_value(s, 300);                 // 0 to 1200
  s->set_gain_ctrl(s, 1);                   // 0 = disable , 1 = enable
  s->set_agc_gain(s, 0);                    // 0 to 30
  s->set_gainceiling(s, (gainceiling_t)0);  // 0 to 6
  s->set_bpc(s, 0);                         // 0 = disable , 1 = enable
  s->set_wpc(s, 1);                         // 0 = disable , 1 = enable
  s->set_raw_gma(s, 1);                     // 0 = disable , 1 = enable
  s->set_lenc(s, 1);                        // 0 = disable , 1 = enable
  s->set_hmirror(s, 0);                     // 0 = disable , 1 = enable
  s->set_vflip(s, 1);                       // 0 = disable , 1 = enable
  s->set_dcw(s, 1);                         // 0 = disable , 1 = enable
  s->set_colorbar(s, 0);                    // 0 = disable , 1 = enable
}
//Méotdo SD
void initMicroSDCard() {
  if (!SD_MMC.begin("/sdcard", true)) {
     ESP_LOGE(tag,"MicroSD Card Mount Failed");
    //SD_present =  false;
    return;
  }
  uint8_t cardType = SD_MMC.cardType();
  if (cardType == CARD_NONE) {
    ESP_LOGE(tag,"No MicroSD Card found");
    //SD_present =  false;
    return;
  }
  //SD_present =  true;
}
//Métodos seriales
void sendACK(uint16_t ts[6], uint8_t cmd,  uint8_t status){
  delay(5);//Porque aja
  uint16_t len;
  len = 0;
  len = linkReciever.txObj(ts,  len, 6 * sizeof(uint16_t));
  len = linkReciever.txObj(cmd, len);
  len = linkReciever.txObj(status, len);
  linkReciever.sendData(len); //Enviar el evento
}
//Métodos del serivor
void saveCredentials(String s, String p) {
    prefs.begin("wifi_conf", false);
    prefs.putString("ssid", s);
    prefs.putString("pass", p);
    prefs.end();
}
void loadCredentials(String &s, String &p) {
    prefs.begin("wifi_conf", true);
    s = prefs.getString("ssid", "default_ssid");
    p = prefs.getString("pass", "default_pass");
    prefs.end();
}
esp_err_t wifi_setup_handler(httpd_req_t *req) {
  if (req->content_len >= 128) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Too long JSON");
    return ESP_FAIL;
  }
  char buf[128];
  int ret = httpd_req_recv(req, buf, req->content_len); //Recibe los datos enviados
  if (ret <= 0) return ESP_FAIL;
  buf[ret] = '\0';
  JsonDocument doc; // Parsear JSON
  DeserializationError error = deserializeJson(doc, buf);
  if (error) {
      httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "JSON Invalido");
      return ESP_FAIL;
  }
  const char* ssid = doc["ssid"];
  const char* pass = doc["password"];
  if (ssid && pass) {
      saveCredentials(ssid, pass);
      httpd_resp_sendstr(req, "{\"status\":\"Credenciales guardadas. Reiniciando...\"}");
      delay(1000);
      ESP.restart();
  } else {
      httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Faltan campos ssid/password");
  }
  return ESP_OK;
}
esp_err_t list_files_sd_handler(httpd_req_t *req) {
    if (is_sta_mode == false){
      return httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Only STA mode for this mode");
      return ESP_FAIL;
    }
    if (req->content_len >= 128) {
      httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Too long JSON");
      return ESP_FAIL;
    }

    if (xSemaphoreTake(SDMutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "SD Busy (503)");
    }

    File root = SD_MMC.open("/");
    if (!root || !root.isDirectory()) {
        xSemaphoreGive(SDMutex);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to open Dir");
    } 
    httpd_resp_sendstr_chunk(req, "{\"files\":[");
    File file = root.openNextFile();
    int fileCount = 0;
    bool first = true;
    while (file && fileCount < MAX_FILES_TO_LIST ) {
        if (!file.isDirectory()) {
            if (!first) httpd_resp_sendstr_chunk(req, ",");
            char buf[128];
            snprintf(buf, sizeof(buf), "{\"name\":\"%s\",\"size\":%d}", file.name(), file.size());
            httpd_resp_sendstr_chunk(req, buf);
            first = false;
            fileCount++;
        }
        file = root.openNextFile();
    }
    bool truncated = (file) ? true : false;

    char tail[64];
    snprintf(tail, sizeof(tail), "], \"count\":%d, \"truncated\":%s}", 
             fileCount, truncated ? "true" : "false");
    
    httpd_resp_sendstr_chunk(req, tail);
    httpd_resp_sendstr_chunk(req, NULL);

    xSemaphoreGive(SDMutex);
    return ESP_OK;
}
esp_err_t download_file_handler(httpd_req_t *req) {
    if (!is_sta_mode) {
        return httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Only STA mode allowed");
    }
    size_t buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len <= 1) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing file parameter");
    }

    char* buf = (char*)malloc(buf_len);
    if (!buf) return ESP_ERR_NO_MEM; 
    esp_err_t query_status = httpd_req_get_url_query_str(req, buf, buf_len);
    char param[64];
    
    if (query_status != ESP_OK || httpd_query_key_value(buf, "file", param, sizeof(param)) != ESP_OK) {
        free(buf);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid query");
    }
    free(buf);
    if (xSemaphoreTake(SDMutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
      return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "SD Busy");
    }

    String path = "/" + String(param);
    File file = SD_MMC.open(path.c_str(), FILE_READ);
    if (!file || file.isDirectory()) {
        if (file) file.close();
        xSemaphoreGive(SDMutex);
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
    }
    httpd_resp_set_type(req, "image/jpeg");
    //Añadir un header para que el navegador sepa que es una descarga
    httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");

    char *chunk = (char *)malloc(1024);
    if (!chunk) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No RAM for chunk");
    }  
    size_t read_bytes;
    esp_err_t res = ESP_OK;
    while ((read_bytes = file.read((uint8_t*)chunk, sizeof(chunk))) > 0) {
        res = httpd_resp_send_chunk(req, chunk, read_bytes);
        if (res != ESP_OK) {
            ESP_LOGE("HTTP", "Abortado: Cliente desconectado");
            break; 
        }
    }
    free(chunk);
    file.close();
    xSemaphoreGive(SDMutex); 
    httpd_resp_send_chunk(req, NULL, 0); 
    return res;
}
esp_err_t delete_file_handler(httpd_req_t *req){
    if (!is_sta_mode) {
        return httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Only STA mode allowed");
    }
    size_t buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len <= 1) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing file parameter");
    }

    char* buf = (char*)malloc(buf_len);
    if (!buf) return ESP_ERR_NO_MEM; 
    esp_err_t query_status = httpd_req_get_url_query_str(req, buf, buf_len);
    char param[64];
    
    if (query_status != ESP_OK || httpd_query_key_value(buf, "file", param, sizeof(param)) != ESP_OK) {
        free(buf);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid query");
    }
    free(buf);
    if (xSemaphoreTake(SDMutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
      return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "SD Busy");
    }

    String path = "/" + String(param);
    if (!SD_MMC.exists(path.c_str())) {
        xSemaphoreGive(SDMutex);
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
    }
    if (SD_MMC.remove(path.c_str())) {
        xSemaphoreGive(SDMutex);
        ESP_LOGI("SD", "Archivo eliminado: %s", path.c_str());
        httpd_resp_sendstr(req, "{\"status\":\"Eliminado correctamente\"}");
        return ESP_OK;
    } else {
        xSemaphoreGive(SDMutex);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to remove file");
    }
}
void startServer() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    if (httpd_start(&server, &config) == ESP_OK) {
        // Ruta de configuración (Siempre disponible)
        httpd_uri_t wifi_uri = { .uri="/wifi", .method=HTTP_POST, .handler=wifi_setup_handler };
        httpd_register_uri_handler(server, &wifi_uri);

        httpd_uri_t list_files_uri = { .uri="/list", .method=HTTP_GET, .handler=list_files_sd_handler };
        httpd_register_uri_handler(server, &list_files_uri);

        httpd_uri_t download_file_uri = { .uri="/download", .method=HTTP_GET, .handler=download_file_handler };
        httpd_register_uri_handler(server, &download_file_uri);

        httpd_uri_t delete_file_uri = { .uri="/delete", .method=HTTP_GET, .handler=delete_file_handler };
        httpd_register_uri_handler(server, &delete_file_uri);
    }
}
esp_err_t create_tasks(void){
  cameraQueue = xQueueCreate(15, sizeof(Event));
  sdQueue = xQueueCreate(20, sizeof(Event));
  SDMutex = xSemaphoreCreateMutex();

  static uint8_t ucParameterToPass;
  TaskHandle_t  xHandle = NULL;

  xTaskCreate(
    vTaskSerial,
    "vTaskSerial",
    STACK_SERIAL,
    &ucParameterToPass,
    2,
    &xHandle
  );

  xTaskCreate(
    vTaskCamera,
    "vTaskCamera",
    STACK_CAMERA,
    &ucParameterToPass,
    1,
    &xHandle
  );


  xTaskCreate(
    vTaskSDCard,
    "vTaskSDCard",
    STACK_SD,
    &ucParameterToPass,
    3,
    &xHandle
  );
  return ESP_OK;
}

void vTaskSerial( void *pvParameters ){
  while(1)
  {
    if(linkReciever.available()) 
    {
      //índice: Donde va la lecutra
      uint16_t idxx = 0;
      uint16_t ts[6]; //Timestamp
      uint8_t cmd ; //Qué comando llegó
      
      //Primero lee el timestamp
      idxx = linkReciever.rxObj(ts, idxx, sizeof(ts));
      char ts_string[20];
      snprintf(ts_string, sizeof(ts_string),
          "%02u-%02u-%02uT%02u-%02u-%02u",
          ts[0], ts[1], ts[2], ts[3], ts[4], ts[5]);
      //Luego lee el CMD
      idxx = linkReciever.rxObj(cmd, idxx);
      
      switch (cmd){
        //tomar la foto perro
        case CMD_TAKE_PHOTO:
        {
          for (int i = 0; i < numPhotosPerMessage; i++){
            Event event_to_camera;
            char filename[64];
            snprintf(filename, sizeof(filename),"/image_%s_%d.jpg", ts_string, i);
            event_to_camera.type = EVT_TAKE_PHOTO;
            strncpy(event_to_camera.filename, filename, sizeof(event_to_camera.filename));
            if ( !xQueueSend(cameraQueue, &event_to_camera, pdMS_TO_TICKS(portMAX_DELAY)) ){
              ESP_LOGE(tag, "Error sending event to camera queue ");
            }
            vTaskDelay(pdMS_TO_TICKS(10));
          }
          sendACK(ts, cmd, 1);
          break;
        }
        //Guaradar el evento en la SD
        case CMD_SAVE_EVENT: 
        {
          Event event_to_sd;
          char msg[128]; //Mensaje para guardar en la SD
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
          snprintf(
            msg, 
            sizeof(msg),
            "%s,%s,-,0", 
            ts_string, eveto_str
          );
          event_to_sd.type = EVT_SAVE_LOG;
          strncpy(event_to_sd.msg, msg, sizeof(event_to_sd.msg));
          if ( !xQueueSend(sdQueue, &event_to_sd, pdMS_TO_TICKS(500)) ){
            ESP_LOGE(tag, "Error sending to sd queue ");
          }
          vTaskDelay(pdMS_TO_TICKS(10));
          sendACK(ts, cmd, 1);
          break;
        }

        //Guaradar un dato de sensor en la SD
        case CMD_SAVE_DATA:
        {
          Event event_to_sd;
          char msg[128]; //Mensaje para guardar en la SD
          uint8_t sensor;
          float sensor_value;
          idxx = linkReciever.rxObj(sensor, idxx);
          idxx = linkReciever.rxObj(sensor_value, idxx);
          const char* sensor_decoded = keyLabel(sensor);
          snprintf(
            msg, 
            sizeof(msg),
            "%s,-,%s,%.3f", 
            ts_string, sensor_decoded, sensor_value
          );
          event_to_sd.type = EVT_SAVE_LOG;
          strncpy(event_to_sd.msg, msg, sizeof(event_to_sd.msg));
          if (xQueueSend(sdQueue, &event_to_sd, pdMS_TO_TICKS(500)) != pdPASS ){
            ESP_LOGE(tag, "Error sending to sd queue ");
          }
          vTaskDelay(pdMS_TO_TICKS(10));
          sendACK(ts, cmd, 1);
          break;
        }
        // Código si no coincide con ningún case
        default:
          ESP_LOGE(tag, "Invalid Format recieved for command");
          break;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void vTaskCamera ( void *pvParameters ){
  while(1){
    Event receivedEvent;
    if(xQueueReceive(cameraQueue, &receivedEvent, pdMS_TO_TICKS(portMAX_DELAY))) {
      if (receivedEvent.type == EVT_TAKE_PHOTO) {
        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb) {
          ESP_LOGE(tag, "Camera capture failed");
          continue;
        }
        //Enviar a la cola de la SD con el filename y el puntero de fb
        Event event_to_sd;
        event_to_sd.type = EVT_SAVE_PHOTO;
        strncpy(event_to_sd.filename, receivedEvent.filename, sizeof(event_to_sd.filename));
        event_to_sd.data = (void*) fb;
        if (xQueueSend(sdQueue, &event_to_sd, pdMS_TO_TICKS(500)) != pdPASS) {
          ESP_LOGE(tag, "SD Queue Full! Freeing FB");
          esp_camera_fb_return(fb); 
        }

      }
    }
    //Poling time
    vTaskDelay(pdMS_TO_TICKS(CAMERA_POLLING_INTERVAL));
  }
}

void vTaskSDCard ( void *pvParameters ){
  //Mejora: Podrías acumular 5 o 10 mensajes en un buffer y escribirlos todos juntos, 
  //o dejar el archivo abierto si sabes que vendrán ráfagas de datos.
  while(1){
    Event receivedEvent;
    if(xQueueReceive(sdQueue, &receivedEvent, pdMS_TO_TICKS(portMAX_DELAY))) {
      switch (receivedEvent.type){
        case EVT_SAVE_PHOTO:{
          camera_fb_t *fb = (camera_fb_t*) receivedEvent.data;
          if (!fb) {
            ESP_LOGE(tag, "Error: fb null");
            break;
          }
          xSemaphoreTake(SDMutex, portMAX_DELAY);
          fs::FS &fs = SD_MMC;
          File file = fs.open(receivedEvent.filename, FILE_WRITE);
          if (!file) {
              ESP_LOGE(tag, "Failed to open file in write mode");
          } else {
              file.write(fb->buf, fb->len);  // payload (image), payload length
              file.close();
          }
          xSemaphoreGive(SDMutex);
          esp_camera_fb_return(fb);
          fb = NULL;
          break;
        }
        case EVT_SAVE_LOG:{
          xSemaphoreTake(SDMutex, portMAX_DELAY);
          File file = SD_MMC.open("/log.txt", FILE_APPEND);
          if (!file) {
            // Solo crear si NO existe; si existe, evitar truncar
            if (!SD_MMC.exists("/log.txt")) {
              file = SD_MMC.open("/log.txt", FILE_WRITE); // crea nuevo
            }
          }
          if (!file) { ESP_LOGE(tag, "Cant create/open log.txt"); }
          file.println(receivedEvent.msg);
          file.close();
          xSemaphoreGive(SDMutex);
          break;
        }
      }
    }
  }
}

