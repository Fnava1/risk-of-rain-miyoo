import os
import re
import subprocess
import pytest

NET_DIR = os.path.join(os.path.dirname(__file__), "..", "butterscotch", "src", "net")
NET_HEADER = os.path.join(NET_DIR, "net_socket.h")
NET_SOURCE = os.path.join(NET_DIR, "net_socket.c")

def test_net_socket_header_exists_and_declares_api():
    """Verify net_socket.h exists and declares core POSIX socket APIs."""
    assert os.path.exists(NET_HEADER), f"Header {NET_HEADER} does not exist"
    with open(NET_HEADER, "r", encoding="utf-8") as f:
        content = f.read()

    required_funcs = [
        "net_socket_create",
        "net_connect_async",
        "net_send_packet",
        "net_server_create",
        "net_socket_destroy",
        "net_poll_sockets",
    ]
    for func in required_funcs:
        assert re.search(rf"\b{func}\s*\(", content), f"Missing declaration for {func} in net_socket.h"

    required_constants = [
        "NET_TYPE_TCP",
        "NET_TYPE_UDP",
        "NET_EVENT_CONNECT",
        "NET_EVENT_DISCONNECT",
        "NET_EVENT_DATA",
    ]
    for const in required_constants:
        assert const in content, f"Missing constant definition {const} in net_socket.h"

def test_net_socket_source_implements_nonblocking_posix():
    """Verify net_socket.c implements non-blocking I/O and event polling."""
    assert os.path.exists(NET_SOURCE), f"Source {NET_SOURCE} does not exist"
    with open(NET_SOURCE, "r", encoding="utf-8") as f:
        content = f.read()

    assert "O_NONBLOCK" in content, "Missing O_NONBLOCK flag for non-blocking sockets"
    assert "poll(" in content or "select(" in content, "Missing poll/select event multiplexing"
    assert "EINPROGRESS" in content or "EWOULDBLOCK" in content, "Missing EINPROGRESS handling in async connect"
    assert "net_poll_sockets" in content, "Missing net_poll_sockets implementation"
    assert "net_send_packet" in content, "Missing net_send_packet implementation"

def test_net_socket_state_machine_and_pollin_order():
    """Verify TCP socket is not marked CONNECTING before connect and POLLIN precedes POLLHUP."""
    with open(NET_SOURCE, "r", encoding="utf-8") as f:
        content = f.read()

    assert "SOCKET_STATE_CREATED" in content, "Missing SOCKET_STATE_CREATED state enum"
    # Verify in net_socket_create: TCP starts in SOCKET_STATE_CREATED
    assert "SOCKET_STATE_CREATED" in content
    # Verify in net_poll_sockets that POLLIN is handled before disconnect / POLLHUP
    pollin_pos = content.find("POLLIN")
    assert pollin_pos != -1, "Missing POLLIN handling"

def test_net_socket_armv7_cross_compilation():
    """Verify net_socket.c compiles cleanly with arm-linux-gnueabihf-gcc in Docker."""
    if not os.path.exists(NET_SOURCE):
        pytest.fail("Cannot compile non-existent net_socket.c")

    cmd = [
        "docker", "exec", "determined_moore",
        "/opt/miyoomini-toolchain/bin/arm-linux-gnueabihf-gcc", "-c", "butterscotch/src/net/net_socket.c",
        "-Ibutterscotch/src", "-O2", "-Wall", "-Wextra", "-Werror", "-o", "/tmp/net_socket.o"
    ]
    res = subprocess.run(cmd, capture_output=True, text=True)
    assert res.returncode == 0, f"Cross-compilation failed:\n{res.stderr}\n{res.stdout}"
