#!/usr/bin/env python3
"""
Risk of Rain GML Bytecode & Network Script Disassembler.
Decompiles objClient, objServer, oHostClient, and networking scripts
from data.win or data_mmp.win to analyze network flows and state machines.
"""

import sys
import os
import struct

def find_win_file():
    candidates = [
        r"C:\Program Files (x86)\Steam\steamapps\common\Risk of Rain\data.win",
        os.path.join(os.path.dirname(__file__), "..", "dist", "Roms", "PORTS", "Games", "Risk of Rain", "data_mmp.win"),
        os.path.join(os.path.dirname(__file__), "..", "dist", "Roms", "PORTS", "Games", "Risk of Rain", "data.win")
    ]
    for c in candidates:
        if os.path.isfile(c):
            return c
    return None

def main():
    target_path = sys.argv[1] if len(sys.argv) > 1 and not sys.argv[1].startswith("-") else find_win_file()
    if not target_path or not os.path.isfile(target_path):
        print("Error: No data.win file found to inspect.")
        return 1

    with open(target_path, "rb") as f:
        data = f.read()

    def find_chunk(name):
        pos = 8
        while pos < len(data):
            cname = data[pos:pos+4].decode('latin1', 'ignore')
            clen = int.from_bytes(data[pos+4:pos+8], 'little')
            if cname == name: return pos + 8, clen
            pos += 8 + clen
        return None, None

    strg_pos, strg_len = find_chunk('STRG')
    def get_str(ptr): return data[ptr+4:data.find(b'\x00', ptr+4)].decode('latin1', 'ignore')

    vari_pos, vari_len = find_chunk('VARI')
    var_count = (vari_len - 12) // 20
    var_map = {}
    for i in range(var_count):
        np, it, vid, occ, fa = struct.unpack('<IIiII', data[vari_pos+12+i*20:vari_pos+32+i*20])
        vname = data[np:data.find(b'\x00', np)].decode('latin1', 'ignore')
        if occ == 0 or fa == 0xffffffff: continue
        addr = fa
        for _ in range(occ):
            var_map[addr+4] = vname
            op = struct.unpack('<I', data[addr+4:addr+8])[0]
            delta = op & 0x07ffffff
            if delta == 0: break
            addr += delta

    func_pos, _ = find_chunk('FUNC')
    num_funcs = struct.unpack('<I', data[func_pos:func_pos+4])[0]
    call_map = {}
    for i in range(num_funcs):
        np, occ, fa = struct.unpack('<III', data[func_pos+4+i*12:func_pos+16+i*12])
        fname = data[np:data.find(b'\x00', np)].decode('latin1', 'ignore')
        if occ == 0 or fa == 0xffffffff: continue
        addr = fa
        for _ in range(occ):
            call_map[addr] = fname
            op = struct.unpack('<I', data[addr+4:addr+8])[0]
            delta = op & 0x07ffffff
            if delta == 0: break
            addr += delta

    code_pos, _ = find_chunk('CODE')
    num_codes = struct.unpack('<I', data[code_pos:code_pos+4])[0]
    code_map = {}
    for ci in range(num_codes):
        ptr = struct.unpack('<I', data[code_pos+4+ci*4:code_pos+8+ci*4])[0]
        name_ptr = struct.unpack('<I', data[ptr:ptr+4])[0]
        cname = data[name_ptr:data.find(b'\x00', name_ptr)].decode('latin1', 'ignore')
        clen = struct.unpack('<I', data[ptr+4:ptr+8])[0]
        rel_pos = ptr + 12
        rel_addr = struct.unpack('<i', data[ptr+12:ptr+16])[0]
        abs_off = rel_pos + rel_addr
        code_map[cname] = (abs_off, clen)

    def decompile(cname):
        if cname not in code_map:
            print(f"Script {cname} not found.")
            return
        abs_off, clen = code_map[cname]
        raw = data[abs_off:abs_off+clen]
        print(f'=== {cname} (len={clen}) ===')
        o = 0
        while o < clen:
            w = struct.unpack('<I', raw[o:o+4])[0]
            opcode = (w >> 24) & 0xFF
            vname = var_map.get(abs_off + o + 4, '')
            fname = call_map.get(abs_off + o, '')
            s = f'{o:04x}: {w:08x} op={opcode:02x}'
            if vname: s += f' var={vname}'
            if fname: s += f' func={fname}'
            if opcode in (0x07, 0xc0, 0x08) and (w & 0xffff) == 0x56:
                str_idx = struct.unpack('<I', raw[o+4:o+8])[0]
                if str_idx < strg_len // 4:
                    try:
                        s_ptr = struct.unpack('<I', data[strg_pos+4+str_idx*4:strg_pos+8+str_idx*4])[0]
                        s += f' str="{get_str(s_ptr)}"'
                    except Exception:
                        pass
            print(s)
            o += 4

    requested = sys.argv[2:] if len(sys.argv) > 2 else [
        'gml_Object_objClient_Other_68',
        'gml_Object_objClient_Alarm_1'
    ]
    for r in requested:
        decompile(r)

    return 0

if __name__ == '__main__':
    sys.exit(main())
