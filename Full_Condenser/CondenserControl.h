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

class CondenserControl {
    //estructura que aloja los pines (no son los pines en sí)
    public:
        struct  Pins
        {
            //DHT   
            uint8_t dht_pin1, dht_pin2, dht_type;
            // Termocuplas
            uint8_t max_d01, max_cs1, max_clk1, max_d02, max_cs2, max_clk2;
            //L298N
            uint8_t in1, in2, ena, in3, in4, enb;
            //celdas peltier
            uint8_t rpwm, lpwm, ren, len;
            //Sensor de corriente
            uint8_t cSP1, cSP2, cSP3;
            //Balanza
            uint8_t dout, clk;
        };

        static const size_t N_DATA_CRL = 16;

        explicit CondenserControl(const Pins& p); //constructor
        void iniciar_control();
        void leer_sensores_y_controlar();
        
        void PrenderMotor();
        void ApagarMotor();
        void promediar(float out[N_DATA_CRL]);
        //inline
        void set_PI_parameters (float kp_, float ki_, float maxint_) {kp = kp_; ki = ki_; maxIntegracion= maxint_; };
        
    
    private:

        Pins pins;               
        DHT_Unified dht1;       
        DHT_Unified dht2;       
        Adafruit_MAX31855 tc1;    
        Adafruit_MAX31855 tc2;
        HX711 balanza;
        sensors_event_t event{};  

        //motor
        bool motorEncendido;

        //Escala de la balanza
        const float SCALE_DEFAULT = -422.55f;

        // Direcciones de EEPROM
        const int EEPROM_OFFSET_ADDR = 0;  // long (4 bytes)
        const int EEPROM_SCALE_ADDR  = 4;  // float (4 bytes)
        const int EEPROM_FLAG_ADDR   = 8;  // byte (1 byte)
        bool borrar_datos_eeprom = false;  // Señal para re-tarear


        // Identificador de datos válidos
        const byte CALIB_OK = 0x42;

        //Medición de corriente
        const float Rshunt1 = 0.1; // Por ejemplo, 0.1 Ohm
        const float Rshunt2 = 0.1; // Por ejemplo, 0.1 Ohm
        const float Rshunt3 = 0.1; // Por ejemplo, 0.1 Ohm

        // Ganancia del amplificador operacional para cada sensor
        // Asumiendo que todos tienen una ganancia de 10 (1k/10k)
        const float opAmpGain1 = 10.0;
        const float opAmpGain2 = 10.0;
        const float opAmpGain3 = 10.0;

        // UMBRAL: Corriente mínima en mA para que la medición sea considerada válida
        const float MIN_CURRENT_THRESHOLD_mA = 100.0; // 100 mA
         
        //Mediciones
        float c1;
        float c2;
        float c12;
        float tempAmbiente1;
        float tempAmbiente2;
        float humedad1;
        float humedad2;
        float puntoRocio;

        float current_mA1;
        float current_mA2;
        float current_mA3;

        float peso_agua;

        //Acumuladores
        float   T1_sum = 0.0; //tempAmbiente2
        float   T2_sum = 0.0; //tempAmbiente1
        float   T3_sum = 0.0;//c1
        float   T4_sum = 0.0;//c2
        float   T5_sum = 0.0;//c12
        float   T6_sum = 0.0;//tempObjetivo
        float   H1_sum = 0.0;//humedad2    
        float   H2_sum = 0.0;//humedad1
        float   E1_sum = 0.0;//error
        float   E2_sum = 0.0;//errorAcumulado
        float   P1_sum = 0.0;//puntoRocio
        float   P2_sum = 0.0;//pwm 
        //-----------nuevo-------//
        float   I1_sum = 0.0;
        float   I2_sum = 0.0;
        float   I3_sum = 0.0;
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
        void autoTareInicial();
};