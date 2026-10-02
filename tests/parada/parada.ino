// Parada segura: ningún servo recibe pulsos (señales en bajo) y la Peltier y los ventiladores
// quedan APAGADOS. Usar para detener cualquier prueba de inmediato.
const uint8_t PINS_BAJO[] = {10, 11, 13, 5, 4, 39, 33, 30, 31, 2, 28, 29, 3};

void setup() {
  for (uint8_t p : PINS_BAJO) { pinMode(p, OUTPUT); digitalWrite(p, LOW); }
  Serial.begin(115200);
  Serial.println(F("PARADA: servos sin señal, Peltier y ventiladores apagados"));
}

void loop() {}
