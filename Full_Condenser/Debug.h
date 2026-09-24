#pragma once
#include <Arduino.h>

// Mensajes de depuración por Serial (USB). En operación dejar en 0:
// Serial.print bloquea aunque no haya nada conectado al USB.
#define DEBUG 1

#if DEBUG
  #define DBG(...)   Serial.print(__VA_ARGS__)
  #define DBGLN(...) Serial.println(__VA_ARGS__)
#else
  #define DBG(...)   do {} while (0)
  #define DBGLN(...) do {} while (0)
#endif
