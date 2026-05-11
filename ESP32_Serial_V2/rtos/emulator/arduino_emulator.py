import time
import serial
from pySerialTransfer import pySerialTransfer as txfer

PORT = '/dev/ttyUSB0'
BAUD = 115200

# (código, clave, descripción, valor_default)
SENSORS = [
    (0x20, 'T1_K', 'Temp. ambiente °C',      25.0),
    (0x21, 'T2_K', 'Temp. caja interna °C',  28.0),
    (0x22, 'T3_K', 'Placa fría 1 °C',        10.0),
    (0x23, 'T4_K', 'Placa fría 2 °C',        10.5),
    (0x24, 'T5_K', 'Temp. media fría °C',    10.2),
    (0x25, 'T6_K', 'Temp. objetivo °C',       8.0),
    (0x26, 'H1_K', 'Humedad externa %',      65.0),
    (0x27, 'H2_K', 'Humedad interna %',      55.0),
    (0x28, 'E1_K', 'Error',                   0.0),
    (0x29, 'E2_K', 'Error acumulado',         0.0),
    (0x2A, 'P1_K', 'Punto de rocío °C',      18.0),
    (0x2B, 'P2_K', 'PWM aplicado %',         50.0),
    (0x2C, 'I1_K', 'Corriente Panel A',       2.5),
    (0x2D, 'I2_K', 'Corriente Turbina A',     1.2),
    (0x2E, 'I3_K', 'Corriente Batería A',     0.8),
    (0x2F, 'I4_K', 'Corriente filtrada A',    1.5),
    (0x30, 'W1_K', 'Peso del agua g',       250.0),
]

sensor_values = {code: default for code, _, _, default in SENSORS}

EVENTS = {
    '1': (0x81, 'BIRD',     True),   # (código, nombre, incluye_foto)
    '2': (0x82, 'PERIODIC', False),
    '3': (0x80, 'BOOT',     False),
}


def send_packet(link, cmd, extra=None):
    offset = 0
    t = time.localtime()
    ts = [t.tm_year - 2000, t.tm_mon, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec]
    for val in ts:
        offset = link.tx_obj(val, start_pos=offset, val_type_override='H')
    offset = link.tx_obj(cmd, start_pos=offset, val_type_override='B')
    if cmd == 0x02:
        offset = link.tx_obj(extra, start_pos=offset, val_type_override='B')
    elif cmd == 0x03:
        offset = link.tx_obj(extra[0], start_pos=offset, val_type_override='B')
        offset = link.tx_obj(extra[1], start_pos=offset, val_type_override='f')
    link.send(offset)


def wait_ack(link, raw_ser, timeout=5):
    deadline = time.time() + timeout
    while time.time() < deadline:
        if link.available():
            return link.rx_obj(obj_type='B', start_pos=13)
        if raw_ser.in_waiting > 0:
            text = raw_ser.read(raw_ser.in_waiting).decode('utf-8', errors='ignore').strip()
            if text:
                print(f"\n  [LOG] {text}")
        time.sleep(0.05)
    return None


def send_and_print(link, raw_ser, label, cmd, extra=None):
    print(f"  → {label}", end=' ', flush=True)
    send_packet(link, cmd, extra)
    ack = wait_ack(link, raw_ser)
    print('[ACK]' if ack is not None else '[SIN ACK]')
    time.sleep(0.05)


def fire_event(link, raw_ser, event_code, event_name, with_photo):
    print(f"\nDisparando {event_name}...")

    if with_photo:
        send_and_print(link, raw_ser, 'CMD_TAKE_PHOTO', 0x01)

    send_and_print(link, raw_ser, f'CMD_SAVE_EVENT {event_name}', 0x02, event_code)

    for code, key, _, _ in SENSORS:
        val = sensor_values[code]
        send_and_print(link, raw_ser, f'CMD_SAVE_DATA {key} = {val:.2f}', 0x03, [code, val])

    print(f"✓ {event_name} completado.")


def edit_sensors():
    while True:
        print("\n--- EDITAR SENSORES ---")
        for i, (code, key, desc, _) in enumerate(SENSORS, 1):
            print(f"  {i:2}. {key:6}  {desc:25}  = {sensor_values[code]:.2f}")
        print("   0. Volver")
        try:
            sel = int(input("Sensor a editar (0 para volver): "))
        except ValueError:
            continue
        if sel == 0:
            break
        if 1 <= sel <= len(SENSORS):
            code, key, desc, _ = SENSORS[sel - 1]
            try:
                val = float(input(f"  Nuevo valor para {key} ({desc}): "))
                sensor_values[code] = val
                print(f"  ✓ {key} = {val:.2f}")
            except ValueError:
                print("  Valor inválido, sin cambios.")


def print_sensor_summary():
    rows = [f"{key}={sensor_values[code]:.1f}" for code, key, _, _ in SENSORS]
    for i in range(0, len(rows), 6):
        print("  " + "  |  ".join(rows[i:i+6]))


def main():
    try:
        raw_ser = serial.Serial(PORT, BAUD, timeout=0.1)
        raw_ser.setDTR(False)
        raw_ser.setRTS(False)
        link = txfer.SerialTransfer(PORT, baud=BAUD)
        link.connection = raw_ser

        print(f"Conectado a {PORT}. Esperando al ESP32...")
        time.sleep(2)

        while True:
            print("\n══════════ BRUMAIRE EMULATOR ══════════")
            print_sensor_summary()
            print()
            print("  1. BIRD     — foto + evento + todos los sensores")
            print("  2. PERIODIC — evento + todos los sensores")
            print("  3. BOOT     — evento + todos los sensores")
            print("  4. Editar valores de sensores")
            print("  q. Salir")

            op = input("\n>> ").strip().lower()

            if op in EVENTS:
                code, name, with_photo = EVENTS[op]
                fire_event(link, raw_ser, code, name, with_photo)
            elif op == '4':
                edit_sensors()
            elif op == 'q':
                break

    except Exception as e:
        print(f"\nError: {e}")
    finally:
        if 'raw_ser' in locals():
            raw_ser.close()
            print("Puerto cerrado.")


if __name__ == '__main__':
    main()
