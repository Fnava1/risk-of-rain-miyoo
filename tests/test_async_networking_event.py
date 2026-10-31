import os
import re
import pytest

RUNNER_H = os.path.join(os.path.dirname(__file__), "..", "butterscotch", "src", "runner.h")
RUNNER_C = os.path.join(os.path.dirname(__file__), "..", "butterscotch", "src", "runner.c")

def test_other_async_networking_defined():
    """Verify OTHER_ASYNC_NETWORKING is defined as 68 in runner.h."""
    with open(RUNNER_H, "r", encoding="utf-8") as f:
        content = f.read()

    assert "#define OTHER_ASYNC_NETWORKING 68" in content, "Missing #define OTHER_ASYNC_NETWORKING 68 in runner.h"

def test_runner_declares_network_process():
    """Verify Runner_processNetworkEvents declaration in runner.h."""
    with open(RUNNER_H, "r", encoding="utf-8") as f:
        content = f.read()

    assert "Runner_processNetworkEvents" in content, "Missing Runner_processNetworkEvents declaration in runner.h"

def test_runner_implements_and_calls_network_events():
    """Verify Runner_processNetworkEvents implementation and its hook in Runner_step."""
    with open(RUNNER_C, "r", encoding="utf-8") as f:
        content = f.read()

    assert "void Runner_processNetworkEvents(Runner* runner)" in content, "Missing Runner_processNetworkEvents in runner.c"
    assert "Runner_processNetworkEvents(runner);" in content, "Missing Runner_processNetworkEvents call inside Runner_step"
    assert "net_poll_sockets" in content, "Missing net_poll_sockets call in runner.c"
    assert "EVENT_OTHER" in content and "OTHER_ASYNC_NETWORKING" in content

def test_async_load_includes_socket_key():
    """Verify GM:S 1.4 async_load map includes 'socket' key required by objClient."""
    with open(RUNNER_C, "r", encoding="utf-8") as f:
        content = f.read()

    assert 'safeStrdup("socket")' in content, "Missing 'socket' key in async_load map population"
    assert "eventSock = -1" in content, "Missing eventSock = -1 on NET_EVENT_DATA for GM:S 1.4 compatibility"
    assert "parentServer" in content, "Missing parentServer resolution for server connections"

def test_async_networking_recycles_pools():
    """Verify dsMapPool and gmlBufferPool do not leak slots under network traffic."""
    with open(RUNNER_C, "r", encoding="utf-8") as f:
        content = f.read()

    assert "arrpop(runner->dsMapPool)" in content or "runner->dsMapPool" in content
    assert "arrpop(runner->gmlBufferPool)" in content, "Missing gmlBufferPool recycling/arrpop"
