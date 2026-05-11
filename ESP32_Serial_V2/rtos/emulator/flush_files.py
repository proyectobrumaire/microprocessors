import requests
import os
from datetime import datetime

BASE_URL = "http://esp32cam.local"
PHOTOS_DIR = "fotos_esp32"
LOGS_DIR = "logs_esp32"

os.makedirs(PHOTOS_DIR, exist_ok=True)
os.makedirs(LOGS_DIR, exist_ok=True)


def set_time():
    now = datetime.now()
    ts = [now.year - 2000, now.month, now.day, now.hour, now.minute, now.second]
    requests.post(f"{BASE_URL}/set_time", json={"ts": ts}, timeout=10).raise_for_status()


def flush():
    try:
        # 0. Sincronizar hora
        print("Sincronizando hora con el ESP32...")
        try:
            set_time()
            print("OK")
        except Exception as e:
            print(f"[!] No se pudo sincronizar la hora: {e}")

        # 1. Listar archivos
        print(f"\nConectando a {BASE_URL}/list...")
        res_list = requests.get(f"{BASE_URL}/list", timeout=10)
        res_list.raise_for_status()
        files = res_list.json().get("files", [])
        print(f"{len(files)} archivos encontrados.")

        photos = [f for f in files if f["name"].endswith(".jpg")]
        has_log = any(f["name"] == "log.txt" for f in files)
        print(f"  {len(photos)} fotos, {'1 log' if has_log else 'sin log'}.")

        # 2. Fotos: descargar → eliminar
        for f in photos:
            name = f["name"]
            print(f"\nDescargando {name}...", end=" ", flush=True)
            res = requests.get(f"{BASE_URL}/download?file={name}", timeout=60)
            if res.status_code != 200:
                print(f"FALLÓ ({res.status_code})")
                continue
            with open(os.path.join(PHOTOS_DIR, name), "wb") as fh:
                fh.write(res.content)
            print("OK")

            res_del = requests.get(f"{BASE_URL}/delete?file={name}", timeout=10)
            if res_del.status_code == 200:
                print(f"  → {name} eliminado de la SD.")
            else:
                print(f"  [X] Error al eliminar {name} ({res_del.status_code})")

        # 3. Log: descargar → guardar con timestamp → reset_log
        if not has_log:
            print("\nNo hay log.txt en la SD.")
        else:
            print("\nDescargando log.txt...", end=" ", flush=True)
            res = requests.get(f"{BASE_URL}/download?file=log.txt", timeout=60)
            if res.status_code != 200:
                print(f"FALLÓ ({res.status_code})")
            else:
                ts_str = datetime.now().strftime("%y-%m-%dT%H-%M-%S")
                log_path = os.path.join(LOGS_DIR, f"log_{ts_str}.txt")
                with open(log_path, "wb") as fh:
                    fh.write(res.content)
                print(f"OK → {log_path}")

                res_reset = requests.post(f"{BASE_URL}/reset_log", timeout=10)
                if res_reset.status_code == 200:
                    print("  → Log reseteado en el ESP32 (epoch incrementado).")
                else:
                    print(f"  [X] Error al resetear el log ({res_reset.status_code})")

    except requests.exceptions.RequestException as e:
        print(f"\nError de conexión: {e}")
    except Exception as e:
        print(f"\nError inesperado: {e}")


if __name__ == "__main__":
    flush()
    print("\nProceso finalizado.")
