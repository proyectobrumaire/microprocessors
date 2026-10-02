#include <stdio.h>

#include "Communication.h"
#include "CondenserControl.h"
#include "Debug.h"

const int N_Sensores = 12;


/*=========TIEMPOS=========*/
unsigned long t_tikcer_anterior = 0;
const int tiempoLectura = 1;      // Intervalo de lectura y control en segundos
int tiempoSensor = 0; //Tiempo actual del sensor

/*=========VOLCADO=========*/
// Vaciado diario a hora fija (hora local del RTC). Se evalúa en cada tick del timer (cada 5 min):
// vacía si ya pasó HORA_VOLCADO:MINUTO_VOLCADO y todavía no se vació hoy (resolución de 5 min por el timer). La fecha del último vaciado se guarda en
// EEPROM, así un reinicio no repite el vaciado del día ni lo salta (si se reinicia después de la
// hora, vacía en el siguiente tick).
const uint8_t HORA_VOLCADO = 21;       // hora (0-23, hora local del RTC) desde la que se vacía
const uint8_t MINUTO_VOLCADO = 40;     // minuto (0-59)
const int EEPROM_VOLCADO_ADDR = 16;    // 3 bytes: yy, mm, dd del último vaciado (la balanza usa 0-8)

bool fechaRtcValida(uint8_t yy, uint8_t mm, uint8_t dd, uint8_t hh, uint8_t mi) {
  return yy >= 20 && mm >= 1 && mm <= 12 && dd >= 1 && dd <= 31 && hh < 24 && mi < 60;
}

bool volcadoHechoHoy(uint8_t yy, uint8_t mm, uint8_t dd) {
  return EEPROM.read(EEPROM_VOLCADO_ADDR) == yy && EEPROM.read(EEPROM_VOLCADO_ADDR + 1) == mm &&
         EEPROM.read(EEPROM_VOLCADO_ADDR + 2) == dd;
}

void marcarVolcadoHoy(uint8_t yy, uint8_t mm, uint8_t dd) {
  EEPROM.update(EEPROM_VOLCADO_ADDR, yy);
  EEPROM.update(EEPROM_VOLCADO_ADDR + 1, mm);
  EEPROM.update(EEPROM_VOLCADO_ADDR + 2, dd);
}


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


void setup(void) {
#if DEBUG
  Serial.begin(115200);   //Debugging por USB
#endif
  DBG("booting arduino...");
  DBGLN("done");

  ctrl.set_PI_parameters(kp, ki, maxIntegracion);
  ctrl.iniciar_control();
  com.iniciar_comunicaciones();
 
  delay(1000);
  DBGLN("Sistema listo.");

  ctrl.leer_sensores_y_controlar();
  ctrl.promediar(sensores_promedio); //Primera lectura
  com.when_event(CondenserCom::BOOT, sensores_promedio);
  peltier_actual = ctrl.peltier_on;
}


void loop(void) {
  com.recieve_commands();

  //Eviar pulso (Aquí sucede una interrupción) por el mismo sensor
  com.sendSensorPulse();
  delay(60); //No sobre carga por la int
  
  //Manejar la interrupción del timer
 if (com.takeTimerFlag()) {
    DBGLN("Flag from Timer Taken");
    ctrl.promediar(sensores_promedio);
    com.clearRtcTimerFlags();

    com.when_event(CondenserCom::PERIODIC, sensores_promedio);

    // Verificar si es hora de volcar (una vez al día, desde HORA_VOLCADO:MINUTO_VOLCADO)
    uint8_t yy, mm, dd, hh, mi;
    com.get_fecha_hora(yy, mm, dd, hh, mi);
    if (!fechaRtcValida(yy, mm, dd, hh, mi)) {
      DBGLN("RTC con fecha inválida: vaciado omitido (sincroniza la hora desde la app)");
    } else if (hh * 60 + mi >= HORA_VOLCADO * 60 + MINUTO_VOLCADO && !volcadoHechoHoy(yy, mm, dd)) {
      DBGLN("Hora de volcar el plato");
      ctrl.ejecutar_volcado();
      com.when_event(CondenserCom::VOLCADO, sensores_promedio);
      marcarVolcadoHoy(yy, mm, dd);
    }
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

  // Lectura de sensores cada tiempoLectura
  if (tiempoSensor == tiempoLectura) {
    ctrl.leer_sensores_y_controlar();  //Aquí se ejecuta el control
    tiempoSensor = 0;
  }
}
