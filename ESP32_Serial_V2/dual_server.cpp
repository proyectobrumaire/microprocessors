#include "dual_server.h"


//Archivo de config
static const char* FILENAME_CONFIG = "/config.json";
String sta_ssid, sta_password;
String default_SSID_sta = "univalle";
String default_password_sta = "Univalle";
bool SD_present = false;

  
// AP Mode Credentials
static const char* ap_ssid = "ESP32_AP";
static const char* ap_password = "12345678";

// IP Configurations
IPAddress local_ip(192,168,4,1);
IPAddress gateway(192,168,4,1);
IPAddress subnet(255,255,255,0);
ESP32WebServer server(80);

const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 0;
const int   daylightOffset_sec = 3600;

// Máximo de archivos a devolver en la respuesta
const int MAX_FILES_RESPONSE = 20;     // o 100, según quieras

// Longitud máxima de nombre de archivo que se puede guardar
const int MAX_FILENAME_LEN   = 64;     // si usas rutas cortas, con 64 suele bastar

struct FileInfo {
  char  name[MAX_FILENAME_LEN];     // nombre de archivo fijo
  uint32_t size;                       // tamaño en bytes
};

void startHttpRoutes() {
  server.on("/", handleHome);
  server.on("/wifi", HTTP_POST, handleWiFiSTA);
  server.on("/files", HTTP_GET, handleListFiles);
  server.on("/files", HTTP_DELETE, handleDelete);
  server.on("/download", HTTP_GET, handleDownload);
  server.onNotFound(handleNotFound);
}


bool is_authenticated() {
  if (!server.authenticate("admin", "admin"))
  {
    server.requestAuthentication();
    return false;
  }
  return true;
}

void initMicroSDCard_Server() {
  // Start the MicroSD card

  Serial.println("Mounting MicroSD Card");
  if (!SD_MMC.begin("/sdcard", true)) {
    Serial.println("MicroSD Card Mount Failed");
    SD_present =  false;
    return;
  }
  uint8_t cardType = SD_MMC.cardType();
  if (cardType == CARD_NONE) {
    Serial.println("No MicroSD Card found");
    SD_present =  false;
    return;
  }
  SD_present =  true;
}



// ---- Utilidades JSON / CORS ----
static void sendJSON(int code, const String& payload) {
  server.sendHeader("Access-Control-Allow-Methods", "GET,DELETE,OPTIONS,POST");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
  server.send(code, "application/json; charset=utf-8", payload);
}

static String jsonError(const String& msg) {
  return String("{\"error\":\"") + msg + "\"}";
}

static String jsonMessage(const String& msg) {
  return String("{\"message\":\"") + msg + "\"}";
}

bool detect_sd(){
  if (SD_present) {
    // Prueba algo sencillo (abrir raíz). Si falla, marca no presente.
    File test = SD_MMC.open("/");
    if (test) { test.close(); return true; }
    SD_present = false;
  }
  return false;
}

static bool read_config(String& ssid, String& pass) {
  //Actualiza ssid y pass que se pasen en parámetros leyendo de FILENAME_CONFIG en la sd
  JsonDocument config;
  if (!detect_sd() || !SD_MMC.exists(FILENAME_CONFIG)) return false;
  
  File file = SD_MMC.open(FILENAME_CONFIG, FILE_READ);
  if (!file) {
    Serial.println("No pude abrir config config.json");
    return false;
  }
  DeserializationError error = deserializeJson(config, file);
  file.close();
  if (error) {
    Serial.print("deserializeJson() failed: ");
    Serial.println(error.c_str());
    return false;
  }

  const char* s = config["SSID"];
  const char* p = config["Password"];
  if (!s || !*s || !p || !*p) return false;

  ssid = s; pass = p;
  return true;
}

static bool write_config(String ssid, String pass) {
  //Borra el archivo FILENAME_CONFIG (si existe) y lorecrea poniendo la configuración

  JsonDocument config;
  if (!detect_sd()) return false;

  if (!SD_MMC.exists(FILENAME_CONFIG)){
    SD_MMC.remove(FILENAME_CONFIG);
  }

  File file = SD_MMC.open(FILENAME_CONFIG, FILE_WRITE);
  if (!file) return false;

  config["SSID"] = ssid;
  config["Password"] = pass;

  serializeJsonPretty(config, file);
  file.close();
  return true;

}

void reconnectSTA() {
  const String ssid = sta_ssid;
  const String pass = sta_password;
  //AP+STA
  WiFi.mode(WIFI_AP_STA);

  if (WiFi.status() == WL_CONNECTED) {
    // Limpia asociación previa y arranca de cero
    WiFi.disconnect(true, true);  // stop + erase old creds
    delay(200);
  }


  WiFi.begin(ssid.c_str(), pass.c_str());
  Serial.print("Reconnecting STA to: "); Serial.println(ssid);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("STA reconnected");
    Serial.print("Station IP address: ");
    Serial.println(WiFi.localIP());

    MDNS.begin("fileserver");
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  } else {
    Serial.println("STA reconnection failed (AP sigue activo).");
  }
}




// ---- Handlers ----
void handleHome() {

  JsonDocument response_body;
  String payload;
  if (WiFi.status() == WL_CONNECTED) {
    response_body["WiFiLocal"]["WiFi_status"] = true;
    response_body["WiFiLocal"]["ip"] =  WiFi.localIP().toString();
    response_body["WiFiLocal"]["mask"] =  WiFi.subnetMask().toString();
    response_body["WiFiLocal"]["MAC"] =  WiFi.macAddress();
   
  } else {
    response_body["WiFiLocal"]["WiFi_status"] = false;
  }

  response_body["WiFiAp"]["WiFi_ip"] = WiFi.softAPIP().toString();
  response_body["WiFiAp"]["station"] =  WiFi.softAPgetStationNum();
  response_body["WiFiAp"]["MAC"] =  WiFi.softAPmacAddress();

  serializeJsonPretty(response_body, payload);  // o serializeJson(doc, out);

  sendJSON(200, payload);
}


void handleNotFound() {
  String content = "<h1>404: Not Found</h1><p>The requested page was not found.</p>";
  sendJSON(404, content);
}


void handleWiFiSTA(){
  JsonDocument body;

  if (!is_authenticated()) {
  return;
  }
  send_lock_arduino();
  if (!detect_sd()) { sendJSON(503, jsonError("No SD Card present")); send_unlock_arduino(); return; }

  if (server.hasArg("plain")) {
    const String body_plain = server.arg("plain");
    DeserializationError error = deserializeJson(body, body_plain);
    if (error) {
      sendJSON(400, jsonError("deserializeJson() failed: "));
      send_unlock_arduino();
      return;
    }

    const char* ssid_req = body["ssid"];
    const char* password_req = body["password"];

    if (!ssid_req || !*ssid_req || !password_req || !*password_req) {
      sendJSON(400, jsonError("ssid and password required in body")); 
      send_unlock_arduino();
      return;
    }

    
    write_config(ssid_req, password_req); //Subimos la config a la sd
    if (read_config(sta_ssid, sta_password)){
      reconnectSTA();
    }
    sendJSON(200, jsonMessage("config File updated"));
    send_unlock_arduino();
    return;

    

    

  }else{
    sendJSON(400, jsonError("Cuerpo requerido"));
    send_unlock_arduino();
    return;
  }
}



// GET /files  -> lista archivos de la raíz en JSON
void handleListFiles() {
  // Si quieres, aquí podrías mandar LOCK al Arduino antes de empezar
  send_lock_arduino();

  if (!detect_sd()) {
    sendJSON(503, jsonError("No SD Card present"));
    send_unlock_arduino();
    return;
  }

  File root = SD_MMC.open("/");
  if (!root || !root.isDirectory()) {
    sendJSON(500, jsonError("No se pudo abrir el directorio raiz"));
    send_unlock_arduino();
    return;
  }

  // Ventana deslizante: solo guardamos los últimos N archivos
  FileInfo lastFiles[MAX_FILES_RESPONSE];
  int stored = 0;   // cuántos archivos tenemos realmente guardados (<= MAX_FILES_RESPONSE)
  int head   = 0;   // índice circular donde se insertar á el siguiente
  int total  = 0;   // total de archivos vistos en la raíz (para has_more)

  File entry = root.openNextFile();
  while (entry) {
    if (!entry.isDirectory()) {
      // Copiar nombre al buffer de longitud fija
      const char* entryName = entry.name();
      // strncpy garantiza que no nos pasamos; agregamos '\0' al final
      strncpy(lastFiles[head].name, entryName, MAX_FILENAME_LEN - 1);
      lastFiles[head].name[MAX_FILENAME_LEN - 1] = '\0';

      // Guardar tamaño
      lastFiles[head].size = (uint32_t) entry.size();

      // Avanzar en el buffer circular
      head = (head + 1) % MAX_FILES_RESPONSE;
      if (stored < MAX_FILES_RESPONSE) {
        stored++;
      }

      total++;
    }

    entry = root.openNextFile();
    yield();  // por si hay muchos archivos, no bloquear del todo
  }

  root.close();

  bool has_more = (total > stored);

  // --- Construir JSON (máx. N archivos, así que el String no se hace enorme) ---
  String out = "{\n  \"files\": [\n";

  if (stored > 0) {
    // Reconstruimos en orden lógico: del más antiguo de los N últimos al más reciente
    int startIdx;
    if (stored < MAX_FILES_RESPONSE) {
      // No llenamos el buffer: están en 0..stored-1 en orden de aparición
      startIdx = 0;
    } else {
      // Buffer lleno: el más antiguo está donde apunta head (porque head es el siguiente en escribirse)
      startIdx = head;
    }

    for (int i = 0; i < stored; i++) {
      int idx = (startIdx + i) % MAX_FILES_RESPONSE;

      if (i > 0) out += ",\n";
      out += "    {\"file\":\"";
      out += lastFiles[idx].name;         // ya es un char[]
      out += "\",\"size\":";
      out += String(lastFiles[idx].size);
      out += "}";
    }
  }

  out += "\n  ],\n";
  out += "  \"has_more\": ";
  out += has_more ? "true" : "false";
  out += ",\n";
  out += "  \"total\": ";
  out += String(total);
  out += "\n}";

  sendJSON(200, out);

  send_unlock_arduino();
  return;
}

void handleDownload() {
  send_lock_arduino();
  if (!detect_sd()) { sendJSON(503, jsonError("No SD Card present")); send_unlock_arduino(); return; }

  // 1) Intentar query string ?file=...
  String filename = server.hasArg("file") ? server.arg("file") : String();


  if (filename.length() == 0) {
    sendJSON(400, jsonError("Parametro 'file' requerido"));
    send_unlock_arduino();
    return;
  }


  
  if (!filename.startsWith("/")) filename = "/" + filename;
  File f = SD_MMC.open(filename, FILE_READ);
  if (!f) {
    sendJSON(404, jsonError("Archivo no encontrado"));
    send_unlock_arduino();
    return;
  }

  // Descarga directa

  server.sendHeader("Cache-Control", "no-cache");
  server.sendHeader("Content-Disposition", "attachment; filename=" + String(filename.c_str()+1)); // sin la barra inicial
  server.streamFile(f, "application/octet-stream");
  f.close();
  send_unlock_arduino();
  return;
}


// DELETE /files
void handleDelete() {
  send_lock_arduino();
  if (!detect_sd()) { sendJSON(503, jsonError("No SD Card present")); send_unlock_arduino(); return; }

  String filename = server.hasArg("file") ? server.arg("file") : String();

  //String filename;
  //if (filename.length() == 0 && server.hasArg("file")) filename = server.arg("file"); // fallback por query

  if (filename.length() == 0) {
    sendJSON(400, jsonError("Parametro 'file' requerido"));
    send_unlock_arduino();
    return;
  }
  
  if (!filename.startsWith("/")) filename = "/" + filename; //Añadir root a la ruta

  // Verificar existencia
  File f = SD_MMC.open(filename, FILE_READ);
  if (!f) {
    sendJSON(404, jsonError("Archivo no encontrado"));
    send_unlock_arduino();
    return;
  }
  f.close();

  bool ok = SD_MMC.remove(filename);
  if (ok) sendJSON(200, jsonMessage("Archivo eliminado con éxito"));
  else    sendJSON(500, jsonError("archivo no eliminado"));
  send_unlock_arduino();
  return;
}



bool initializeConfig(){

  if (!read_config(sta_ssid, sta_password)){
    sta_ssid     = default_SSID_sta;                      
    sta_password = default_password_sta;
    write_config(sta_ssid, sta_password); //Para persistirlos
  }

  // Connect to WiFi network
  //Modo AP
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(local_ip, gateway, subnet);
  WiFi.softAP(ap_ssid, ap_password);
  Serial.println("Setting up Access Point: ");
  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP());

  reconnectSTA();
  return true;
}





