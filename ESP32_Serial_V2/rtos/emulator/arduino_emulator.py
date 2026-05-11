import time
import serial
from pySerialTransfer import pySerialTransfer as txfer

# --- CONFIGURACIÓN ---
PORT = '/dev/ttyUSB0'
BAUD = 115200

def send_packet(link, cmd, extra=None):
    """Lógica de empaquetado idéntica a tu script original"""
    offset = 0
    t = time.localtime()
    ts = [t.tm_year, t.tm_mon, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec]
    
    # Timestamp (12 bytes)
    for val in ts:
        offset = link.tx_obj(val, start_pos=offset, val_type_override='H')
    
    # Comando (1 byte)
    offset = link.tx_obj(cmd, start_pos=offset, val_type_override='B')

    if cmd == 0x02: # CMD_SAVE_EVENT
        offset = link.tx_obj(extra, start_pos=offset, val_type_override='B')
    elif cmd == 0x03: # CMD_SAVE_DATA
        offset = link.tx_obj(extra[0], start_pos=offset, val_type_override='B')
        offset = link.tx_obj(extra[1], start_pos=offset, val_type_override='f')

    link.send(offset)

def main():
    try:
        # 1. Abrimos el puerto de forma manual (lo que funcionó en test_raw.py)
        raw_ser = serial.Serial(PORT, BAUD, timeout=0.1)
        raw_ser.setDTR(False)
        raw_ser.setRTS(False)
        
        # 2. Inicializamos SerialTransfer pasándole la conexión ya abierta
        link = txfer.SerialTransfer(PORT, baud=BAUD)
        link.connection = raw_ser 
        
        print(f"--- Conectado a {PORT} ---")
        print("Dale un segundo al ESP32 para estabilizarse...")
        time.sleep(2)

        while True:
            print("\n--- MENÚ ---")
            print("1. Foto | 2. Evento BIRD | 3. Sensor T1_K | q. Salir")
            opcion = input(">> ").lower()

            if opcion == '1':
                send_packet(link, 0x01)
                print(">> Comando Foto enviado.")
            elif opcion == '2':
                send_packet(link, 0x02, 0x81)
                print(">> Evento BIRD enviado.")
            elif opcion == '3':
                val = float(input("Temp (ej 25.5): "))
                send_packet(link, 0x03, [0x20, val])
                print(f">> Dato {val} enviado.")
            elif opcion == 'q':
                break

            # 3. Escucha de Respuesta (Mezcla de ACK y Logs)
            timeout = time.time() + 20
            while time.time() < timeout:
                # ¿Llegó un paquete estructurado (ACK)?
                if link.available():
                    # Status en posición 13 (TS=12 + CMD=1)
                    res_status = link.rx_obj(obj_type='B', start_pos=13)
                    print(f"\n[ACK RECIBIDO] Status ESP32: {res_status}")
                    break
                
                # ¿Llegó texto plano (Logs)?
                if raw_ser.in_waiting > 0:
                    raw_data = raw_ser.read(raw_ser.in_waiting)
                    try:
                        log_text = raw_data.decode('utf-8', errors='ignore').strip()
                        if log_text:
                            print(f"[ESP32 LOG]: {log_text}")
                    except:
                        pass
                time.sleep(0.1)

    except Exception as e:
        print(f"\nError: {e}")
    finally:
        if 'raw_ser' in locals():
            raw_ser.close()
            print("Puerto cerrado.")

if __name__ == '__main__':
    main()