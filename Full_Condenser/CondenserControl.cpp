#include "CondenserControl.h"
#include "Debug.h"
#include <math.h>

//constructor
CondenserControl::CondenserControl(const CondenserControl::Pins& p)
: pins(p),
  dht1(p.dht_pin1, DHT22),
  dht2(p.dht_pin2, DHT22),
  tc1(p.max_clk1, p.max_cs1, p.max_d01),
  tc2(p.max_clk2, p.max_cs2, p.max_d02)
  {
} //Aqupi solamente se llama al constructor para poblar p (que son los pines)

void CondenserControl::iniciar_control(){
  
  if (!tc1.begin()) { DBGLN("ERROR termocupla 1."); while (1) delay(10); }
  if (!tc2.begin()) { DBGLN("ERROR termocupla 2."); while (1) delay(10); }
  // Solo la termocupla abierta cuenta como falla. Las puntas tocan la placa y el MAX31855
  // marca "corto a GND" de forma intermitente (ciclo externo de ~29 s), pero la lectura es válida.
  tc1.setFaultChecks(MAX31855_FAULT_OPEN);
  tc2.setFaultChecks(MAX31855_FAULT_OPEN);
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
  seguro.write(SEGURO_TRABADO);  //antes de attach: por defecto Servo arranca en 90° (suelto)
  seguro.attach(pins.m1);
  valvula.write(VALVULA_CERRADA);  //antes de attach: por defecto Servo arranca en 90°
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

  //Ventiladores apagados: se encienden con la Peltier (actualizar_ventiladores)
  ApagarVentiladorPrincipal();
  ApagarVentiladorChimenea();
  ventiladores_on = false;
  is_ventilador_chimenea_on = false;
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

  //Balanza (antes del control: el corte por tanque lleno depende de ella).
  // Lectura sin bloqueo: HX711::read() espera indefinidamente si el chip no está listo
  // (p.ej. balanza desconectada). Si no hay dato nuevo se conserva el último peso.
  // Las lecturas absurdas (< BALANZA_MIN_VALIDA_G) se ignoran y no cuentan como dato.
  if (is_balanza && balanza.is_ready()) {
    float lectura = balanza.get_units(1);
    if (lectura >= BALANZA_MIN_VALIDA_G) {
      peso_agua = lectura;
      t_balanza_ultimo_dato = millis();
    }
  }
  //Corte por tanque lleno, con histéresis. Con la balanza en error no se corta (se condensa).
  if (balanza_con_error()) {
    tanqueLleno = false;
  } else if (peso_agua > TANQUE_LLENO_G) {
    tanqueLleno = true;
  } else if (peso_agua < TANQUE_REANUDA_G) {
    tanqueLleno = false;
  }

  //dt se actualiza en todas las ramas: si el PI estuvo inactivo (no viable, apagado, sensor en NaN
  //o volcado) no debe integrar de golpe todo ese tiempo. Se limita a dt_max_ctrl como protección.
  unsigned long t_ctrl_actual = millis();
  float dt = (t_ctrl_actual - t_ctrl_prev)/1000.0;  // en segundos (con decimal)
  t_ctrl_prev = t_ctrl_actual;
  if (dt > dt_max_ctrl) dt = dt_max_ctrl;

  bool sensores_ok = !isnan(tempAmbiente2) && !isnan(humedad2) && !isnan(c12);
  if (sensores_ok) {
    puntoRocio = calcularPuntoRocio(tempAmbiente2, humedad2);            //rocío real
    objetivoPlaca = max(puntoRocio - MARGEN_BAJO_ROCIO, TEMP_PLACA_MIN); //límite contra la escarcha
    //Condición de viabilidad, calculada con el PWM máximo del modo actual (con histéresis para
    //no oscilar en el umbral): la Peltier no puede llevar la placa al objetivo si el ambiente
    //está demasiado por encima. Apagada por horario no se evalúa.
    if (modo != PELTIER_APAGADA) {
      float umbral_viabilidad = objetivoPlaca - pwm_max * peltier_delta_T;
      if (!es_viable && !viabilidad_sin_histeresis) umbral_viabilidad -= viabilidad_histeresis;
      es_viable = tempAmbiente2 < umbral_viabilidad;
      viabilidad_sin_histeresis = false;
    }
  }

  bool encender;
  if (modo == PELTIER_APAGADA) {
    pwm = 0;
    encender = false;
  } else if (tanqueLleno) {
    pwm = 0;
    encender = false;
    DBGLN("Tanque lleno: no se condensa");
  } else if (!sensores_ok) {
    //Decisión consciente: con un sensor del control en NaN la celda va al máximo del modo
    DBGLN("No es posible ejecutar el control ....Encendiendo celda");
    pwm = pwm_max;
    encender = true;
  } else if (!es_viable) {
    pwm = 0;
    encender = false;
    DBGLN("No se puede condensar debido a las condiciones ambientales");
  } else if (modo == PELTIER_PWM_FIJO) {
    pwm = pwm_max;
    encender = true;
  } else {  //PELTIER_PI
    error = objetivoPlaca - c12;

    // Evitar acumulación excesiva (anti-windup)
    errorAcumulado += error * dt;
    errorAcumulado = constrain(errorAcumulado, -maxIntegracion, maxIntegracion);

    salidaPI = kp * error + ki * errorAcumulado;
    tempObjetivo = salidaPI + c12;

    pwm = (tempObjetivo - tempAmbiente2) / peltier_delta_T;
    pwm = constrain(pwm, 0, pwm_max);
    encender = true;
  }
  analogWrite(pins.rpwm, (int)pwm); //Ejecutar el control
  peltier_on = encender;

  actualizar_ventiladores();

  float suma = 0;
  for (int i = 0; i < 10; i++) {
    suma += analogRead(pins.otrosensor);
    delay(2);
  }
  float voltaje = (suma / 10.0) * (5.0 / 1023.0);
  voltajeCorrienteFiltrada = (voltaje - 2.5) / 0.100;  // Sensibilidad para 20A

  lluviaCruda = analogRead(pins.lluvia);
  if (lluviaCruda < UMBRAL_AGUA_PRESENTE) aguaPresente = true;
  else if (lluviaCruda > UMBRAL_AGUA_AUSENTE) aguaPresente = false;  //entre umbrales: mantiene el estado

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

  DBG("Temp Ambiente: "); DBGLN(tempAmbiente2);
  DBG("Humedad: "); DBGLN(humedad2);
  DBG("Temp Caja Interna: "); DBGLN(tempAmbiente1);
  DBG("Humedad Interna: "); DBGLN(humedad1);
  DBG("Temp Placa Fria 1: "); DBGLN(c1);
  DBG("Temp Placa Fria 2: "); DBGLN(c2);
  DBG("Temp Media Fria: "); DBGLN(c12);
  DBG("Punto Rocío (real): "); DBGLN(puntoRocio);
  DBG("Objetivo placa: "); DBGLN(objetivoPlaca);
  DBG("Error: "); DBGLN(error);
  DBG("Error Acumulado: "); DBGLN(errorAcumulado);
  DBG("Temp Objetivo: "); DBGLN(tempObjetivo);
  DBG("PWM aplicado: "); DBGLN(pwm);
  DBG("Correinte 4: "); DBGLN(voltajeCorrienteFiltrada);
  DBG("Peso : "); DBG(peso_agua); DBG(balanza_con_error() ? " (balanza con ERROR)" : "");
  DBGLN(tanqueLleno ? " | tanque LLENO" : "");
  DBG("Modo Peltier: "); DBG(modo == PELTIER_APAGADA ? "apagada" : (modo == PELTIER_PWM_FIJO ? "PWM fijo" : "PI"));
  DBG(" (max "); DBG(pwm_max); DBG(") viable="); DBG(es_viable); DBG(" on="); DBGLN(peltier_on);
  DBG("Lluvia: "); DBG(aguaPresente ? "SI" : "NO"); DBG(" (A2="); DBG(lluviaCruda); DBGLN(")");
  DBGLN("-----------");
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
  //Sin muestras nuevas se conserva el último promedio válido (evita reportar NaN)
  if (num_samples == 0) return;
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
  out[11] = aguaPresente ? 1.0f : 0.0f;  //lluvia: último estado, no se promedia
  reset_acumuladores();  // ← limpia promedios para la siguiente ventana
}



// Cambia el modo de la Peltier (lo decide el horario). Al cambiar se reinicia el integral y la
// viabilidad se evalúa de nuevo sin histéresis.
void CondenserControl::set_modo(ModoPeltier modo_, float pwm_max_) {
  if (modo_ == modo && pwm_max_ == pwm_max) return;
  modo = modo_;
  pwm_max = pwm_max_;
  errorAcumulado = 0.0;
  es_viable = false;
  viabilidad_sin_histeresis = true;
}

bool CondenserControl::balanza_con_error() const {
  return !is_balanza || (millis() - t_balanza_ultimo_dato > BALANZA_TIMEOUT_MS);
}

// Los ventiladores van con la Peltier: apagados si peltier_on es false. Encendidos, el principal
// queda prendido y la chimenea cicla (ventilador_chimenea_on_T / ventilador_chimenea_off_T).
void CondenserControl::actualizar_ventiladores() {
  unsigned long t_now = millis();
  if (!peltier_on) {
    if (ventiladores_on) {
      ApagarVentiladorPrincipal();
      ApagarVentiladorChimenea();
      ventiladores_on = false;
    }
    return;
  }
  if (!ventiladores_on) {
    PrenderVentiladorPrincipal();
    PrenderVentiladorChimenea();
    ventiladores_on = true;
    is_ventilador_chimenea_on = true;
    t_ventilador_ultimo_on = t_now;
    return;
  }
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
  DBGLN("Iniciando Balanza...");
  DBGLN("Iniciando Balanza...");
  balanza.begin(pins.dout, pins.clk);

  if (!balanza.wait_ready_timeout(1000)){
    is_balanza = false;
    DBGLN("Balanza no encontrada");
    return;
  }

  is_balanza = true;
  t_balanza_ultimo_dato = millis();
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
    

    DBGLN("Tara inicial completada y almacenada.");
    DBG("Offset promedio guardado: ");
    DBGLN(offset_promedio);
    DBG("Escala guardada: ");
    DBGLN(SCALE_DEFAULT, 3);
    DBGLN();

  } else {
    long offset;
    float escala;
    cargarCalibracion(offset, escala);
    balanza.set_scale(escala);
    balanza.set_offset(offset);

    DBGLN("Calibracion cargada desde EEPROM:");
    DBG("Offset: ");
    DBGLN(offset);
    DBG("Escala: ");
    DBGLN(escala, 3);
    DBGLN("Listo para medir");
  }    
}

void CondenserControl::ejecutar_volcado() {
  DBGLN("Iniciando secuencia de volcado");

  // Apagar actuadores antes del movimiento mecánico
  ApagarVentiladorPrincipal();
  ApagarVentiladorChimenea();
  analogWrite(pins.rpwm, 0); // pausa forzada: no se toca peltier_on, ya se reporta como VOLCADO
  delay(2000);

  volcar_plato_y_renovar();

  // Volver a operación normal: los ventiladores solo si estaban encendidos (van con la Peltier);
  // la Peltier recupera su PWM en el siguiente ciclo de control
  if (ventiladores_on) {
    PrenderVentiladorPrincipal();
    // Restaurar la chimenea al estado que espera su ciclo (15 s on / 120 s off)
    if (is_ventilador_chimenea_on) PrenderVentiladorChimenea();
  }
  errorAcumulado = 0.0;  // evitar windup acumulado durante la pausa
  DBGLN("Volcado completado — reanudando control");
}

// Secuencia calibrada en hardware (tests/volcado_test). El plato siempre queda sujeto: el volcado
// toma el plato antes de soltar el seguro y se suelta después de trabarlo. La válvula solo se abre
// con el plato en reposo y trabado.
void CondenserControl::volcar_plato_y_renovar() {
  DBGLN("Vaciando el plato del bebedero");

  // 1. El volcado toma el plato en reposo y se suelta el seguro
  volcado.write(VOLCADO_REPOSO);  //posición fijada antes de attach
  volcado.attach(pins.m2);
  delay(500);
  seguro.write(SEGURO_SUELTO);
  delay(ESPERA_SEGURO_MS);

  // 2. Volcar, esperar y volver a reposo (lento)
  moverVolcado(VOLCADO_REPOSO, VOLCADO_VOLCAR);
  delay(PAUSA_VOLCADO_MS);
  moverVolcado(VOLCADO_VOLCAR, VOLCADO_REPOSO);
  delay(ESPERA_SEGURO_MS);

  // 3. Trabar el plato y soltar el volcado (el seguro lo sujeta)
  seguro.write(SEGURO_TRABADO);
  delay(ESPERA_TRABA_MS);         //que el seguro termine de entrar antes de soltar el plato
  volcado.detach();

  // 4. Renovar el agua con el plato trabado
  DBGLN("Renovando el agua");
  valvula.write(VALVULA_ABIERTA);
  delay(TIEMPO_RELLENO_MS);
  valvula.write(VALVULA_CERRADA);
}

// Arranque: lleva el plato a reposo (por si quedó a mitad de un vaciado) y lo deja trabado.
void CondenserControl::reset_plato_pos() {
  DBGLN("Colocando el plato del bebedero");
  valvula.write(VALVULA_CERRADA);
  volcado.write(VOLCADO_REPOSO);  //posición fijada antes de attach
  volcado.attach(pins.m2);
  delay(500);
  seguro.write(SEGURO_SUELTO);    //deja que el volcado lleve el plato a reposo
  delay(ESPERA_SEGURO_MS);
  seguro.write(SEGURO_TRABADO);
  delay(ESPERA_TRABA_MS);         //que el seguro termine de entrar antes de soltar el plato
  volcado.detach();
}

// Mueve el volcado de a 1° (PASO_VOLCADO_MS por grado). Requiere el volcado conectado.
void CondenserControl::moverVolcado(int desde, int hasta) {
  int paso = (hasta > desde) ? 1 : -1;
  for (int a = desde; a != hasta; a += paso) {
    volcado.write(a);
    delay(PASO_VOLCADO_MS);
  }
  volcado.write(hasta);
}


//Helpers
//Clacula el punto de rocío
float CondenserControl::calcularPuntoRocio(float temperaturaC, float humedadRelativa) {
  float alpha = log(humedadRelativa / 100.0) + (17.27 * temperaturaC) / (237.3 + temperaturaC);
  return (237.3 * alpha) / (17.27 - alpha);  //rocío real (Magnus); el margen se aplica en el control
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