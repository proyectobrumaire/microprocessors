// Diagnóstico de termocuplas MAX31855 con la Peltier APAGADA.
// Pines iguales a Full_Condenser. Deja el IBT-2 deshabilitado, ventiladores apagados,
// seguro del plato trabado (90) y válvula cerrada (0), igual que en reposo.
//
// Cada segundo imprime por termocupla:
//   err  : 0 OK, 1 abierta, 2 corto a GND, 4 corto a VCC (suman)
//   T    : readCelsius() con setFaultChecks(OPEN) (NaN solo si la termocupla está abierta)
//   Traw : temperatura ignorando las fallas (setFaultChecks(NONE))
//   Int  : temperatura interna del chip
// y un conteo acumulado de lecturas con falla.

#include <SPI.h>
#include <Servo.h>
#include "Adafruit_MAX31855.h"

// MAX31855 (clk, cs, do) — igual que Full_Condenser
Adafruit_MAX31855 tc1(22, 23, 24);
Adafruit_MAX31855 tc2(25, 26, 27);

// IBT-2 (rpwm, lpwm, ren, len)
const uint8_t RPWM = 5, LPWM = 4, REN = 39, LEN = 33;
// L298N ventiladores (in1, in2, ena, in3, in4, enb)
const uint8_t IN1 = 30, IN2 = 31, ENA = 2, IN3 = 28, IN4 = 29, ENB = 3;
// Servos (seguro, volcado, válvula)
const uint8_t M1 = 10, MV = 13;

Servo seguro, valvula;

unsigned long n = 0, fallas1 = 0, fallas2 = 0;

void apagarPotencia() {
  uint8_t pins[] = {RPWM, LPWM, REN, LEN, IN1, IN2, ENA, IN3, IN4, ENB};
  for (uint8_t p : pins) { pinMode(p, OUTPUT); digitalWrite(p, LOW); }
}

void imprimirTC(const char *nombre, Adafruit_MAX31855 &tc, unsigned long &fallas) {
  // T: igual que usará Full_Condenser (solo termocupla abierta cuenta como falla)
  tc.setFaultChecks(MAX31855_FAULT_OPEN);
  double t = tc.readCelsius();
  uint8_t err = tc.readError();
  tc.setFaultChecks(MAX31855_FAULT_NONE);
  double traw = tc.readCelsius();
  double tint = tc.readInternal();
  if (isnan(t)) fallas++;

  Serial.print(nombre);
  Serial.print(" err="); Serial.print(err);
  Serial.print(" T=");   Serial.print(t);
  Serial.print(" Traw="); Serial.print(traw);
  Serial.print(" Int=");  Serial.print(tint);
  Serial.print(" fallas="); Serial.print(fallas);
}

void setup() {
  apagarPotencia();
  seguro.attach(M1);  seguro.write(90);   // plato trabado
  valvula.attach(MV); valvula.write(0);   // válvula cerrada

  Serial.begin(115200);
  Serial.println(F("tc_diag: Peltier y ventiladores APAGADOS"));
  if (!tc1.begin()) Serial.println(F("tc1.begin() fallo"));
  if (!tc2.begin()) Serial.println(F("tc2.begin() fallo"));
  delay(500);
}

void loop() {
  n++;
  Serial.print(F("t=")); Serial.print(millis() / 1000); Serial.print(F("s n=")); Serial.print(n);
  Serial.print(F(" | "));
  imprimirTC("TC1", tc1, fallas1);
  Serial.print(F(" | "));
  imprimirTC("TC2", tc2, fallas2);
  Serial.println();
  delay(1000);
}
