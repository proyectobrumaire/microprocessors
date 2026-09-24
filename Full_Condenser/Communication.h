#include <Wire.h>
#include <stdio.h>
#include <SPI.h>
#include <math.h>
#include <PCF8583.h>
#include <SerialTransfer.h>
#include <Arduino.h>




class CondenserCom {
public:
  struct Pins {
    //donde se captan las interrupciones
    byte sensor_interrupt, timer_interrupt, trig;
  };

  static const size_t N_DATA = 11;

  //Códigos de la sensores y variables a guardar:
  enum KeyCode : uint8_t {
    T1_K = 0x20,  // Temp ambiente
    T2_K = 0x21,  // Temp caja interna
    T3_K = 0x22,  // Placa fría 1
    T4_K = 0x23,  // Placa fría 2
    T5_K = 0x24,  // Temp media fría
    H1_K = 0x26,  // Humedad externa
    H2_K = 0x27,  // Humedad interna
    P1_K = 0x2A,  // Punto de rocío
    P2_K = 0x2B,  // PWM aplicado
    I4_K = 0x2F,  //Corriente peltier
    W1_K = 0x30   //Peso agua
  };

  //Códigos de la trama:
  enum Command : uint8_t {
    CMD_TAKE_PHOTO = 0x01,
    CMD_SAVE_EVENT = 0x02,
    CMD_SAVE_DATA = 0x03,
    CMD_HELLO = 0x04
  };



  // Códigos de los eventos
  enum EventCode : uint8_t {
    BOOT = 0x80,
    BIRD = 0x81,
    PERIODIC = 0x82,
    PELTIER_ON = 0x83,
    PELTIER_OFF = 0x84,
    VOLCADO = 0x85
  };


  //Mensajes Hello
  enum HelloModes : uint8_t {
    SET_TIME = 0xA0,
    STATUS = 0xA1
  };
  static const KeyCode kAllKeys[N_DATA];

  explicit CondenserCom(const Pins& pi);

  void iniciar_comunicaciones();

  bool takeSensorFlag();
  bool takeTimerFlag();
  void report_boot(float values[N_DATA]);
  void handle_interruption(bool take_photo, float values[N_DATA]);
  void clearRtcTimerFlags();
  void recieve_commands();
  void sendSensorPulse();
  void safety_lock_timeout();
  void when_event(uint8_t TYPE, float values_to_send[N_DATA]);
  uint32_t get_rtc_hours();


  uint32_t duracion;
  volatile uint32_t lastSensorFlagRaisen = 0;


private:
  Pins pins;

  const uint32_t min_sensor_duration = 50; // 0 cm
  const uint32_t max_sensor_duration = 2500; // 40 cm

  volatile bool flag_sensor = false;
  volatile bool flag_timer = false;

  volatile uint32_t startPulse = 0;
  
  uint32_t cooldown = 6000000;

  // ----- Soporte ISR en clase -----
  static CondenserCom* s_self;         // instancia activa (singleton)
  static void sensorISR_trampoline();  // ISR estáticas compatibles con attachInterrupt
  static void timerISR_trampoline();

  // Lado instancia
  void onSensorISR();
  void onTimerISR();

  SerialTransfer link;  //Instancia paratransferir datos vía serial
  PCF8583 p;            //instancia de PFC8583

  int correct_address = 0;
  int timer_frecuency_mins = 5;

  bool wait_for_ack(uint8_t expected_cmd);
};
