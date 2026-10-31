import os
import re
import pytest

def test_computed_goto_macro_definitions():
    """Verify butterscotch/src/vm.c defines dual-mode computed goto macros."""
    vm_c_path = os.path.join(os.path.dirname(__file__), "..", "butterscotch", "src", "vm.c")
    with open(vm_c_path, "r", encoding="utf-8") as f:
        content = f.read()

    assert "VM_USE_COMPUTED_GOTO" in content, "Missing VM_USE_COMPUTED_GOTO macro check"
    assert "TARGET(" in content, "Missing TARGET(op) macro abstraction"
    assert "DISPATCH()" in content, "Missing DISPATCH() macro abstraction"
    assert "dispatch_table" in content, "Missing dispatch_table array definition"

def test_computed_goto_dispatch_table_covers_all_opcodes():
    """Verify dispatch table maps all required GameMaker opcodes."""
    vm_c_path = os.path.join(os.path.dirname(__file__), "..", "butterscotch", "src", "vm.c")
    with open(vm_c_path, "r", encoding="utf-8") as f:
        content = f.read()

    required_opcodes = [
        "OP_PUSH", "OP_PUSHLOC", "OP_PUSHGLB", "OP_PUSHBLTN", "OP_PUSHI",
        "OP_POP", "OP_POPZ", "OP_ADD", "OP_SUB", "OP_MUL", "OP_DIV",
        "OP_REM", "OP_MOD", "OP_AND", "OP_OR", "OP_XOR", "OP_SHL", "OP_SHR",
        "OP_NEG", "OP_NOT", "OP_CONV", "OP_CMP", "OP_DUP", "OP_B", "OP_BT",
        "OP_BF", "OP_CALL", "OP_CALLV", "OP_RET", "OP_EXIT", "OP_PUSHENV",
        "OP_POPENV", "OP_BREAK"
    ]

    for op in required_opcodes:
        assert f"[{op}]" in content or f"TARGET({op})" in content, f"Opcode {op} not registered in computed goto dispatch"
