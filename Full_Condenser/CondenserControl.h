#pragma once
#include <Wire.h>
#include <stdio.h>
#include <SPI.h>
#include <math.h>
#include <DHT.h>
#include <DHT_U.h>
#include "Adafruit_MAX31855.h"
#include <Arduino.h>
#include <HX711.h>
#include <EEPROM.h>
#include <Servo.h>

class CondenserControl {
    //estructura que aloja los pines (no son los pines en sí)
    public:
        struct  Pins
        {
            //DHT   
            uint8_t dht_pin1, dht_pin2;
            // Termocuplas
            uint8_t max_d01, max_cs1, max_clk1, max_d02, max_cs2, max_clk2;
            //L298N
            uint8_t in1, in2, ena, in3, in4, enb;
            //celdas peltier
            uint8_t rpwm, lpwm, ren, len;
            //Balanza
            uint8_t dout, clk;
            //sensor de corrinete
            uint8_t otrosensor;
            //motores
            uint8_t m1, m2, mv;
        };

        static const size_t N_DATA_CRL = 11;

        bool peltier_on;

        explicit CondenserControl(const Pins& p); //constructor
        void iniciar_control();
        void leer_sensores_y_controlar();
        
        void promediar(float out[N_DATA_CRL]);
        void ejecutar_volcado();
        //inline
        void set_PI_parameters (float kp_, float ki_, float maxint_) {kp = kp_; ki = ki_; maxIntegracion= maxint_; };
        
    
    private:

        Pins pins;               
        DHT_Unified dht1;       
        DHT_Unified dht2;       
        Adafruit_MAX31855 tc1;    
        Adafruit_MAX31855 tc2;
        HX711 balanza;

        bool is_balanza;
        sensors_event_t event{};  

        //motor
        void ApagarVentiladorPrincipal();
        void PrenderVentiladorPrincipal();
        void PrenderVentiladorChimenea();
        void ApagarVentiladorChimenea();
        bool is_ventilador_chimenea_on;
        unsigned long t_ventilador_ultimo_on;
        unsigned long ventilador_chimenea_on_T = 15000;
        unsigned long ventilador_chimenea_off_T = 120000;

        

        //Escala de la balanza
        const float SCALE_DEFAULT = -422.55f;

        //Seguridad placas peltier
        static constexpr int max_peltier_op = 100;
        static constexpr float peltier_delta_T = -0.0745; //Grados por unidad pwm     
        float peltier_temp_amb_max;   
        float peltier_pwm_max;

        //Servomotores
        Servo volcado;
        Servo seguro;
        Servo valvula;
        void volcar_plato_y_renovar();
        void reset_plato_pos();


        // Direcciones de EEPROM
        const int EEPROM_OFFSET_ADDR = 0;  // long (4 bytes)
        const int EEPROM_SCALE_ADDR  = 4;  // float (4 bytes)
        const int EEPROM_FLAG_ADDR   = 8;  // byte (1 byte)
        bool borrar_datos_eeprom = false;  // Señal para re-tarear

        // Identificador de datos válidos
        const byte CALIB_OK = 0x42;
         
        //Mediciones
        float c1;
        float c2;
        float c12;
        float tempAmbiente1;
        float tempAmbiente2;
        float humedad1;
        float humedad2;
        float puntoRocio;


        float peso_agua;

        float voltajeCorrienteFiltrada;
        //Acumuladores
        float   T1_sum = 0.0; //tempAmbiente2
        float   T2_sum = 0.0; //tempAmbiente1
        float   T3_sum = 0.0;//c1
        float   T4_sum = 0.0;//c2
        float   T5_sum = 0.0;//c12
        float   H1_sum = 0.0;//humedad2    
        float   H2_sum = 0.0;//humedad1
        float   P1_sum = 0.0;//puntoRocio
        float   P2_sum = 0.0;//pwm 
        //-----------nuevo-------//
        float   I4_sum = 0.0;
        float   W1_sum = 0.0;
        int num_samples = 0;
        
        unsigned long t_ctrl_prev=0;
        //PI
        float kp;
        float ki;
        float maxIntegracion;
        float errorAcumulado = 0.0;
        float salidaPI = 0.0;
        float tempObjetivo = 0.0;
        float pwm = 0.0;
        float error = 0.0;

        void reset_acumuladores();
        float calcularPuntoRocio (float temperaturaC, float humedadRelativa);
        float safe_avg(float  sum, int n);
        void cargarCalibracion(long &offset, float &escala);
        void guardarCalibracion(long offset, float escala);
        void balanzaInicial();
};