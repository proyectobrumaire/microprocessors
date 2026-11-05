#include <stdio.h>

#include "Communication.h"
#include "CondenserControl.h"
#include "Debug.h"

const int N_Sensores = 12;


/*=========TIEMPOS=========*/
unsigned long t_tikcer_anterior = 0;
const int tiempoLectura = 1;      // Intervalo de lectura y control en segundos
int tiempoSensor = 0; //Tiempo actual del sensor

/*=========HORARIO DE CONDENSACIÓN (hora local del RTC, solo con hora válida)=========*/
// [inicio, fin) en horas. Fuera de las franjas la Peltier y los ventiladores quedan apagados.
const uint8_t MADRUGADA_INI = 4, MADRUGADA_FIN = 6;   // PWM fijo (sin PI) si es viable
const float   PWM_MADRUGADA = 230;                    // 90 %
const uint8_t MANANA_INI = 8, MANANA_FIN = 10;        // PI con tope si es viable
const float   PWM_TOPE_MANANA = 204;                  // 80 %
const float   PWM_TOPE_HORA_INVALIDA = 255;           // con hora inválida: PI todo el día, 100 %

/*=========VOLCADO=========*/
// Hora válida: una vez al día dentro de [HORA_VOLCADO:00, HORA_VOLCADO+1:00) si pasaron
// DIAS_ENTRE_VOLCADOS días desde el último o si el tanque tiene AGUA_PARA_VOLCADO_G (si la balanza
// está en error, solo los días). Si se pierde la ventana queda para el día siguiente.
// Hora inválida: con el tanque en AGUA_PARA_VOLCADO_G (balanza en error: cada 5 días), pasada
// ESPERA_ARRANQUE_MS desde el arranque y MIN_ENTRE_VOLCADOS_MS desde el último (por millis()).
const uint8_t HORA_VOLCADO = 15;
const uint8_t DIAS_ENTRE_VOLCADOS = 5;
const float   AGUA_PARA_VOLCADO_G = 1000.0;   // 1 L (1 g ≈ 1 ml)
const unsigned long ESPERA_ARRANQUE_MS = 3600000UL;            // 1 h
const unsigned long MIN_ENTRE_VOLCADOS_MS = 86400000UL;        // 24 h
const unsigned long VOLCADO_SIN_BALANZA_MS = 5UL * 86400000UL; // 5 días

/*=========EEPROM=========*/
// 0-8: balanza (CondenserControl). 16-18: yy, mm, dd del último vaciado con hora válida.
// 19-21: yy, mm, dd del piso de la hora válida (última fecha válida vista o del último SET_TIME).
const int EEPROM_VOLCADO_ADDR = 16;
const int EEPROM_PISO_ADDR = 19;

/*=========VALIDEZ DE LA HORA=========*/
// La hora del RTC es válida si: el RTC responde, los campos están en rango (días del mes con
// bisiestos), y la fecha está entre el piso (máx. entre la fecha de compilación y la guardada en
// EEPROM) y la fecha de compilación + TECHO_ANIOS. Se evalúa en cada lectura (cada segundo).
const uint8_t TECHO_ANIOS = 10;

struct FechaHora { uint8_t yy, mm, dd, hh, mi, ss; };

FechaHora ahora;          // última lectura del RTC
bool hora_valida = false;
long dia_hoy = -1;        // días desde 2000-01-01 de `ahora` (-1 si la fecha no está en rango)
long dia_compilacion = 0;
long dia_techo = 0;
long dia_piso_eeprom = -1;

/*=========ESTADO DEL VOLCADO POR millis()=========*/
bool hubo_volcado = false;           // desde el arranque
unsigned long t_ultimo_volcado = 0;
bool arranque_cumplido = false;      // ya pasó ESPERA_ARRANQUE_MS (millis() puede desbordar)


/*=========CONTROL=========*/
float kp = 1.3;
float ki = 0.05;
float maxIntegracion = 100.0;


// Inicialización posicional (NO designators)
CondenserCom::Pins pinsCom{18, 19, 12}; //sensor_interrupt, timer_interrupt, trig

CondenserControl::Pins ctrlCom{
  // DHT (dht_pin1, dht_pin2)
  36, 37,
  // MAX31855  (max_d01, max_cs1, max_clk1, max_d02, max_cs2, max_clk2);
  24, 23, 22, 27, 26, 25,
  // L298N (in1,in2,ena,in3,in4,enb)
  30, 31, 2, 28, 29, 3,
  // IBT-2 (rpwm,lpwm,ren,len)
  5, 4, 39, 33,
  //Balanza (dout, clk)
  A1, A0,
  //Sensor  ACS712
  A3,
  //motores (M1, M2, Mv)
  10, 11, 13,
  //sensor de lluvia (analógico)
  A2
};

//Leds ->

//Instanciar las clases
CondenserCom com(pinsCom);
CondenserControl ctrl (ctrlCom);

float sensores_promedio[N_Sensores];
bool peltier_actual;


/*=========FECHAS=========*/
uint8_t diasDelMes(uint8_t yy, uint8_t mm) {
  static const uint8_t dias[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (mm == 2 && yy % 4 == 0) return 29;  // 2000-2099: bisiesto si yy % 4 == 0
  return dias[mm - 1];
}

bool fechaEnRango(uint8_t yy, uint8_t mm, uint8_t dd) {
  return yy <= 99 && mm >= 1 && mm <= 12 && dd >= 1 && dd <= diasDelMes(yy, mm);
}

// Días desde 2000-01-01 (requiere fechaEnRango)
long diaNumero(uint8_t yy, uint8_t mm, uint8_t dd) {
  static const uint16_t acumulado[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
  long n = 365L * yy + (yy + 3) / 4;  // bisiestos de los años 2000..(2000+yy-1)
  n += acumulado[mm - 1] + dd - 1;
  if (mm > 2 && yy % 4 == 0) n++;
  return n;
}

// Fecha de compilación desde __DATE__ ("Nov  5 2026")
void fechaCompilacion(uint8_t &yy, uint8_t &mm, uint8_t &dd) {
  const char *f = __DATE__;
  const char *meses = "JanFebMarAprMayJunJulAugSepOctNovDec";
  mm = 1;
  for (uint8_t i = 0; i < 12; i++) {
    if (strncmp(f, meses + 3 * i, 3) == 0) { mm = i + 1; break; }
  }
  dd = (f[4] == ' ' ? 0 : f[4] - '0') * 10 + (f[5] - '0');
  yy = (f[9] - '0') * 10 + (f[10] - '0');
}

// Día guardado en EEPROM (3 bytes yy, mm, dd); -1 si vacío o inválido
long leerDiaEeprom(int addr) {
  uint8_t yy = EEPROM.read(addr), mm = EEPROM.read(addr + 1), dd = EEPROM.read(addr + 2);
  return fechaEnRango(yy, mm, dd) ? diaNumero(yy, mm, dd) : -1;
}

void guardarFechaEeprom(int addr, uint8_t yy, uint8_t mm, uint8_t dd) {
  EEPROM.update(addr, yy);       // update: solo escribe si el byte cambió
  EEPROM.update(addr + 1, mm);
  EEPROM.update(addr + 2, dd);
}

void guardarPiso(const FechaHora &f) {
  guardarFechaEeprom(EEPROM_PISO_ADDR, f.yy, f.mm, f.dd);
  dia_piso_eeprom = diaNumero(f.yy, f.mm, f.dd);
}

bool volcadoHechoHoy() {
  return EEPROM.read(EEPROM_VOLCADO_ADDR) == ahora.yy && EEPROM.read(EEPROM_VOLCADO_ADDR + 1) == ahora.mm &&
         EEPROM.read(EEPROM_VOLCADO_ADDR + 2) == ahora.dd;
}


/*=========HORA=========*/
// Lee el RTC y evalúa la validez. Con hora válida guarda el piso una vez al día (primera lectura
// válida de cada día nuevo).
void evaluarHora() {
  bool rtc_ok = com.get_fecha_hora(ahora.yy, ahora.mm, ahora.dd, ahora.hh, ahora.mi, ahora.ss);
  bool en_rango = rtc_ok && fechaEnRango(ahora.yy, ahora.mm, ahora.dd) &&
                  ahora.hh < 24 && ahora.mi < 60 && ahora.ss < 60;
  dia_hoy = en_rango ? diaNumero(ahora.yy, ahora.mm, ahora.dd) : -1;
  long piso = max(dia_compilacion, dia_piso_eeprom);
  bool valida = en_rango && dia_hoy >= piso && dia_hoy <= dia_techo;

  if (valida != hora_valida) {
    DBG(valida ? "Hora VALIDA: " : "Hora INVALIDA: ");
    DBG(ahora.yy); DBG("-"); DBG(ahora.mm); DBG("-"); DBG(ahora.dd); DBG(" ");
    DBG(ahora.hh); DBG(":"); DBG(ahora.mi); DBG(":"); DBG(ahora.ss);
    DBG(" rtc_ok="); DBG(rtc_ok); DBG(" dia="); DBG(dia_hoy); DBG(" piso="); DBG(piso);
    DBG(" techo="); DBGLN(dia_techo);
  }
  hora_valida = valida;

  if (hora_valida && dia_hoy > dia_piso_eeprom) guardarPiso(ahora);
}

// Modo de la Peltier según la hora
void aplicarHorario() {
  if (!hora_valida) {
    ctrl.set_modo(CondenserControl::PELTIER_PI, PWM_TOPE_HORA_INVALIDA);
  } else if (ahora.hh >= MADRUGADA_INI && ahora.hh < MADRUGADA_FIN) {
    ctrl.set_modo(CondenserControl::PELTIER_PWM_FIJO, PWM_MADRUGADA);
  } else if (ahora.hh >= MANANA_INI && ahora.hh < MANANA_FIN) {
    ctrl.set_modo(CondenserControl::PELTIER_PI, PWM_TOPE_MANANA);
  } else {
    ctrl.set_modo(CondenserControl::PELTIER_APAGADA, 0);
  }
}


/*=========VOLCADO=========*/
void revisarVolcado() {
  unsigned long t = millis();
  if (!arranque_cumplido && t >= ESPERA_ARRANQUE_MS) arranque_cumplido = true;

  bool balanza_ok = !ctrl.balanza_con_error();
  bool hay_agua = balanza_ok && ctrl.get_peso_agua() >= AGUA_PARA_VOLCADO_G;
  bool volcar = false;

  if (hora_valida) {
    if (ahora.hh == HORA_VOLCADO && !volcadoHechoHoy()) {
      long ultimo = leerDiaEeprom(EEPROM_VOLCADO_ADDR);
      // Sin registro, o registro en el futuro (el RTC estuvo adelantado): toca vaciar
      bool por_dias = ultimo < 0 || ultimo > dia_hoy || dia_hoy - ultimo >= DIAS_ENTRE_VOLCADOS;
      volcar = por_dias || hay_agua;
    }
  } else if (arranque_cumplido) {
    if (balanza_ok) {
      volcar = hay_agua && (!hubo_volcado || t - t_ultimo_volcado >= MIN_ENTRE_VOLCADOS_MS);
    } else {
      volcar = !hubo_volcado || t - t_ultimo_volcado >= VOLCADO_SIN_BALANZA_MS;
    }
  }
  if (!volcar) return;

  DBG("Hora de volcar el plato (hora "); DBG(hora_valida ? "valida" : "INVALIDA");
  DBG(", peso "); DBG(ctrl.get_peso_agua()); DBGLN(")");
  FechaHora inicio = ahora;  // fecha con la que se registra el vaciado
  bool inicio_valida = hora_valida;
  ctrl.ejecutar_volcado();
  if (inicio_valida) guardarFechaEeprom(EEPROM_VOLCADO_ADDR, inicio.yy, inicio.mm, inicio.dd);
  hubo_volcado = true;
  t_ultimo_volcado = millis();
  ctrl.promediar(sensores_promedio);
  com.when_event(CondenserCom::VOLCADO, sensores_promedio);
}


void setup(void) {
#if DEBUG
  Serial.begin(115200);   //Debugging por USB
#endif
  DBG("booting arduino...");
  DBGLN("done");

  uint8_t cy, cm, cd;
  fechaCompilacion(cy, cm, cd);
  dia_compilacion = diaNumero(cy, cm, cd);
  dia_techo = diaNumero(cy + TECHO_ANIOS, cm, cd);
  dia_piso_eeprom = leerDiaEeprom(EEPROM_PISO_ADDR);

  ctrl.set_PI_parameters(kp, ki, maxIntegracion);
  ctrl.iniciar_control();
  com.iniciar_comunicaciones();

  delay(1000);
  DBGLN("Sistema listo.");

  evaluarHora();
  aplicarHorario();
  ctrl.leer_sensores_y_controlar();
  ctrl.promediar(sensores_promedio); //Primera lectura
  com.when_event(CondenserCom::BOOT, sensores_promedio);
  peltier_actual = ctrl.peltier_on;
}


void loop(void) {
  com.recieve_commands();

  //SET_TIME de la app: la hora del teléfono es absoluta; su fecha pasa a ser el piso (aunque
  //sea anterior al guardado), para que la app siempre pueda corregir el RTC.
  if (com.takeSetTimeFlag()) {
    FechaHora f;
    if (com.get_fecha_hora(f.yy, f.mm, f.dd, f.hh, f.mi, f.ss) && fechaEnRango(f.yy, f.mm, f.dd)) {
      guardarPiso(f);
      DBGLN("SET_TIME recibido: piso actualizado");
    }
  }

  //Eviar pulso (Aquí sucede una interrupción) por el mismo sensor
  com.sendSensorPulse();
  delay(60); //No sobre carga por la int

  //Manejar la interrupción del timer (reporte periódico)
 if (com.takeTimerFlag()) {
    DBGLN("Flag from Timer Taken");
    ctrl.promediar(sensores_promedio);
    com.clearRtcTimerFlags();

    com.when_event(CondenserCom::PERIODIC, sensores_promedio);
  }

  //Manejar la interrupción del sensor
  if (com.takeSensorFlag()) {
    //DBGLN(String(com.lastSensorFlagRaisen));
    DBGLN("Flag from Sensor Taken");
    ctrl.promediar(sensores_promedio);
    com.when_event(CondenserCom::BIRD, sensores_promedio);
  }

  //Mirar si la celda peltier cambió de estado
  if (ctrl.peltier_on^peltier_actual) {
    //DBGLN(String(com.lastSensorFlagRaisen));
    DBGLN("Flag from Peltier Control Taken");
    ctrl.promediar(sensores_promedio);
    uint8_t ev = ctrl.peltier_on ? CondenserCom::PELTIER_ON : CondenserCom::PELTIER_OFF;
    com.when_event(ev, sensores_promedio);
    peltier_actual = ctrl.peltier_on;
  }

  //Contador de segundos
  unsigned long t_tikcer_actual = millis();  //Cuánto lleva prendido el arduino en milisegundos
  if (t_tikcer_actual - t_tikcer_anterior >= 1000) {
    t_tikcer_anterior = t_tikcer_actual;
    tiempoSensor++;
  }

  // Cada tiempoLectura: hora → horario → lectura y control → vaciado
  if (tiempoSensor >= tiempoLectura) {
    evaluarHora();
    aplicarHorario();
    ctrl.leer_sensores_y_controlar();  //Aquí se ejecuta el control
    revisarVolcado();
    tiempoSensor = 0;
  }
}
