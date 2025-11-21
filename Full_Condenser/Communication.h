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

  static const size_t N_DATA = 16;

  //Códigos de la sensores y variables a guardar:
  enum KeyCode : uint8_t {
    T1_K = 0x20,  // Temp ambiente
    T2_K = 0x21,  // Temp caja interna
    T3_K = 0x22,  // Placa fría 1
    T4_K = 0x23,  // Placa fría 2
    T5_K = 0x24,  // Temp media fríauint8_t
    T6_K = 0x25,  // Temp objetivo
    H1_K = 0x26,  // Humedad externa
    H2_K = 0x27,  // Humedad interna
    E1_K = 0x28,  // Error
    E2_K = 0x29,  // Error acumulado
    P1_K = 0x2A,  // Punto de rocío
    P2_K = 0x2B,  // PWM aplicado
    I1_K = 0x2C,  //Corriente uno
    I2_K = 0x2D,  //Corriente dos
    I3_K = 0x2E,  //Corriente tres
    W1_K = 0x2F   //Peso agua
  };

  //Códigos de la trama:
  enum Command : uint8_t {
    CMD_TAKE_PHOTO = 0x01,
    CMD_SAVE_EVENT = 0x02,
    CMD_SAVE_DATA = 0x03,
    CMD_HELLO = 0x04,
    CMD_LOCKARDUINO   = 0x05,
    CMD_UNLOCKARDUINO = 0x06
  };



  // Códigos de los eventos
  enum EventCode : uint8_t {
    BOOT = 0x80,
    BIRD = 0x81,
    PERIODIC = 0x82
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

  uint32_t duracion;
  volatile uint32_t lastSensorFlagRaisen = 0;


private:
  Pins pins;

  const uint32_t min_sensor_duration = 50; // 0 cm
  const uint32_t max_sensor_duration = 2500; // 40 cm

  volatile bool esp_busy = false; //Flag por si el esp32 entra en estado de espera
  uint32_t esp_busy_since = 0; //para el timeout
  uint32_t ESP_BUSY_TIMEOUT = 60000; //1 minuto de busy como max

  

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

  void when_event(uint8_t TYPE, float values_to_send[N_DATA]);

  bool wait_for_ack(uint8_t expected_cmd);
};
