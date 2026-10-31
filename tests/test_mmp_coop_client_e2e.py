import os
import socket
import struct
import threading
import time
import pytest

# Risk of Rain 1.3.0 Binary Protocol Packet IDs
PACKET_HANDSHAKE_REQ = 0x01
PACKET_HANDSHAKE_ACK = 0x02
PACKET_PLAYER_SPAWN  = 0x05
PACKET_GHOST_UPDATE  = 0x06
PACKET_DISCONNECT    = 0x09

class MockRiskOfRainPCServer:
    def __init__(self, host="127.0.0.1", port=0):
        self.host = host
        self.server_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.server_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.server_sock.bind((host, port))
        self.port = self.server_sock.getsockname()[1]
        self.server_sock.listen(1)
        self.client_sock = None
        self.connected_event = threading.Event()
        self.running = True
        self.received_packets = []
        self.thread = threading.Thread(target=self._run, daemon=True)
        self.thread.start()

    def _run(self):
        try:
            self.server_sock.settimeout(2.0)
            self.client_sock, _ = self.server_sock.accept()
            self.connected_event.set()
            self.client_sock.settimeout(2.0)
            while self.running:
                data = self.client_sock.recv(4096)
                if not data:
                    break
                self.received_packets.append(data)
        except Exception:
            pass

    def send_packet(self, data: bytes):
        self.connected_event.wait(timeout=2.0)
        if self.client_sock:
            self.client_sock.sendall(data)

    def close(self):
        self.running = False
        if self.client_sock:
            try:
                self.client_sock.close()
            except Exception:
                pass
        try:
            self.server_sock.close()
        except Exception:
            pass
        self.thread.join(timeout=1.0)

def test_mock_pc_server_handshake_and_ghost_update_stream():
    """Verify Little-Endian binary protocol compatibility between PC Host and MMP Client."""
    server = MockRiskOfRainPCServer()
    try:
        # Simulate MMP client connecting
        client = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        client.connect((server.host, server.port))

        # 1. Client sends Handshake
        client_handshake = struct.pack("<BB16s", PACKET_HANDSHAKE_REQ, 1, b"MiyooMiniPlus\x00\x00\x00")
        client.sendall(client_handshake)
        time.sleep(0.05)

        assert len(server.received_packets) > 0
        req_id, p_num = struct.unpack("<BB", server.received_packets[0][:2])
        assert req_id == PACKET_HANDSHAKE_REQ
        assert p_num == 1

        # 2. Server sends Handshake ACK with Player 2 slot
        ack = struct.pack("<BBII", PACKET_HANDSHAKE_ACK, 2, 60, 0x1337)
        server.send_packet(ack)

        client.settimeout(1.0)
        resp = client.recv(1024)
        assert len(resp) == struct.calcsize("<BBII")
        resp_id, assigned_slot, tick_rate, seed = struct.unpack("<BBII", resp)
        assert resp_id == PACKET_HANDSHAKE_ACK
        assert assigned_slot == 2
        assert tick_rate == 60
        assert seed == 0x1337

        # 3. Server sends 60 FPS Ghost Entity Spawn & Position Sync Updates
        # Spawn ghost player at x=120.5, y=340.0
        spawn_pkt = struct.pack("<Biffh", PACKET_PLAYER_SPAWN, 201, 120.5, 340.0, 15)
        server.send_packet(spawn_pkt)

        spawn_recv = client.recv(1024)
        s_id, ent_id, spawn_x, spawn_y, spr = struct.unpack("<Biffh", spawn_recv)
        assert s_id == PACKET_PLAYER_SPAWN
        assert ent_id == 201
        assert abs(spawn_x - 120.5) < 0.001
        assert abs(spawn_y - 340.0) < 0.001
        assert spr == 15

        # Stream 10 movement updates (ghost_update)
        for frame in range(10):
            sync_x = 120.5 + frame * 2.0
            sync_y = 340.0 - frame * 1.5
            hp = 100.0 - frame * 1.0
            update_pkt = struct.pack("<Bifff", PACKET_GHOST_UPDATE, 201, sync_x, sync_y, hp)
            server.send_packet(update_pkt)

            pkt_data = client.recv(1024)
            u_id, u_ent, rx, ry, rhp = struct.unpack("<Bifff", pkt_data)
            assert u_id == PACKET_GHOST_UPDATE
            assert u_ent == 201
            assert abs(rx - sync_x) < 0.001
            assert abs(ry - sync_y) < 0.001
            assert abs(rhp - hp) < 0.001

        client.close()
    finally:
        server.close()

def test_mock_pc_discovery_beacon_format_and_parsing():
    """Verify UDP broadcast beacon conforms to RoR_BEACON:<HostName>:<Port> specification."""
    beacon_payload = b"RoR_BEACON:PC_Dedicated_Host:11100"
    assert beacon_payload.startswith(b"RoR_BEACON:")

    parts = beacon_payload.decode("utf-8").split(":")
    assert len(parts) == 3
    assert parts[0] == "RoR_BEACON"
    assert parts[1] == "PC_Dedicated_Host"
    assert int(parts[2]) == 11100

def test_high_throughput_packet_framing_burst():
    """Verify 100 packets in burst arrive intact without stream corruption."""
    server = MockRiskOfRainPCServer()
    try:
        client = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        client.connect((server.host, server.port))

        total_packets = 100
        burst = b"".join(struct.pack("<Bi", PACKET_GHOST_UPDATE, i) for i in range(total_packets))
        server.send_packet(burst)

        received = b""
        client.settimeout(2.0)
        expected_len = total_packets * struct.calcsize("<Bi")
        while len(received) < expected_len:
            chunk = client.recv(4096)
            if not chunk:
                break
            received += chunk

        assert len(received) == expected_len
        for i in range(total_packets):
            offset = i * struct.calcsize("<Bi")
            pkt_id, pkt_val = struct.unpack_from("<Bi", received, offset)
            assert pkt_id == PACKET_GHOST_UPDATE
            assert pkt_val == i

        client.close()
    finally:
        server.close()
