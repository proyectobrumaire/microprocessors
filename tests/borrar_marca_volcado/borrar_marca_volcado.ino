// Borra la marca del último vaciado diario de Full_Condenser (EEPROM 16-18: yy, mm, dd),
// para poder forzar un vaciado de prueba el mismo día. No toca la tara de la balanza (0-8).
// Peltier y ventiladores APAGADOS. Después de usarlo, volver a cargar Full_Condenser.

#include <EEPROM.h>

const int EEPROM_VOLCADO_ADDR = 16;  // igual que Full_Condenser.ino
const uint8_t POWER_PINS[] = {5, 4, 39, 33, 30, 31, 2, 28, 29, 3};  // IBT-2 y L298N

void imprimirMarca(const __FlashStringHelper *titulo) {
  Serial.print(titulo);
  for (int i = 0; i < 3; i++) {
    Serial.print(EEPROM.read(EEPROM_VOLCADO_ADDR + i));
    Serial.print(i < 2 ? '-' : '\n');
  }
}

void setup() {
  for (uint8_t p : POWER_PINS) { pinMode(p, OUTPUT); digitalWrite(p, LOW); }
  Serial.begin(115200);
  delay(500);
  imprimirMarca(F("Marca antes (yy-mm-dd): "));
  for (int i = 0; i < 3; i++) EEPROM.update(EEPROM_VOLCADO_ADDR + i, 0xFF);
  imprimirMarca(F("Marca después:          "));
  Serial.println(F("Listo. Vuelve a cargar Full_Condenser."));
}

void loop() {}
