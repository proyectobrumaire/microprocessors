#include <stdio.h>

#include "Communication.h"
#include "CondenserControl.h"

const int N_Sensores = 16;


/*=========TIEMPOS=========*/
unsigned long t_tikcer_anterior = 0;
const int tiempoLectura = 1;      // Intervalo de lectura y control en segundos
int tiempoSensor = 0; //Tiempo actual del sensor


/*=========CONTROL=========*/
float kp = 1.3;
float ki = 0.05;
float maxIntegracion = 100.0;


// Inicialización posicional (NO designators)
CondenserCom::Pins pinsCom{ 18, 19, 12}; //sensor_interrupt, timer_interrupt, trig

CondenserControl::Pins ctrlCom{
  // DHT
  30, 31, DHT22,
  // MAX31855 (tc1, tc2)
  22, 23, 24, 25, 26, 27,
  // L298N (in1,in2,ena,in3,in4,enb)
  2, 3, 4, 5, 6, 7,
  // IBT-2 (rpwm,lpwm,ren,len)
  8, 9, 10, 11,
  //Sensor de corriente
  A2, A3, A4,
  //Balanza
  A1, A0
};

//Instanciar las clases
CondenserCom com(pinsCom);
CondenserControl ctrl (ctrlCom);

float sensores_promedio[N_Sensores];

void setup(void) {
  Serial.begin(9600);   //Debugging...
  Serial.print("booting arduino...");
  Serial.println("done");

  ctrl.set_PI_parameters(kp, ki, maxIntegracion);
  ctrl.iniciar_control();
  com.iniciar_comunicaciones();
 
  delay(1000);
  Serial.println("Sistema listo.");

  ctrl.leer_sensores_y_controlar();
  ctrl.promediar(sensores_promedio); //Primera lectura
  com.report_boot(sensores_promedio); //Repotar el boot
}


void loop(void) {

  //Eviar pulso (Aquí sucede una interrupción) por el mismo sensor
  com.sendSensorPulse();
  delay(60); //No sobre carga por la int
  
  //Manejar la interrupción del timer
 if (com.takeTimerFlag()) {
    Serial.println("Flag from Timer Taken");
    ctrl.promediar(sensores_promedio);
    com.clearRtcTimerFlags();
    com.handle_interruption(false, sensores_promedio); //sin foto
  }

  //Manejar la interrupción del sensor
  if (com.takeSensorFlag()) {
    //Serial.println(String(com.lastSensorFlagRaisen));
    Serial.println("Flag from Sensor Taken");
    ctrl.promediar(sensores_promedio);
    com.handle_interruption(true, sensores_promedio); //con foto
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
  //com.recieve_commands();
}
