#include "CondenserControl.h"
#include <math.h>

//constructor
CondenserControl::CondenserControl(const CondenserControl::Pins& p)
: pins(p),
  dht1(p.dht_pin1, p.dht_type),
  dht2(p.dht_pin2, p.dht_type),
  tc1(p.max_clk1, p.max_cs1, p.max_d01),
  tc2(p.max_clk2, p.max_cs2, p.max_d02) {
} //Aqupi solamente se llama al constructor para poblar p (que son los pines)

void CondenserControl::iniciar_control(){
  
  if (!tc1.begin()) { Serial.println("ERROR termocupla 1."); while (1) delay(10); }
  if (!tc2.begin()) { Serial.println("ERROR termocupla 2."); while (1) delay(10); }
  dht1.begin();
  dht2.begin();
  balanza.begin(pins.dout, pins.clk);
  //Balanza
  byte flag = EEPROM.read(EEPROM_FLAG_ADDR);

  if (flag != CALIB_OK || borrar_datos_eeprom) {
    autoTareInicial(); //Se hace el tare ahí mismo
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

  //L298N
  PrenderMotor();
  Serial.println("Vibración activada");
  motorEncendido = true;

  // Ventiladores
  pinMode(pins.ena, OUTPUT);
  pinMode(pins.in1, OUTPUT);
  pinMode(pins.in2, OUTPUT);
  pinMode(pins.enb, OUTPUT);
  pinMode(pins.in3, OUTPUT);
  pinMode(pins.in4, OUTPUT);

  digitalWrite(pins.in1, LOW);
  digitalWrite(pins.in2, LOW);
  analogWrite (pins.ena, 0);
  digitalWrite(pins.in3, LOW);
  digitalWrite(pins.in4, LOW);
  analogWrite (pins.enb, 0);

  // Pines IBT2
  pinMode(pins.rpwm, OUTPUT);
  pinMode(pins.lpwm, OUTPUT);
  pinMode(pins.ren , OUTPUT);
  pinMode(pins.len , OUTPUT);

  digitalWrite(pins.ren, LOW);
  digitalWrite(pins.len, LOW);
  analogWrite (pins.rpwm, 0 );
  analogWrite (pins.lpwm, 0 );
  
  t_ctrl_prev = millis();
  delay(1000);

  //Configurar el controlador para las celdas
  digitalWrite(pins.ren, HIGH);
  digitalWrite(pins.len, HIGH);
  analogWrite(pins.lpwm, 0);
  //Encender ventiladores de refrigeracion a la maxima potencia
  digitalWrite(pins.in1, HIGH);
  digitalWrite(pins.in2, LOW);
  analogWrite (pins.ena, 255); 
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
    error = puntoRocio - c12;
    unsigned long t_ctrl_actual = millis();
    float dt = (t_ctrl_actual - t_ctrl_prev)/1000.0;  // en segundos (con decimal)
    t_ctrl_prev = t_ctrl_actual;

    // Evitar acumulación excesiva (anti-windup)
    errorAcumulado += error * dt;
    errorAcumulado = constrain(errorAcumulado, -maxIntegracion, maxIntegracion);

    salidaPI = kp * error + ki * errorAcumulado;
    tempObjetivo = salidaPI + c12;

    pwm = (tempObjetivo - 28.0) / -0.0745;
    pwm = constrain(pwm, 0, 255);
    analogWrite(pins.rpwm, (int)pwm); //Ejecutar el control
    }

    //Lecturas del sensor de corriente
    float averageRawReading1 = (float)analogRead(pins.cSP1);
    float voltage1 = averageRawReading1 * (5.0 / 1023.0);
    float Vshunt1 = voltage1 / opAmpGain1;
    float currentAmps1 = Vshunt1 / Rshunt1;
    current_mA1 = currentAmps1 * 1000.0;


    float averageRawReading2 = (float)analogRead(pins.cSP2);;
    float voltage2 = averageRawReading2 * (5.0 / 1023.0);
    float Vshunt2 = voltage2 / opAmpGain2;
    float currentAmps2 = Vshunt2 / Rshunt2;
    current_mA2 = currentAmps2 * 1000.0;

    float averageRawReading3 = (float)analogRead(pins.cSP3);
    float voltage3 = averageRawReading3 * (5.0 / 1023.0);
    float Vshunt3 = voltage3 / opAmpGain3;
    float currentAmps3 = Vshunt3 / Rshunt3;
    current_mA3 = currentAmps3 * 1000.0;

    if (current_mA1 < MIN_CURRENT_THRESHOLD_mA) {current_mA1 = 0.0f;}
    if (current_mA2 < MIN_CURRENT_THRESHOLD_mA) {current_mA2 = 0.0f;}
    if (current_mA3 < MIN_CURRENT_THRESHOLD_mA) {current_mA3 = 0.0f;}

    //Aquí se debe modificar peso_agua
    if (balanza.wait_ready_timeout(1000)) {
      peso_agua = balanza.get_units(20);
    }


    //Esto no se puede interumpir...
    noInterrupts();
    //acumular...
    T1_sum += tempAmbiente2;
    T2_sum += tempAmbiente1;
    T3_sum += c1;
    T4_sum += c2;
    T5_sum += c12;
    T6_sum += tempObjetivo;
    H1_sum += humedad2;    
    H2_sum += humedad1;
    E1_sum += error;
    E2_sum += errorAcumulado;
    P1_sum += puntoRocio;
    P2_sum += pwm;
    I1_sum += current_mA1;
    I2_sum += current_mA2;
    I3_sum += current_mA3;
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
    Serial.print("Correinte 1: "); Serial.println(current_mA1);
    Serial.print("Correinte 2: "); Serial.println(current_mA2);
    Serial.print("Correinte 3: "); Serial.println(current_mA3);
    Serial.print("Peso : "); Serial.println(peso_agua);
    Serial.println("-----------");
}


void CondenserControl::reset_acumuladores() {
  T1_sum = T2_sum = T3_sum = T4_sum = T5_sum = T6_sum = 0.0f;
  H1_sum = H2_sum = E1_sum = E2_sum = P1_sum = P2_sum = 0.0f;
  I1_sum = I2_sum = I3_sum = 0.0f;
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
  out[5]  = safe_avg(T6_sum, num_samples);
  out[6]  = safe_avg(H1_sum, num_samples);
  out[7]  = safe_avg(H2_sum, num_samples);
  out[8]  = safe_avg(E1_sum, num_samples);
  out[9]  = safe_avg(E2_sum, num_samples);
  out[10] = safe_avg(P1_sum, num_samples);
  out[11] = safe_avg(P2_sum, num_samples);
  out[12] = safe_avg(I1_sum, num_samples);
  out[13] = safe_avg(I2_sum, num_samples);
  out[14] = safe_avg(I3_sum, num_samples);
  out[15] = safe_avg(W1_sum, num_samples);
  reset_acumuladores();  // ← limpia promedios para la siguiente ventana
}

void CondenserControl::PrenderMotor(){
    digitalWrite(pins.in4, HIGH);
    digitalWrite(pins.in3, LOW);
    analogWrite(pins.enb, 255);
}

void CondenserControl::ApagarMotor(){
    digitalWrite(pins.in4, HIGH);
    digitalWrite(pins.in3, LOW);
    analogWrite(pins.enb, 0);
}

void CondenserControl::autoTareInicial() {
  Serial.println("Realizando tare inicial...");

  balanza.set_scale(SCALE_DEFAULT);
  if (balanza.wait_ready_timeout(1000)){
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
    Serial.println("Balanza no encontrada");
  }
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