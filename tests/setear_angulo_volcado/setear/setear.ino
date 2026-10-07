#include <Servo.h>0


const uint8_t PIN_VOLCADO = 11;                  // Pin del servomotor de volcado
const int ANGULO_INICIAL = 90;                   // Cambia este valor al ángulo que necesites (0 a 180) reposo
const uint8_t POWER_PINS[] = {5, 4, 39, 33, 30, 31, 2, 28, 29, 3}; // Pines de potencia auxiliares de tu hardware

Servo volcado;

void setup() {
  // Inicializa los pines de potencia en LOW por seguridad
  for (uint8_t p : POWER_PINS) {
    pinMode(p, OUTPUT);
    digitalWrite(p, LOW);
  }

  Serial.begin(115200);
  
  // Posiciona el servo antes de activarlo para evitar tirones bruscos
  volcado.write(ANGULO_INICIAL);
  volcado.attach(PIN_VOLCADO);
  
  Serial.println(F("--- SCRIPT DE CONTROL DE VOLCADO ---"));
  Serial.print(F("Volcado posicionado y bloqueado en: "));
  Serial.print(ANGULO_INICIAL);
  Serial.println(F(" grados."));
  Serial.println(F("Puedes enviar cualquier valor entre 0 y 180 por el puerto serie para cambiar el ángulo."));
}
void loop() {
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim(); // Elimina espacios o saltos de línea sobrantes
    
    if (cmd.length() > 0) {
      int nuevoAngulo = cmd.toInt();
      if (nuevoAngulo >= 0 && nuevoAngulo <= 180) {
        volcado.write(nuevoAngulo);
        Serial.print(F("Ángulo actualizado a: "));
        Serial.println(nuevoAngulo);
      } else {
        Serial.println(F("Error: Ingresa un valor entre 0 y 180"));
      }
    }
  }
}