// Diagnóstico del RTC PCF8583: escanea el bus I2C, lee los registros de hora y vigila el pin
// de interrupción del timer (pin 19). Deja la Peltier, los ventiladores y los servos apagados.
// Monitor serie a 115200. Después de usarlo, volver a cargar Full_Condenser.
#include <Wire.h>

const uint8_t PINS_BAJO[] = {10, 11, 13, 5, 4, 39, 33, 30, 31, 2, 28, 29, 3};
const uint8_t PIN_TIMER = 19;
const uint8_t RTC_ADDR = 0xA0 >> 1;  // 0x50 (A0 a GND); con A0 a VCC sería 0x51

uint8_t bcd2dec(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }

void escanear() {
  Serial.println(F("Escaneando I2C..."));
  uint8_t n = 0;
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    uint8_t err = Wire.endTransmission();
    if (err == 0) { Serial.print(F("  dispositivo en 0x")); Serial.println(a, HEX); n++; }
    if (Wire.getWireTimeoutFlag()) { Serial.println(F("  TIMEOUT: bus I2C bloqueado (SDA/SCL en bajo?)")); Wire.clearWireTimeoutFlag(); return; }
  }
  if (!n) Serial.println(F("  ningún dispositivo responde (cableado, alimentación o pull-ups)"));
}

void leerHora() {
  Wire.beginTransmission(RTC_ADDR);
  Wire.write(0x00);
  uint8_t err = Wire.endTransmission();
  if (err) { Serial.print(F("RTC no responde en 0x")); Serial.print(RTC_ADDR, HEX); Serial.print(F(", error ")); Serial.println(err); return; }
  Wire.requestFrom(RTC_ADDR, (uint8_t)7);
  if (Wire.available() < 7) { Serial.println(F("RTC: lectura incompleta")); return; }
  uint8_t ctrl = Wire.read(); Wire.read();  // control, centésimas
  uint8_t s = bcd2dec(Wire.read()), m = bcd2dec(Wire.read()), h = bcd2dec(Wire.read() & 0x3F);
  uint8_t yd = Wire.read(), md = Wire.read();
  char buf[64];
  snprintf(buf, sizeof(buf), "RTC %02u:%02u:%02u  dia %u mes %u  ctrl=0x%02X  INT(19)=%s",
           h, m, s, bcd2dec(yd & 0x3F), bcd2dec(md & 0x1F), ctrl, digitalRead(PIN_TIMER) ? "alto" : "BAJO");
  Serial.println(buf);
  if (ctrl & 0x80) Serial.println(F("  ojo: el reloj está DETENIDO (bit stop en control)"));
}

void setup() {
  for (uint8_t p : PINS_BAJO) { pinMode(p, OUTPUT); digitalWrite(p, LOW); }
  pinMode(PIN_TIMER, INPUT_PULLUP);
  Serial.begin(115200);
  Wire.begin();
  Wire.setWireTimeout(25000, true);  // 25 ms: si el bus se cuelga, no bloquea el sketch
  Serial.println(F("RTC_DIAG: Peltier, ventiladores y servos apagados"));
  escanear();
}

void loop() {
  leerHora();  // los segundos deben avanzar
  delay(1000);
}
