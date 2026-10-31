#!/usr/bin/env python3
"""
Herramienta de Diagnóstico y Servidor de Pruebas LAN para Risk of Rain MMP Co-op.
Emite la baliza UDP broadcast (puerto 11101) para auto-descubrimiento en la Miyoo Mini Plus
y levanta un listener TCP (puerto 11100) para registrar la conexión, el handshake y paquetes.
"""

import socket
import struct
import threading
import time
import sys

UDP_BEACON_PORT = 11101
TCP_GAME_PORT   = 11100
BEACON_MAGIC    = "RoR_BEACON"

def get_local_ip():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        # No envía tráfico real, solo resuelve la interfaz LAN activa
        s.connect(('8.8.8.8', 80))
        ip = s.getsockname()[0]
    except Exception:
        ip = '127.0.0.1'
    finally:
        s.close()
    return ip

def get_broadcast_targets(local_ip):
    targets = ['<broadcast>', '255.255.255.255']
    if local_ip and local_ip != '127.0.0.1':
        parts = local_ip.split('.')
        if len(parts) == 4:
            targets.append(f"{parts[0]}.{parts[1]}.{parts[2]}.255")
    # Return unique targets preserving order
    seen = set()
    res = []
    for t in targets:
        if t not in seen:
            seen.add(t)
            res.append(t)
    return res

class BeaconBroadcaster(threading.Thread):
    def __init__(self, host_name, port, local_ip):
        super().__init__(daemon=True)
        self.host_name = host_name
        self.port = port
        self.local_ip = local_ip
        self.running = True

    def run(self):
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        payload = f"{BEACON_MAGIC}:{self.host_name}:{self.port}".encode('utf-8')
        targets = get_broadcast_targets(self.local_ip)
        while self.running:
            for t in targets:
                try:
                    sock.sendto(payload, (t, UDP_BEACON_PORT))
                except Exception:
                    pass
            time.sleep(0.5)
        sock.close()

    def stop(self):
        self.running = False

def run_test_server():
    local_ip = get_local_ip()
    print("=" * 70)
    print("  Risk of Rain MMP - Servidor de Diagnóstico y Pruebas LAN Co-op")
    print("=" * 70)
    print(f"[*] IP Local del Host (PC): {local_ip}")
    print(f"[*] Puerto del Juego (TCP):  {TCP_GAME_PORT}")
    print(f"[*] Puerto Baliza UDP:       {UDP_BEACON_PORT}")
    print("-" * 70)
    print("[1] Emisión de Baliza UDP de auto-descubrimiento iniciada.")
    print("    -> Si la MMP está en la misma red Wi-Fi o Hotspot OnionOS,")
    print(f"       detectará '{local_ip}' al entrar a 'Join Game'.")
    print("-" * 70)
    print("[2] Si usas Prefs.ini como fallback, asegúrate de tener:")
    print("    [Network]")
    print(f'    host_ip="{local_ip}"')
    print(f"    port={TCP_GAME_PORT}")
    print("=" * 70)

    # Iniciar baliza UDP
    beacon = BeaconBroadcaster("PC_Host_Tester", TCP_GAME_PORT, local_ip)
    beacon.start()

    # Iniciar servidor TCP
    tcp_server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    tcp_server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        tcp_server.bind(('0.0.0.0', TCP_GAME_PORT))
        tcp_server.listen(4)
        print(f"[*] Esperando conexión de la consola Miyoo Mini Plus en el puerto {TCP_GAME_PORT}...\n")

        while True:
            client_sock, client_addr = tcp_server.accept()
            print(f"\n[+] ¡CONSOLA CONECTADA! -> IP: {client_addr[0]}:{client_addr[1]}")
            
            # Protocolo de Handshake GameMaker: Studio 1.4
            try:
                client_sock.settimeout(5.0)
                # Paso 1: El servidor GM:S envía el saludo binario
                client_sock.sendall(b"GM:Studio-Connect\x00")
                
                # Paso 2: Recibir respuesta de autenticación del cliente (16 bytes: 0xcafebabe, 0xdeadb00b)
                stage2 = client_sock.recv(1024)
                if stage2 and len(stage2) >= 16 and stage2[:4] == b"\xbe\xba\xfe\xca":
                    # Paso 3: Servidor confirma con firma de sesión GM:S (12 bytes: 0xdeafbead, 0xf00dbeeb, 12)
                    server_reply = struct.pack("<III", 0xdeafbead, 0xf00dbeeb, 12)
                    client_sock.sendall(server_reply)
                    print("    [✓] Handshake GameMaker: Studio 1.4 completado con éxito (0xcafebabe / 0xdeafbead).")
                else:
                    print(f"    [*] Conexión cruda recibida ({len(stage2) if stage2 else 0} bytes).")

                # Mantener conexión y monitorear paquetes con framing 0xdeadc0de
                packet_count = 0
                while True:
                    data = client_sock.recv(4096)
                    if not data:
                        print(f"[-] Consola {client_addr[0]} se desconectó normalmente.")
                        break
                    
                    # Deframing de paquetes GM:S
                    if len(data) >= 12 and data[:4] == b"\xde\xc0\xad\xde":
                        magic, hlen, plen = struct.unpack_from("<III", data, 0)
                        payload = data[12:12+plen]
                        packet_count += 1
                        if packet_count == 1:
                            print(f"    [GM:S Packet] Magic=0xdeadc0de PayloadLen={plen} PayloadHex={payload.hex()[:32]}...")
                        elif packet_count % 60 == 0:
                            print(f"    [Sync] {packet_count} paquetes GM:S procesados a 60 FPS desde la consola.")
                    else:
                        packet_count += 1
                        if packet_count % 60 == 0:
                            print(f"    [Sync] {packet_count} paquetes procesados desde la consola.")
            except socket.timeout:
                print("    [!] Timeout esperando datos de la consola.")
            except Exception as e:
                print(f"    [!] Error en socket de cliente: {e}")
            finally:
                client_sock.close()
                print("[*] Listo para recibir la siguiente conexión...")

    except KeyboardInterrupt:
        print("\n[*] Deteniendo servidor de pruebas...")
    finally:
        beacon.stop()
        tcp_server.close()
        print("[+] Servidor detenido limpiamente.")

if __name__ == '__main__':
    run_test_server()
