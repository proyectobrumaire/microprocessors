#include "Arduino.h"
#include "HardwareSerial.h"
#include "Communication.h"
// Definición del estático
CondenserCom* CondenserCom::s_self = nullptr;

// Definición del arreglo estático
const CondenserCom::KeyCode CondenserCom::kAllKeys[CondenserCom::N_DATA] = {
  CondenserCom::T1_K, CondenserCom::T2_K, CondenserCom::T3_K, CondenserCom::T4_K,
  CondenserCom::T5_K, CondenserCom::T6_K, CondenserCom::H1_K, CondenserCom::H2_K,
  CondenserCom::E1_K, CondenserCom::E2_K, CondenserCom::P1_K, CondenserCom::P2_K,
  CondenserCom::I1_K, CondenserCom::I2_K, CondenserCom::I3_K,
  CondenserCom::W1_K
};

// Constructor: p con dirección 0xA0
CondenserCom::CondenserCom(const Pins& pi)
  : pins(pi), p(0xA0) {}

void CondenserCom::iniciar_comunicaciones(){
  s_self = this;  // registrar instancia activa
  Serial3.begin(115200);  //Comuninicación con ESP32CAM
  link.begin(Serial3); //Comuninicación con ESP32CAM

  //COMUNICACIONES
  pinMode(pins.trig, OUTPUT); // Sets the trigPin as an Output
  pinMode(pins.timer_interrupt, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(pins.timer_interrupt), timerISR_trampoline, FALLING);
  pinMode(pins.sensor_interrupt, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(pins.sensor_interrupt), sensorISR_trampoline, CHANGE);

  p.set_timer(timer_frecuency_mins); //Un minuto
  p.clear_timer_flags(true, true);

}


// ----- ISR estáticas (sin this) -----
void CondenserCom::sensorISR_trampoline() { 
  if (s_self) s_self->onSensorISR(); 
}
void CondenserCom::timerISR_trampoline()  { 
  if (s_self) s_self->onTimerISR(); 
}

// ----- Manejo real en la instancia ----- //Esta es la función en la que cae la interrupción
void CondenserCom::onSensorISR() {
  uint32_t now_isr = micros();
  
  if ((now_isr - lastSensorFlagRaisen) < cooldown){ //Cooldown después de detectada un ave
     return;
  }

  if (digitalRead(pins.sensor_interrupt) == HIGH){ //Rising edge
    startPulse = now_isr;
  } else {  //Falling edge
    duracion = now_isr - startPulse;
    if (duracion > min_sensor_duration && duracion < max_sensor_duration){ //rango de detección
      flag_sensor = true;
      lastSensorFlagRaisen = now_isr;
    } else { 
      flag_sensor = false; 
    }
  }
  
}

void CondenserCom::onTimerISR()  { 
  flag_timer  = true;
}

// -------El método que se exportar para mirar el flag (Esto es "tomar la bandera") ("Aquí ya se cambia el estado del flag")
bool CondenserCom::takeSensorFlag() { 
  bool v = flag_sensor; 
  flag_sensor = false; 
  return v; 
}
bool CondenserCom::takeTimerFlag()  { 
  bool v = flag_timer;  
  flag_timer  = false; 
  return v; 
}

void CondenserCom::clearRtcTimerFlags(){
  p.clear_timer_flags(true, true);
  p.set_timer(timer_frecuency_mins); //Un minuto
} 


void CondenserCom::sendSensorPulse(){
  //isr.println("Sending Ultrasonic Signal");
  digitalWrite(pins.trig, LOW);
  delayMicroseconds(2);
  digitalWrite(pins.trig, HIGH);
  delayMicroseconds(10);
  digitalWrite(pins.trig, LOW);
  //Aquí se manda el pulso
}

void CondenserCom::when_event(uint8_t TYPE, float values_to_send[N_DATA]) {
  //Este método solo debe habilitarse si el lock está abierto
  
  //Este método enviar los datos al ESP32 cuando se detecta un evento
  p.get_time(); //Obtener fecha y hora sobre p
  uint16_t len;
  uint8_t cmd;
  uint8_t ev;
  uint16_t ts[6] = {(uint16_t)p.year, (uint16_t)p.month, (uint16_t)p.day, (uint16_t)p.hour, (uint16_t)p.minute, (uint16_t)p.second};     // YY,MM,DD,hh,mm,ss
  
  char ts_string[20];
  snprintf(ts_string, sizeof(ts_string),
    "%02u-%02u-%02uT%02u-%02u-%02u",
    ts[0], ts[1], ts[2], ts[3], ts[4], ts[5]);
  
  Serial.println("Sending event at time: ");
  Serial.println(ts_string);
    
  //0) SI E UN PÁJARO TOMA LA FOTO DE UNA
  if (TYPE == BIRD){
    Serial.print("Sending Take photo command at: ");
    Serial.println(ts_string);
    cmd = CMD_TAKE_PHOTO;
    len = 0;
    len = link.txObj(ts,  len, sizeof(ts));
    len = link.txObj(cmd, len);
    link.sendData(len); //Enviar el evento
    if (!wait_for_ack(cmd)) {
      Serial.println("Exiting comand [take photo] since bad or no response");
      Serial.println("Nah...Sending shit anyway");
      //return;
    }
    
  }

  // 1) INFORMA DEL EVENTO
  cmd = CMD_SAVE_EVENT;
  ev  = TYPE;
  len = 0;
  len = link.txObj(ts,  len, sizeof(ts));
  len = link.txObj(cmd, len);
  len = link.txObj(ev,  len);
  link.sendData(len); //Enviar el evento
  if (!wait_for_ack(cmd)) {
    Serial.println("Exiting comand [save event] since bad or no response");
    Serial.println("Nah...Sending shit anyway");
    //return;
  }
  

  // 2) ENVIAR DATOS DE LOS SENSORES
  cmd = CMD_SAVE_DATA;
  for (size_t i=0; i<N_DATA; i++) {
    len = 0;
    len = link.txObj(ts,  len, sizeof(ts));
    len = link.txObj(cmd, len);
    len = link.txObj(kAllKeys[i],  len);
    len = link.txObj(values_to_send[i],  len);
    link.sendData(len); //Enviar el evento
    if (!wait_for_ack(cmd)) {
      Serial.println("Exiting comand [save data] since bad or no response");
      //return;
      Serial.println("Nah...Sending shit anyway");
    }

  }
}


bool CondenserCom::wait_for_ack(uint8_t expected_cmd){
  uint32_t start_wait = millis();
  uint32_t timeout = 10000;
  Serial.println("Waiting...");
  while ((millis() - start_wait) < timeout){
    if(link.available()) {
      uint16_t idx = 0;
      uint16_t ts[6]; //Timestamp
      uint8_t cmd ; //Qué comando llegó
      uint8_t status ; //Qué tal llegó el comando

      //Primero lee el timestamp
      idx = link.rxObj(ts, idx,  6 * sizeof(uint16_t));
      char ts_string[20];
      snprintf(ts_string, sizeof(ts_string),
          "%02u-%02u-%02uT%02u-%02u-%02u",
          ts[0], ts[1], ts[2], ts[3], ts[4], ts[5]);
      
      idx = link.rxObj(cmd, idx); //recibir el comando al que le hacen el ack
      idx = link.rxObj(status, idx); //status // Status (1 = OK, 0 = ERROR)
      if (cmd == expected_cmd){ 
        if (status){
          Serial.print("ACK recibido at time...");
          Serial.println(ts_string);
          return true;
        } else{
          Serial.println("Comando respondido - No ejecutado");
          return false;
        }
      } else {
        Serial.print("ACK de otro comando...");
        return false;
      }
    }
    delay(1);
  }
  Serial.println("Timeout for ack reached");
  return false;
}

void CondenserCom::handle_interruption(bool take_photo, float values[N_DATA]){
  if (take_photo){
    when_event(BIRD, values);
  }
  else{
    when_event(PERIODIC,values);
  }
}

void CondenserCom::report_boot(float values[N_DATA]){
  when_event(BOOT, values);
}

void CondenserCom::recieve_commands(){
  if(link.available()){
    uint16_t index = 0;
    uint8_t ts[6]; //Timestamp
    uint8_t cmd; //Qué comando llegó
    String line_to_sd = "";

    //Primero lee el timestamp
    index = link.rxObj(ts, index);

    //Ahora lee el comando
    index = link.rxObj(cmd, index);
    switch (cmd){
       //tomar la foto perro
      case CMD_HELLO:{

        uint8_t mode; //En qué modo está el Hello
        index = link.rxObj(mode, index);
        switch (mode){
          case SET_TIME:
            p.year = ts[0];
            p.month = ts[1];
            p.day = ts[2];
            p.hour = ts[3];
            p.minute = ts[5];
            p.second = ts[5];
            p.set_time();

            
            char ts_string[20];
            snprintf(ts_string, sizeof(ts_string),
              "%02u-%02u-%02uT%02u-%02u-%02u",
              ts[0], ts[1], ts[2], ts[3], ts[4], ts[5]);
            Serial.println("Sending event at time: ");
            Serial.print(ts_string);
            break;

          case STATUS:
            //TODO
            break;
          default: 
            Serial.println("Wrong Hello Payload");
            break;
      
        }

      }
      // Código si no coincide con ningún "case
      default:
        Serial.println("Wrong Command");
        break;
    }
  }
}