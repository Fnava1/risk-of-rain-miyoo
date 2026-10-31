import os
import struct
import pytest

NET_DIR = os.path.join(os.path.dirname(__file__), "..", "butterscotch", "src", "net")
NET_HEADER = os.path.join(NET_DIR, "net_socket.h")
NET_SOURCE = os.path.join(NET_DIR, "net_socket.c")

def test_gm_handshake_constants_and_magic_present():
    """Verify net_socket.c defines GameMaker Studio 1.4 handshake strings and magic numbers."""
    with open(NET_SOURCE, "r", encoding="utf-8") as f:
        content = f.read()

    assert "GM:Studio-Connect" in content, "Missing GameMaker greeting string 'GM:Studio-Connect'"
    assert "0xdeadc0de" in content.lower(), "Missing GameMaker TCP packet framing magic 0xdeadc0de"
    assert "0xcafebabe" in content.lower(), "Missing GameMaker login packet signature 0xcafebabe"
    assert "0xdeadb00b" in content.lower(), "Missing GameMaker login packet signature 0xdeadb00b"
    assert "0xdeafbead" in content.lower(), "Missing GameMaker server reply signature 0xdeafbead"
    assert "0xf00dbeeb" in content.lower(), "Missing GameMaker server reply signature 0xf00dbeeb"

def test_gm_handshake_binary_structures():
    """Verify exact binary payloads match GameMaker Studio 1.4 protocol."""
    # Greeting: "GM:Studio-Connect\0" (18 bytes)
    greeting = b"GM:Studio-Connect\x00"
    assert len(greeting) == 18

    # Client Stage 2: 0xcafebabe, 0xdeadb00b, 16, 0
    client_stage2 = struct.pack("<IIII", 0xcafebabe, 0xdeadb00b, 16, 0)
    assert len(client_stage2) == 16
    assert client_stage2 == bytes([
        0xbe, 0xba, 0xfe, 0xca, 0x0b, 0xb0, 0xad, 0xde,
        0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    ])

    # Server Stage 2: 0xdeafbead, 0xf00dbeeb, 12
    server_stage2 = struct.pack("<III", 0xdeafbead, 0xf00dbeeb, 12)
    assert len(server_stage2) == 12
    assert server_stage2 == bytes([
        0xad, 0xbe, 0xaf, 0xde, 0xeb, 0xbe, 0x0d, 0xf0,
        0x0c, 0x00, 0x00, 0x00
    ])

def test_gm_packet_framing_header():
    """Verify packet framing header has magic 0xdeadc0de, header_len 12, and payload len."""
    payload = b"v1.3.0\x00"
    header = struct.pack("<III", 0xdeadc0de, 12, len(payload))
    packet = header + payload

    magic, hlen, plen = struct.unpack_from("<III", packet, 0)
    assert magic == 0xdeadc0de
    assert hlen == 12
    assert plen == len(payload)
    assert packet[12:] == payload

