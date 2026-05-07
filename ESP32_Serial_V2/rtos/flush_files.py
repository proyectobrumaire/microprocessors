import requests
import os

# Configuración
BASE_URL = "http://esp32cam.local"
DOWNLOAD_DIR = "fotos_esp32"

# Crear carpeta de destino si no existe
if not os.path.exists(DOWNLOAD_DIR):
    os.makedirs(DOWNLOAD_DIR)

def automatizar_camara():
    try:
        # 1. LISTAR archivos
        print(f"Conectando a {BASE_URL}/list...")
        response = requests.get(f"{BASE_URL}/list", timeout=10)
        response.raise_for_status()
        data = response.json()
        
        files = data.get("files", [])
        print(f"Se encontraron {len(files)} archivos.")

        for f in files:
            filename = f["name"]
            
            # PROTECCIÓN: No eliminar ni procesar el log
            if filename == "log.txt":
                print(f"--> Saltando {filename} (protegido)")
                continue

            # 2. DESCARGAR
            download_url = f"{BASE_URL}/download?file={filename}"
            local_path = os.path.join(DOWNLOAD_DIR, filename)
            
            print(f"Descargando {filename}...", end=" ")
            res_get = requests.get(download_url)
            
            if res_get.status_code == 200:
                with open(local_path, "wb") as file:
                    file.write(res_get.content)
                print("OK")
                
                # 3. ELIMINAR (Solo si la descarga fue exitosa)
                delete_url = f"{BASE_URL}/delete?file={filename}"
                res_del = requests.get(delete_url)
                
                if res_del.status_code == 200:
                    print(f"   [!] {filename} eliminado de la SD.")
                else:
                    print(f"   [X] Error al eliminar {filename}")
            else:
                print(f"FALLÓ (Status: {res_get.status_code})")

    except requests.exceptions.RequestException as e:
        print(f"Error de conexión: {e}")
    except Exception as e:
        print(f"Ocurrió un error inesperado: {e}")

if __name__ == "__main__":
    automatizar_camara()
    print("\nProceso finalizado.")
