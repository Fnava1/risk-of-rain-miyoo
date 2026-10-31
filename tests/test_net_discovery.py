import os
import re
import socket
import configparser
import subprocess
import pytest

DISCOVERY_H = os.path.join(os.path.dirname(__file__), "..", "butterscotch", "src", "net", "net_discovery.h")
DISCOVERY_C = os.path.join(os.path.dirname(__file__), "..", "butterscotch", "src", "net", "net_discovery.c")
PREFS_INI = os.path.join(os.path.dirname(__file__), "..", "dist", "Roms", "PORTS", "Games", "Risk of Rain", "Prefs.ini")

def test_net_discovery_header_and_constants():
    """Verify net_discovery.h exists and defines constants and API."""
    assert os.path.exists(DISCOVERY_H), f"Missing {DISCOVERY_H}"
    with open(DISCOVERY_H, "r", encoding="utf-8") as f:
        content = f.read()

    assert "net_discovery_find_host" in content
    assert "net_discovery_broadcast_beacon" in content
    assert "ROR_DISCOVERY_PORT" in content
    assert "11101" in content
    assert "RoR_BEACON:" in content

def test_net_discovery_source_implements_udp_beacon():
    """Verify net_discovery.c implements non-blocking UDP broadcast and listening."""
    assert os.path.exists(DISCOVERY_C), f"Missing {DISCOVERY_C}"
    with open(DISCOVERY_C, "r", encoding="utf-8") as f:
        content = f.read()

    assert "SO_BROADCAST" in content
    assert "recvfrom" in content
    assert "sendto" in content
    assert "ROR_BEACON_MAGIC" in content or "RoR_BEACON:" in content

def test_prefs_ini_has_network_section():
    """Verify Prefs.ini has [Network] section with host_ip fallback."""
    config = configparser.ConfigParser()
    config.read(PREFS_INI, encoding="utf-8")
    assert config.has_section("Network"), "Prefs.ini missing [Network] section"
    assert config.has_option("Network", "host_ip"), "Prefs.ini missing host_ip option"
    assert config.has_option("Network", "port"), "Prefs.ini missing port option"

def test_miyoo_autoconnect_sets_global_client_ip_and_port():
    """Verify miyoo.c sets global.client_ip and global.client_port for objClient."""
    miyoo_c = os.path.join(os.path.dirname(__file__), "..", "butterscotch", "src", "backends", "miyoo.c")
    with open(miyoo_c, "r", encoding="utf-8") as f:
        content = f.read()

    assert "globalScopeInstance" in content, "Missing globalScopeInstance check in miyoo.c"
    assert '"client_ip"' in content, "Missing 'client_ip' variable assignment in miyoo.c"
    assert '"client_port"' in content, "Missing 'client_port' variable assignment in miyoo.c"
    # Ensure mmp_check_network_autoconnect uses Instance_setSelfVar, NOT VM_structSet (which asserts STRUCT_OBJECT_INDEX)
    fn_body = content.split("static void mmp_check_network_autoconnect")[1].split("bool platformHandleEvents")[0]
    assert "VM_structSet" not in fn_body, "mmp_check_network_autoconnect must not call VM_structSet (crashes on non-struct instances)"
    assert "Instance_setSelfVar" in content, "mmp_set_instance_var must use Instance_setSelfVar"

def test_net_discovery_armv7_cross_compilation():
    """Verify net_discovery.c compiles cleanly with ARMv7 toolchain in Docker."""
    if not os.path.exists(DISCOVERY_C):
        pytest.fail("Cannot compile non-existent net_discovery.c")

    cmd = [
        "docker", "exec", "determined_moore",
        "/opt/miyoomini-toolchain/bin/arm-linux-gnueabihf-gcc", "-c",
        "butterscotch/src/net/net_discovery.c",
        "-Ibutterscotch/src", "-O2", "-Wall", "-Wextra", "-Werror", "-o", "/tmp/net_discovery.o"
    ]
    res = subprocess.run(cmd, capture_output=True, text=True)
    assert res.returncode == 0, f"Cross-compilation failed:\n{res.stderr}\n{res.stdout}"
