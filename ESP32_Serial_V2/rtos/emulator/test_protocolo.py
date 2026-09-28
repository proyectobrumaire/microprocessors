"""
Prueba automática del protocolo serial Arduino -> ESP32 (ver PROTOCOL.md).

Emula al Arduino: envía SAVE_EVENT, los 11 SAVE_DATA y (opcional) TAKE_PHOTO,
y verifica cada ACK del ESP32: comando, status = 1 y que el timestamp devuelto
sea el mismo que se envió.

Uso (con el ESP32 conectado por USB-serie, sin el Arduino en la línea):
    python3 -m venv .venv && .venv/bin/pip install pyserial pySerialTransfer requests
    .venv/bin/python test_protocolo.py [--port /dev/ttyUSB0] [--foto] [--host esp32cam.local]

--foto : incluye un evento BIRD (toma 3 fotos reales en la SD).
--host : al final descarga log.txt por HTTP y verifica que estén las líneas
         enviadas (el PC debe estar en la misma red que el ESP32). No borra nada.
"""
import argparse
import sys
import time

import serial
from pySerialTransfer import pySerialTransfer as txfer

CMD_TAKE_PHOTO, CMD_SAVE_EVENT, CMD_SAVE_DATA = 0x01, 0x02, 0x03
EVENTOS = {'BOOT': 0x80, 'BIRD': 0x81, 'PERIODIC': 0x82}

# (código, clave, valor de prueba) — mismas 11 claves y orden que Full_Condenser
SENSORES = [
    (0x20, 'T1_K', 25.0), (0x21, 'T2_K', 28.0), (0x22, 'T3_K', 10.0),
    (0x23, 'T4_K', 10.5), (0x24, 'T5_K', 10.25), (0x26, 'H1_K', 65.0),
    (0x27, 'H2_K', 55.0), (0x2A, 'P1_K', 18.0), (0x2B, 'P2_K', 128.0),
    (0x2F, 'I4_K', 1.5), (0x30, 'W1_K', 250.0),
]

ACK_TIMEOUT_S = 3.0


def conectar(port):
    # DTR/RTS en bajo para no reiniciar ni dejar el ESP32 en modo programación
    raw = serial.Serial()
    raw.port, raw.baudrate, raw.timeout = port, 115200, 0.05
    raw.dtr = False
    raw.rts = False
    raw.open()
    link = txfer.SerialTransfer(port, baud=115200)
    link.connection = raw
    return link, raw


def ts_ahora():
    t = time.localtime()
    return [t.tm_year - 2000, t.tm_mon, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec]


def enviar(link, ts, cmd, extra=()):
    pos = 0
    for v in ts:
        pos = link.tx_obj(v, start_pos=pos, val_type_override='H')
    pos = link.tx_obj(cmd, start_pos=pos, val_type_override='B')
    for valor, tipo in extra:
        pos = link.tx_obj(valor, start_pos=pos, val_type_override=tipo)
    link.send(pos)


def esperar_ack(link):
    fin = time.time() + ACK_TIMEOUT_S
    while time.time() < fin:
        if link.available():
            ts = link.rx_obj(obj_type=list, start_pos=0, obj_byte_size=12, list_format='H')
            cmd = link.rx_obj(obj_type='B', start_pos=12)
            status = link.rx_obj(obj_type='B', start_pos=13)
            return list(ts), cmd, status
        time.sleep(0.005)
    return None


def probar(link, nombre, ts, cmd, extra=()):
    enviar(link, ts, cmd, extra)
    ack = esperar_ack(link)
    if ack is None:
        print(f'  FALLA  {nombre}: sin ACK en {ACK_TIMEOUT_S} s')
        return False
    ts_ack, cmd_ack, status = ack
    errores = []
    if cmd_ack != cmd:
        errores.append(f'cmd {cmd_ack:#04x} != {cmd:#04x}')
    if status != 1:
        errores.append(f'status {status}')
    if ts_ack != ts:
        errores.append(f'timestamp {ts_ack} != {ts}')
    if errores:
        print(f'  FALLA  {nombre}: ' + ', '.join(errores))
        return False
    print(f'  ok     {nombre}')
    return True


def evento_completo(link, nombre_ev, con_foto):
    ts = ts_ahora()
    print(f'\nEvento {nombre_ev} ts={ts}')
    resultados = []
    if con_foto:
        resultados.append(probar(link, 'TAKE_PHOTO', ts, CMD_TAKE_PHOTO))
        time.sleep(4)  # el ESP32 toma 3 fotos con 1 s entre cada una
    resultados.append(probar(link, f'SAVE_EVENT {nombre_ev}', ts, CMD_SAVE_EVENT,
                             [(EVENTOS[nombre_ev], 'B')]))
    for codigo, clave, valor in SENSORES:
        resultados.append(probar(link, f'SAVE_DATA {clave}', ts, CMD_SAVE_DATA,
                                 [(codigo, 'B'), (valor, 'f')]))
        time.sleep(0.05)
    return ts, resultados


def verificar_log(host, eventos):
    import requests
    print(f'\nDescargando log.txt de http://{host} ...')
    res = requests.get(f'http://{host}/download', params={'file': 'log.txt'}, timeout=30)
    res.raise_for_status()
    lineas = res.text.splitlines()
    ok = True
    for nombre_ev, ts in eventos:
        ts_txt = '%02d-%02d-%02dT%02d-%02d-%02d' % tuple(ts)
        del_evento = [l for l in lineas if l.startswith(ts_txt)]
        esperado = 1 + len(SENSORES)
        estado = 'ok   ' if len(del_evento) >= esperado else 'FALLA'
        ok &= len(del_evento) >= esperado
        print(f'  {estado} {nombre_ev} {ts_txt}: {len(del_evento)}/{esperado} líneas en log.txt')
    return ok


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--port', default='/dev/ttyUSB0')
    ap.add_argument('--foto', action='store_true', help='incluir un evento BIRD con fotos')
    ap.add_argument('--host', help='verificar log.txt por HTTP (p. ej. esp32cam.local)')
    args = ap.parse_args()

    link, raw = conectar(args.port)
    try:
        time.sleep(1.0)
        raw.reset_input_buffer()
        eventos, resultados = [], []
        for nombre, foto in [('BOOT', False), ('PERIODIC', False)] + ([('BIRD', True)] if args.foto else []):
            ts, r = evento_completo(link, nombre, foto)
            eventos.append((nombre, ts))
            resultados += r
            time.sleep(1.1)  # timestamps distintos entre eventos
        total, oks = len(resultados), sum(resultados)
        print(f'\nACKs: {oks}/{total} correctos')
        ok = oks == total
        if args.host:
            time.sleep(1)
            ok &= verificar_log(args.host, eventos)
        print('\nRESULTADO:', 'OK' if ok else 'FALLA')
        sys.exit(0 if ok else 1)
    finally:
        raw.close()


if __name__ == '__main__':
    main()
