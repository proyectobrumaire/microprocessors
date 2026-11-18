// Diagnóstico del ultrasonido (HC-SR04): imprime el eco en µs y la distancia en cm, y si
// Full_Condenser lo contaría como ave (50 < eco < max_sensor_duration). Servos sin señal,
// Peltier y ventiladores apagados (igual que tests/parada).
const uint8_t PINS_BAJO[] = {10, 11, 13, 5, 4, 39, 33, 30, 31, 2, 28, 29, 3};
const uint8_t TRIG = 12, ECHO = 18;
const uint32_t MIN_US = 50, MAX_US = 500;  // mismos límites que Communication.h

void setup() {
  for (uint8_t p : PINS_BAJO) { pinMode(p, OUTPUT); digitalWrite(p, LOW); }
  pinMode(TRIG, OUTPUT);
  pinMode(ECHO, INPUT_PULLUP);
  Serial.begin(115200);
  Serial.println(F("ULTRASONIDO: eco_us  cm  ave?"));
}

void loop() {
  digitalWrite(TRIG, LOW);  delayMicroseconds(2);
  digitalWrite(TRIG, HIGH); delayMicroseconds(10);
  digitalWrite(TRIG, LOW);
  uint32_t us = pulseIn(ECHO, HIGH, 40000UL);  // 0 = sin eco en 40 ms
  Serial.print(us); Serial.print(F(" us  "));
  Serial.print(us * 0.01715, 1); Serial.print(F(" cm  "));
  Serial.println((us > MIN_US && us < MAX_US) ? F("AVE") : F("-"));
  delay(200);
}
