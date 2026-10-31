import os
import re
import pytest

BUILTINS_C = os.path.join(os.path.dirname(__file__), "..", "butterscotch", "src", "vm_builtins.c")
BUILTINS_H = os.path.join(os.path.dirname(__file__), "..", "butterscotch", "src", "vm_builtins.h")

def test_network_builtins_registered():
    """Assert all 5 GameMaker 1.4 network builtins are registered in vm_builtins.c."""
    with open(BUILTINS_C, "r", encoding="utf-8") as f:
        content = f.read()

    required_builtins = [
        "network_create_socket",
        "network_connect",
        "network_send_packet",
        "network_create_server",
        "network_destroy",
    ]
    for b in required_builtins:
        pattern = rf'VM_registerBuiltin\(\s*ctx\s*,\s*"{b}"\s*,'
        assert re.search(pattern, content), f"Missing builtin registration for {b}"

def test_network_builtins_implemented():
    """Assert handler functions for network builtins exist in vm_builtins.c."""
    with open(BUILTINS_C, "r", encoding="utf-8") as f:
        content = f.read()

    required_handlers = [
        "builtin_network_create_socket",
        "builtin_network_connect",
        "builtin_network_send_packet",
        "builtin_network_create_server",
        "builtin_network_destroy",
    ]
    for handler in required_handlers:
        assert handler in content, f"Missing implementation for {handler}"

def test_network_send_packet_integrates_gml_buffer():
    """Assert network_send_packet queries gmlBufferGet and calls net_send_packet."""
    with open(BUILTINS_C, "r", encoding="utf-8") as f:
        content = f.read()

    assert "gmlBufferGet" in content
    assert "net_send_packet" in content
    assert "net/net_socket.h" in content
