#include "CondenserControl.h"
#include <math.h>

//constructor
CondenserControl::CondenserControl(const CondenserControl::Pins& p)
: pins(p),
  dht1(p.dht_pin1, DHT22),
  dht2(p.dht_pin2, DHT22),
  tc1(p.max_clk1, p.max_cs1, p.max_d01),
  tc2(p.max_clk2, p.max_cs2, p.max_d02),
  peltier_pwm_max(max_peltier_op*2.55),
  peltier_temp_amb_max(max_peltier_op*2.55*peltier_delta_T)
  {
} //Aqupi solamente se llama al constructor para poblar p (que son los pines)

void CondenserControl::iniciar_control(){
  
  if (!tc1.begin()) { Serial.println("ERROR termocupla 1."); while (1) delay(10); }
  if (!tc2.begin()) { Serial.println("ERROR termocupla 2."); while (1) delay(10); }
  dht1.begin();
  dht2.begin();
  balanzaInicial(); //Inicia la balanza

  // Pines Ventiladores
  pinMode(pins.ena, OUTPUT);
  pinMode(pins.in1, OUTPUT);
  pinMode(pins.in2, OUTPUT);
  pinMode(pins.enb, OUTPUT);
  pinMode(pins.in3, OUTPUT);
  pinMode(pins.in4, OUTPUT);

  // Pines IBT2
  pinMode(pins.rpwm, OUTPUT);
  pinMode(pins.lpwm, OUTPUT);
  pinMode(pins.ren , OUTPUT);
  pinMode(pins.len , OUTPUT);

    //pines servomotres
  pinMode(pins.m1, OUTPUT);
  pinMode(pins.m2, OUTPUT);
  pinMode(pins.mv, OUTPUT);
  seguro.attach(pins.m1);
  valvula.attach(pins.mv);


  //Configurar ventiladores  
  digitalWrite(pins.in1, HIGH);
  digitalWrite(pins.in2, LOW);
  digitalWrite(pins.in3, HIGH);
  digitalWrite(pins.in4, LOW);

  //Configurar el controlador para las celdas Peliter
  digitalWrite(pins.ren, HIGH);
  digitalWrite(pins.len, HIGH);
  analogWrite (pins.rpwm, 0 );
  analogWrite (pins.lpwm, 0);
  peltier_on = false;

  // Sensor lluvia
  pinMode(pins.rain_digital, INPUT);

  //Prender los ventiladores
  PrenderVentiladorPrincipal();
  PrenderVentiladorChimenea();
  is_ventilador_chimenea_on = true;
  t_ventilador_ultimo_on = millis();

  t_ctrl_prev = millis();
  //Reset posiciones de los servos
  delay(1000);
  reset_plato_pos();
}

void CondenserControl::leer_sensores_y_controlar(){
  c1 = tc1.readCelsius();
  c2 = tc2.readCelsius();
  //Punto medio entre placas frias
  c12 = (c1 + c2)/2.0;

  //Sensor Humedad y temperatura interno
  dht1.temperature().getEvent(&event);
  tempAmbiente1 = event.temperature;
  dht1.humidity().getEvent(&event);
  humedad1 = event.relative_humidity;

  //Sensor Humedad y temperatura externo
  dht2.temperature().getEvent(&event);
  tempAmbiente2 = event.temperature;
  dht2.humidity().getEvent(&event);
  humedad2 = event.relative_humidity;

  if (!isnan(tempAmbiente2) && !isnan(humedad2) && !isnan(c12)) {
    puntoRocio = calcularPuntoRocio(tempAmbiente2, humedad2);
    //Condición de vabilidad
    if ((tempAmbiente2 >= puntoRocio - peltier_temp_amb_max)){
      pwm = 0;
      analogWrite(pins.rpwm, (int)pwm);
      peltier_on = false;
      Serial.println("No se puede condensar debido a las condiciones ambientales");
    } else {
      error = puntoRocio - c12;
      unsigned long t_ctrl_actual = millis();
      float dt = (t_ctrl_actual - t_ctrl_prev)/1000.0;  // en segundos (con decimal)
      t_ctrl_prev = t_ctrl_actual;

      // Evitar acumulación excesiva (anti-windup)
      errorAcumulado += error * dt;
      errorAcumulado = constrain(errorAcumulado, -maxIntegracion, maxIntegracion);

      salidaPI = kp * error + ki * errorAcumulado;
      tempObjetivo = salidaPI + c12;

      pwm = (tempObjetivo - tempAmbiente2) / peltier_delta_T;
      pwm = constrain(pwm, 0, peltier_pwm_max);
      analogWrite(pins.rpwm, (int)pwm); //Ejecutar el control
      peltier_on = true;
    }

  } else {
    Serial.println("No es posible ejecutar el control ....Encendiendo celda");
    //peltier_on = false;
    peltier_on = true;
    pwm = 255;
    analogWrite(pins.rpwm,(int)pwm); //Ejecutar el control
  }

  unsigned long t_now = millis();
  if (is_ventilador_chimenea_on) {
    if (t_now - t_ventilador_ultimo_on >= ventilador_chimenea_on_T) {
      ApagarVentiladorChimenea();
      is_ventilador_chimenea_on = false;
      t_ventilador_ultimo_on = t_now;
    }
  } else {
    if (t_now - t_ventilador_ultimo_on >= ventilador_chimenea_off_T) {
      PrenderVentiladorChimenea();
      is_ventilador_chimenea_on = true;
      t_ventilador_ultimo_on = t_now;
    }
  }



  //Aquí se debe modificar peso_agua. Evitar método bloqueante
  if (is_balanza) {
    peso_agua = balanza.get_units(5);
  }

  float suma = 0;
  for (int i = 0; i < 10; i++) {
    suma += analogRead(pins.otrosensor);
    delay(2);
  }
  float voltaje = (suma / 10.0) * (5.0 / 1023.0);
  voltajeCorrienteFiltrada = (voltaje - 2.5) / 0.100;  // Sensibilidad para 20A

  //Esto no se puede interumpir...
  noInterrupts();
  //acumular...
  T1_sum += tempAmbiente2;
  T2_sum += tempAmbiente1;
  T3_sum += c1;
  T4_sum += c2;
  T5_sum += c12;
  H1_sum += humedad2;    
  H2_sum += humedad1;
  P1_sum += puntoRocio;
  P2_sum += pwm;
  I4_sum += voltajeCorrienteFiltrada;
  W1_sum += peso_agua;
  num_samples++;
  interrupts();

  Serial.print("Temp Ambiente: "); Serial.println(tempAmbiente2);
  Serial.print("Humedad: "); Serial.println(humedad2);
  Serial.print("Temp Caja Interna: "); Serial.println(tempAmbiente1);
  Serial.print("Humedad Interna: "); Serial.println(humedad1);
  Serial.print("Temp Placa Fria 1: "); Serial.println(c1);
  Serial.print("Temp Placa Fria 2: "); Serial.println(c2);
  Serial.print("Temp Media Fria: "); Serial.println(c12);
  Serial.print("Punto Rocío: "); Serial.println(puntoRocio);
  Serial.print("Error: "); Serial.println(error);
  Serial.print("Error Acumulado: "); Serial.println(errorAcumulado);
  Serial.print("Temp Objetivo: "); Serial.println(tempObjetivo);
  Serial.print("PWM aplicado: "); Serial.println(pwm);
  Serial.print("Correinte 4: "); Serial.println(voltajeCorrienteFiltrada);
  Serial.print("Peso : "); Serial.println(peso_agua);
  Serial.println("-----------");
}


void CondenserControl::reset_acumuladores() {
  T1_sum = T2_sum = T3_sum = T4_sum = T5_sum = 0.0f;
  H1_sum = H2_sum = P1_sum = P2_sum = 0.0f;
  I4_sum = 0.0f;
  W1_sum = 0.0f;
  num_samples = 0;
}


void CondenserControl::promediar(float out[N_DATA_CRL]) {
  //devuelve en el parámetro (out) el resultado de haber promediado los acumuladores en la ventana
  out[0]  = safe_avg(T1_sum, num_samples);
  out[1]  = safe_avg(T2_sum, num_samples);
  out[2]  = safe_avg(T3_sum, num_samples);
  out[3]  = safe_avg(T4_sum, num_samples);
  out[4]  = safe_avg(T5_sum, num_samples);
  out[5]  = safe_avg(H1_sum, num_samples);
  out[6]  = safe_avg(H2_sum, num_samples);
  out[7] = safe_avg(P1_sum, num_samples);
  out[8] = safe_avg(P2_sum, num_samples);
  out[9] = safe_avg(I4_sum, num_samples);
  out[10] = safe_avg(W1_sum, num_samples);
  reset_acumuladores();  // ← limpia promedios para la siguiente ventana
}



void CondenserControl::PrenderVentiladorPrincipal(){
  analogWrite(pins.enb, 255);
}

void CondenserControl::ApagarVentiladorPrincipal(){
  analogWrite(pins.enb, 0);
}
void CondenserControl::PrenderVentiladorChimenea(){
  analogWrite(pins.ena, 255);
}

void CondenserControl::ApagarVentiladorChimenea(){
  analogWrite(pins.ena, 0);
}

void CondenserControl::balanzaInicial() {
  Serial.println("Iniciando Balanza...");
  Serial.println("Iniciando Balanza...");
  balanza.begin(pins.dout, pins.clk);

  if (!balanza.wait_ready_timeout(1000)){
    is_balanza = false;
    Serial.println("Balanza no encontrada");
    return;
  }

  is_balanza = true;
  byte flag = EEPROM.read(EEPROM_FLAG_ADDR);

  if (flag != CALIB_OK || borrar_datos_eeprom) {
    balanza.set_scale(SCALE_DEFAULT);
    balanza.tare(20);
    long suma = 0;
    for (int i = 0; i < 5; i++) {
      long lectura = balanza.read();   // lectura RAW directa
      suma += lectura;
      delay(50);
    }
    long offset_promedio = suma/5;
    balanza.set_offset(offset_promedio);
    guardarCalibracion(offset_promedio, SCALE_DEFAULT);
    

    Serial.println("Tara inicial completada y almacenada.");
    Serial.print("Offset promedio guardado: ");
    Serial.println(offset_promedio);
    Serial.print("Escala guardada: ");
    Serial.println(SCALE_DEFAULT, 3);
    Serial.println();

  } else {
    long offset;
    float escala;
    cargarCalibracion(offset, escala);
    balanza.set_scale(escala);
    balanza.set_offset(offset);

    Serial.println("Calibracion cargada desde EEPROM:");
    Serial.print("Offset: ");
    Serial.println(offset);
    Serial.print("Escala: ");
    Serial.println(escala, 3);
    Serial.println("Listo para medir");
  }    
}

void CondenserControl::ejecutar_volcado() {
  Serial.println("Iniciando secuencia de volcado");

  // Apagar actuadores antes del movimiento mecánico
  ApagarVentiladorPrincipal();
  ApagarVentiladorChimenea();
  analogWrite(pins.rpwm, 0);
  peltier_on = false;
  delay(2000);

  volcar_plato_y_renovar();

  // Volver a operación normal
  PrenderVentiladorPrincipal();
  PrenderVentiladorChimenea();
  errorAcumulado = 0.0;  // evitar windup acumulado durante la pausa
  Serial.println("Volcado completado — reanudando control");
}

void CondenserControl::volcar_plato_y_renovar() {
  Serial.println("Volcando el plato del bebedero");
  
  seguro.write(0);
  delay(3000);
  
  volcado.attach(pins.m2);
  volcado.write(90);
  delay(10000);
  volcado.write(0);
  delay(3000);
  volcado.detach();
  
  valvula.write(90);
  delay(3000);
  valvula.write(0);
  seguro.write(90);
}



void CondenserControl::reset_plato_pos() {
  Serial.println("Colocando el plato del bebedero");
  seguro.write(0);
  volcado.attach(pins.m2);
  volcado.write(0);
  delay(2000);
  volcado.detach();
  seguro.write(90);
  valvula.write(0);
}


void CondenserControl::updateRain() {
  int val = analogRead(pins.rain_analog);

  RainState prev = rainState;
  switch (rainState) {
    case RAIN_DRY:
      if (val < RAIN_DRY_THRESHOLD)  rainState = RAIN_RAINING;
      break;
    case RAIN_RAINING:
      if      (val < RAIN_SOAK_THRESHOLD) rainState = RAIN_SOAKED;
      else if (val >= RAIN_DRY_THRESHOLD) rainState = RAIN_DRY;
      break;
    case RAIN_SOAKED:
      if      (val >= RAIN_DRY_THRESHOLD)  rainState = RAIN_DRY;
      else if (val >= RAIN_SOAK_THRESHOLD) rainState = RAIN_RAINING;
      break;
  }
  (void)prev;
}

//Helpers
//Clacula el punto de rocío
float CondenserControl::calcularPuntoRocio(float temperaturaC, float humedadRelativa) {
  float alpha = log(humedadRelativa / 100.0) + (17.27 * temperaturaC) / (237.3 + temperaturaC);
  float puntoRocio = ((237.3 * alpha) / (17.27 - alpha)-5); // ajuste -5 ºC
  return puntoRocio;
}

//Salvar si la división es entre 0
float CondenserControl::safe_avg(float  sum, int n)  { return (n > 0) ? (sum / static_cast<float>(n))  : NAN; }

//Cargar calibración desde la EEPROM
void CondenserControl::cargarCalibracion(long &offset, float &escala) {
  EEPROM.get(EEPROM_OFFSET_ADDR, offset);
  EEPROM.get(EEPROM_SCALE_ADDR,  escala);
}

//Guadar Calibración
void CondenserControl::guardarCalibracion(long offset, float escala) {
  EEPROM.put(EEPROM_OFFSET_ADDR, offset);
  EEPROM.put(EEPROM_SCALE_ADDR,  escala);
  EEPROM.write(EEPROM_FLAG_ADDR,  CALIB_OK);
}