// Mueve SOLO el servo del seguro (pin 10), alternando trabado/suelto cada 3 s, en bucle.
// Para diagnosticar si el seguro responde (cable, conector o servo).
// Seguridad: Peltier y ventiladores APAGADOS, válvula CERRADA (90°), y el volcado (pin 11)
// sujeta el plato en reposo (80°) para que no quede suelto cuando el seguro se abre.

#include <Servo.h>

const uint8_t M_SEGURO = 10, M_VOLCADO = 11, M_VALVULA = 13;
const int SEGURO_TRABADO = 0, SEGURO_SUELTO = 90;
const int VOLCADO_REPOSO = 80, VALVULA_CERRADA = 90;   // calibrados (CondenserControl.h)
const uint8_t POWER_PINS[] = {5, 4, 39, 33, 30, 31, 2, 28, 29, 3};  // IBT-2 y L298N

Servo seguro, volcado, valvula;
unsigned long n = 0;

void setup() {
  for (uint8_t p : POWER_PINS) { pinMode(p, OUTPUT); digitalWrite(p, LOW); }
  valvula.write(VALVULA_CERRADA); valvula.attach(M_VALVULA);
  volcado.write(VOLCADO_REPOSO);  volcado.attach(M_VOLCADO);
  seguro.write(SEGURO_TRABADO);   seguro.attach(M_SEGURO);
  Serial.begin(115200);
  Serial.println(F("seguro_solo: pin 10 alterna TRABADO (0) / SUELTO (90) cada 3 s"));
}

void loop() {
  n++;
  int ang = (n % 2) ? SEGURO_SUELTO : SEGURO_TRABADO;
  seguro.write(ang);
  Serial.print(F("t=")); Serial.print(millis() / 1000.0, 1);
  Serial.print(F("s seguro (pin 10) -> ")); Serial.print(ang);
  Serial.println(ang == SEGURO_SUELTO ? F(" SUELTO") : F(" TRABADO"));
  delay(3000);
}
