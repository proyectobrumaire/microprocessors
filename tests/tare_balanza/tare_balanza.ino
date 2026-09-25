// Tara de la celda de carga (HX711) y guardado en EEPROM, compatible con Full_Condenser.
// Usar con la celda VACÍA. Peltier y ventiladores quedan apagados.
//
// EEPROM (igual que CondenserControl): offset long @0, escala float @4, flag 0x42 @8.
// Imprime la lectura antes, hace la tara, guarda y verifica durante 20 s.

#include <EEPROM.h>
#include <HX711.h>

const uint8_t DOUT = A1, CLK = A0;
const float SCALE_DEFAULT = -422.55f;
const int EEPROM_OFFSET_ADDR = 0, EEPROM_SCALE_ADDR = 4, EEPROM_FLAG_ADDR = 8;
const byte CALIB_OK = 0x42;

// IBT-2 (rpwm, lpwm, ren, len) y L298N (in1, in2, ena, in3, in4, enb)
const uint8_t POWER_PINS[] = {5, 4, 39, 33, 30, 31, 2, 28, 29, 3};

HX711 balanza;

void setup() {
  for (uint8_t p : POWER_PINS) { pinMode(p, OUTPUT); digitalWrite(p, LOW); }
  Serial.begin(115200);
  Serial.println(F("tare_balanza: Peltier y ventiladores APAGADOS"));

  balanza.begin(DOUT, CLK);
  if (!balanza.wait_ready_timeout(2000)) {
    Serial.println(F("ERROR: balanza no encontrada"));
    while (1) delay(1000);
  }

  // Lectura con la calibración actual de la EEPROM
  long offset_ant; float escala_ant;
  EEPROM.get(EEPROM_OFFSET_ADDR, offset_ant);
  EEPROM.get(EEPROM_SCALE_ADDR, escala_ant);
  balanza.set_scale(escala_ant);
  balanza.set_offset(offset_ant);
  Serial.print(F("Antes -> offset=")); Serial.print(offset_ant);
  Serial.print(F(" escala=")); Serial.print(escala_ant, 3);
  Serial.print(F(" lectura=")); Serial.println(balanza.get_units(10), 2);

  // Tara: promedio de 40 lecturas crudas con la celda vacía
  delay(1000);
  long nuevo_offset = balanza.read_average(40);
  balanza.set_scale(SCALE_DEFAULT);
  balanza.set_offset(nuevo_offset);
  EEPROM.put(EEPROM_OFFSET_ADDR, nuevo_offset);
  EEPROM.put(EEPROM_SCALE_ADDR, SCALE_DEFAULT);
  EEPROM.write(EEPROM_FLAG_ADDR, CALIB_OK);
  Serial.print(F("Tara guardada -> offset=")); Serial.print(nuevo_offset);
  Serial.print(F(" escala=")); Serial.println(SCALE_DEFAULT, 3);
}

void loop() {
  static unsigned long t0 = millis();
  if (millis() - t0 < 20000) {
    Serial.print(F("Verificacion: ")); Serial.println(balanza.get_units(5), 2);
    delay(1000);
  }
}
