// Prueba del servo "seguro" (pin 10). Peltier y ventiladores APAGADOS, válvula cerrada,
// servo de volcado desconectado (como en reposo). Usar con el plato VACÍO.
//
// Secuencia automática (tras 5 s): 90 -> 0 -> 90 -> 0 -> 90 (3 s entre pasos),
// primero en el pin 10 (seguro) y luego en el pin 11 (conector del volcado).
// Después acepta comandos por Serial (115200): "s<angulo>", "p<pin>" (10/11), "d" desconecta, "a" reconecta.

#include <Servo.h>

const uint8_t M1 = 10, M2 = 11, MV = 13;
uint8_t pinActual = M1;
const uint8_t POWER_PINS[] = {5, 4, 39, 33, 30, 31, 2, 28, 29, 3};  // IBT-2 y L298N

Servo seguro, valvula;

void mover(int ang, const __FlashStringHelper *nota, unsigned long espera) {
  seguro.write(ang);
  Serial.print(F("t=")); Serial.print(millis() / 1000.0, 1);
  Serial.print(F("s seguro -> ")); Serial.print(ang); Serial.print(F(" grados  "));
  Serial.println(nota);
  delay(espera);
}

void secuencia(uint8_t pin) {
  seguro.detach();
  seguro.write(90); seguro.attach(pin);
  Serial.print(F("=== Probando pin ")); Serial.println(pin);
  mover(90, F("(trabado)"), 3000);
  mover(0,  F("(suelto)"), 3000);
  mover(90, F("(trabado)"), 3000);
  mover(0,  F("(suelto)"), 3000);
  mover(90, F("(trabado)"), 3000);
  seguro.detach();
}

void setup() {
  for (uint8_t p : POWER_PINS) { pinMode(p, OUTPUT); digitalWrite(p, LOW); }
  valvula.write(0); valvula.attach(MV);  // válvula cerrada

  Serial.begin(115200);
  Serial.println(F("seguro_test: Peltier/ventiladores APAGADOS. Inicia en 5 s"));
  delay(5000);

  secuencia(M1);   // pin 10: donde el firmware espera el seguro
  delay(3000);
  secuencia(M2);   // pin 11: conector del servo de volcado

  seguro.write(90); seguro.attach(M1);
  Serial.println(F("Listo (seguro en pin 10 a 90). Comandos: s<angulo>, p<pin> (10/11), d, a"));
}

void loop() {
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.startsWith("s")) {
      int ang = constrain(cmd.substring(1).toInt(), 0, 180);
      mover(ang, F("(manual)"), 0);
    } else if (cmd.startsWith("p")) {
      pinActual = cmd.substring(1).toInt();
      seguro.detach(); seguro.write(90); seguro.attach(pinActual);
      Serial.print(F("servo ahora en pin ")); Serial.println(pinActual);
    } else if (cmd == "d") {
      seguro.detach(); Serial.println(F("seguro desconectado (sin fuerza)"));
    } else if (cmd == "a") {
      seguro.attach(pinActual); Serial.println(F("seguro conectado"));
    }
  }
}
