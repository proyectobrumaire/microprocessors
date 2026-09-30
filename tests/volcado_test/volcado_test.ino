// Prueba del volcado (pin 11) coordinado con el seguro (pin 10) y la válvula (pin 13). Peltier y
// ventiladores APAGADOS. El seguro traba el plato en reposo y bloquea el giro en ambas direcciones,
// así que el volcado solo se mueve con el seguro SUELTO.
//
// Arranque: seguro SUELTO, volcado sin conectar ESPERA_INICIAL_MS; luego el volcado se conecta en
// REPOSO, espera ESPERA_SEGURO_MS, traba el seguro y suelta el volcado (el seguro sujeta el plato).
//
// El volcado solo tiene fuerza mientras el seguro está suelto: se conecta en REPOSO antes de soltar
// el seguro y se desconecta después de trabarlo. Así el plato nunca queda sin sujeción.
//
// Ciclo automático (en bucle):
//   conectar volcado en REPOSO -> soltar seguro -> 5 s -> volcado a VOLCADO -> 5 s volcado -> volcado a REPOSO -> 5 s -> trabar seguro
//   -> soltar volcado -> abrir válvula TIEMPO_RELLENO_MS (rellena con el plato trabado) -> cerrar válvula -> 5 s
//
// Comandos por Serial (115200):
//   g<angulo>  va LENTO al ángulo (0-180), 1° cada PASO_MS (solo con el seguro suelto)
//   d          suelta el volcado de inmediato (sin fuerza)
//   a          vuelve a conectar el volcado en el último ángulo
//   u / l      suelta / traba el seguro (trabar solo con el volcado en REPOSO)
//   p<ms>      cambia la velocidad de "g" (ms por grado)
//   c / s      inicia / detiene el ciclo automático
//   o / x      abre / cierra la válvula (abrir solo con el seguro TRABADO)
//   ?          muestra el estado
// Cualquier comando interrumpe el movimiento o la espera en curso.

#include <Servo.h>

const uint8_t M1 = 10, M2 = 11, MV = 13;          // seguro, volcado, válvula
const int SEGURO_TRABADO = 0, SEGURO_SUELTO = 90;  // verificados con seguro_test (2026-09-24)
const int REPOSO = 78;                             // volcado en reposo (calibrado 2026-09-29)
const int VOLCADO = 0;                             // volcado en posición de vaciado
const unsigned long ESPERA_INICIAL_MS = 5000;      // volcado sin conectar al arrancar
const unsigned long ESPERA_SEGURO_MS = 5000;       // tiempo muerto entre seguro y rotación
const unsigned long PAUSA_VOLCADO_MS = 5000;       // tiempo en la posición de volcado
const int VALVULA_CERRADA = 90, VALVULA_ABIERTA = 0;  // verificados en hardware (2026-09-29); Full_Condenser los tiene al revés
const unsigned long TIEMPO_RELLENO_MS = 15000;    // válvula abierta en cada relleno
const unsigned long PASO_CICLO_MS = 20;            // velocidad del ciclo (ms por grado)
unsigned long PASO_MS = 1000;                      // velocidad de "g" (ms por grado); cambia con "p"
const uint8_t POWER_PINS[] = {5, 4, 39, 33, 30, 31, 2, 28, 29, 3};  // IBT-2 y L298N

Servo seguro, volcado, valvula;
int anguloActual = REPOSO;
bool seguroTrabado = false;
bool valvulaAbierta = false;
bool ciclando = true;
unsigned long ciclos = 0;

void estado() {
  Serial.print(F("t=")); Serial.print(millis() / 1000.0, 1);
  Serial.print(F("s volcado=")); Serial.print(anguloActual);
  Serial.print(volcado.attached() ? F(" (con fuerza)") : F(" (sin fuerza)"));
  Serial.print(seguroTrabado ? F(" seguro=TRABADO") : F(" seguro=SUELTO"));
  Serial.print(valvulaAbierta ? F(" valvula=ABIERTA") : F(" valvula=cerrada"));
  Serial.println();
}

// Espera ms milisegundos; devuelve false si llegó un comando (interrumpe).
bool esperar(unsigned long ms) {
  unsigned long t0 = millis();
  while (millis() - t0 < ms) {
    if (Serial.available()) return false;
    delay(10);
  }
  return true;
}

void conectarVolcado() {
  if (volcado.attached()) return;
  volcado.write(anguloActual);                      // posición fijada antes de attach
  volcado.attach(M2);
  delay(500);                                       // que tome posición antes de seguir
  Serial.println(F("volcado -> conectado (con fuerza)"));
}

void soltarVolcado() {
  if (!volcado.attached()) return;
  volcado.detach();
  Serial.println(F("volcado -> sin fuerza (el seguro sujeta el plato)"));
}

void soltarSeguro() {
  conectarVolcado();                                // el volcado sujeta el plato antes de soltar el seguro
  if (valvulaAbierta) cerrarValvula();              // nunca rellenar con el plato suelto
  seguro.write(SEGURO_SUELTO); seguroTrabado = false;
  Serial.println(F("seguro -> SUELTO")); estado();
}

void trabarSeguro() {
  if (anguloActual != REPOSO) {
    Serial.println(F("no se traba: el volcado no está en REPOSO"));
    return;
  }
  seguro.write(SEGURO_TRABADO); seguroTrabado = true;
  Serial.println(F("seguro -> TRABADO")); estado();
}

void abrirValvula() {
  if (!seguroTrabado) {
    Serial.println(F("no se abre la válvula: el seguro no está TRABADO"));
    return;
  }
  valvula.write(VALVULA_ABIERTA); valvulaAbierta = true;
  Serial.println(F("válvula -> ABIERTA")); estado();
}

void cerrarValvula() {
  valvula.write(VALVULA_CERRADA); valvulaAbierta = false;
  Serial.println(F("válvula -> cerrada")); estado();
}

// Movimiento lento; devuelve false si no se movió o lo interrumpió un comando.
bool irLento(int destino, unsigned long pasoMs) {
  destino = constrain(destino, 0, 180);
  if (!volcado.attached()) {
    Serial.println(F("volcado sin fuerza: usa 'a' para conectarlo antes de moverlo"));
    return false;
  }
  if (seguroTrabado) {
    Serial.println(F("seguro TRABADO: usa 'u' para soltarlo antes de mover el volcado"));
    return false;
  }
  if (valvulaAbierta) {
    Serial.println(F("válvula ABIERTA: ciérrala con 'x' antes de mover el volcado"));
    return false;
  }
  int paso = (destino > anguloActual) ? 1 : -1;
  while (anguloActual != destino) {
    anguloActual += paso;
    volcado.write(anguloActual);
    if (anguloActual % 5 == 0) estado();             // aviso cada 5°
    delay(pasoMs);
    if (Serial.available()) { estado(); return false; }
  }
  estado();
  return true;
}

void setup() {
  for (uint8_t p : POWER_PINS) { pinMode(p, OUTPUT); digitalWrite(p, LOW); }
  seguro.write(SEGURO_SUELTO); seguro.attach(M1);   // suelto mientras el volcado busca el reposo
  valvula.write(VALVULA_CERRADA); valvula.attach(MV); // válvula cerrada (posición fijada antes de attach)

  Serial.begin(115200);
  Serial.println(F("volcado_test: volcado SIN conectar, seguro SUELTO."));
  Serial.print(F("En ")); Serial.print(ESPERA_INICIAL_MS / 1000); Serial.print(F(" s se conecta el volcado en REPOSO ("));
  Serial.print(REPOSO); Serial.println(F(" grados)."));
  delay(ESPERA_INICIAL_MS);
  volcado.write(REPOSO);                            // posición fijada antes de attach
  volcado.attach(M2);
  anguloActual = REPOSO;
  estado();
  delay(ESPERA_SEGURO_MS);
  trabarSeguro();
  if (seguroTrabado) soltarVolcado();
  Serial.println(F("Comandos: g<angulo>, p<ms>, c, s, d, a, u, l, o, x, ?"));
  Serial.println(F("Ciclo automático activo."));
  esperar(ESPERA_SEGURO_MS);                        // trabado 5 s antes del primer ciclo
}

void cicloCompleto() {
  soltarSeguro();
  if (!esperar(ESPERA_SEGURO_MS)) return;
  if (!irLento(VOLCADO, PASO_CICLO_MS)) return;
  if (!esperar(PAUSA_VOLCADO_MS)) return;
  if (!irLento(REPOSO, PASO_CICLO_MS)) return;
  if (!esperar(ESPERA_SEGURO_MS)) return;
  trabarSeguro();
  if (!seguroTrabado) return;
  soltarVolcado();
  abrirValvula();
  bool completo = esperar(TIEMPO_RELLENO_MS);
  cerrarValvula();                                  // se cierra siempre, aunque se interrumpa
  if (!completo) return;
  ciclos++;
  Serial.print(F("ciclo ")); Serial.print(ciclos); Serial.println(F(" completo"));
  esperar(ESPERA_SEGURO_MS);
}

void loop() {
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.startsWith("g")) {
      ciclando = false;
      irLento(cmd.substring(1).toInt(), PASO_MS);
    } else if (cmd == "d") {
      ciclando = false;
      volcado.detach(); Serial.println(F("volcado SIN FUERZA")); estado();
    } else if (cmd == "a") {
      volcado.write(anguloActual); volcado.attach(M2); Serial.println(F("volcado conectado")); estado();
    } else if (cmd == "u") {
      soltarSeguro();
    } else if (cmd == "l") {
      trabarSeguro();
    } else if (cmd == "o") {
      abrirValvula();
    } else if (cmd == "x") {
      cerrarValvula();
    } else if (cmd.startsWith("p")) {
      PASO_MS = constrain(cmd.substring(1).toInt(), 1, 5000);
      Serial.print(F("velocidad: ")); Serial.print(PASO_MS); Serial.println(F(" ms por grado"));
    } else if (cmd == "c") {
      ciclando = true; Serial.println(F("ciclo automático: INICIADO"));
    } else if (cmd == "s") {
      ciclando = false; Serial.println(F("ciclo automático: DETENIDO")); estado();
    } else if (cmd == "?") {
      estado();
    }
  }

  if (ciclando) cicloCompleto();
}
