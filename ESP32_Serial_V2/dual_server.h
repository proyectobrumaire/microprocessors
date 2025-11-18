#pragma once


#include <WiFi.h>
#include <ESP32WebServer.h>
#include <time.h>
#include <ESPmDNS.h>
#include "FS.h"
#include "SD_MMC.h"
#include "Arduino.h"
#include <ArduinoJson.h>

extern ESP32WebServer server;

bool is_authenticated();

void initMicroSDCard_Server();

extern volatile bool sd_busy;
extern volatile uint32_t sd_busy_since;
extern const uint32_t SD_BUSY_MAX_TIME; 


void startHttpRoutes();

// ---- Utilidades JSON / CORS ----
static void sendJSON(int code, const String& payload);

static String jsonError(const String& msg);

static String jsonMessage(const String& msg);
bool detect_sd();

static bool read_config(String& ssid, String& pass);

static bool write_config(String ssid, String pass);
void reconnectSTA();

// ---- Handlers ----
void handleHome();


void handleNotFound();

void handleWiFiSTA();


// GET /files  -> lista archivos de la raíz en JSON
void handleListFiles();


void handleDownload();

// DELETE /files  body: {"file":"log.txt"}
void handleDelete();

bool initializeConfig();
