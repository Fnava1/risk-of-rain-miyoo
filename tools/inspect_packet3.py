import struct

data = bytearray(open('dist/Roms/PORTS/Games/Risk of Rain/data_mmp.win', 'rb').read())

def find_chunk(name):
    pos = 8
    while pos < len(data):
        cname = data[pos:pos+4].decode('latin1', 'ignore')
        clen = int.from_bytes(data[pos+4:pos+8], 'little')
        if cname == name: return pos + 8, clen
        pos += 8 + clen
    return None, None

def get_str(ptr): return data[ptr+4:data.find(b'\x00', ptr+4)].decode('latin1', 'ignore')

strg_pos, strg_len = find_chunk('STRG')
num_strg = struct.unpack('<I', data[strg_pos:strg_pos+4])[0]
strings = [get_str(struct.unpack('<I', data[strg_pos+4+i*4:strg_pos+8+i*4])[0]) for i in range(num_strg)]

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

code_pos, _ = find_chunk('CODE')
num_codes = struct.unpack('<I', data[code_pos:code_pos+4])[0]
for i in range(num_codes):
    ptr = struct.unpack('<I', data[code_pos+4+i*4:code_pos+8+i*4])[0]
    name_ptr = struct.unpack('<I', data[ptr:ptr+4])[0]
    cname = data[name_ptr:data.find(b'\x00', name_ptr)].decode('latin1', 'ignore')
    if cname == 'gml_Script_proc_server':
        rel_pos = ptr + 12
        rel_addr = struct.unpack('<i', data[ptr+12:ptr+16])[0]
        abs_off = rel_pos + rel_addr
        clen = struct.unpack('<I', data[ptr+4:ptr+8])[0]
        for off in range(0x00d0, 0x0220, 4):
            w = struct.unpack('<I', data[abs_off+off:abs_off+off+4])[0]
            op = (w >> 24) & 0xff
            if op == 0x84:
                val = struct.unpack('<h', data[abs_off+off+2:abs_off+off+4])[0]
                print(f'{off:04x}: PUSHI {val}')
            elif op == 0x15:
                print(f'{off:04x}: CMP condition {(w >> 8) & 0xff}')
            elif op in (0xb7, 0xb8, 0xb6):
                j = struct.unpack('<i', data[abs_off+off+4:abs_off+off+8])[0]
                print(f'{off:04x}: JUMP -> {off + j*4:04x}')
            elif abs_off + off in call_map:
                print(f'{off:04x}: CALL {call_map[abs_off+off]}')
            elif abs_off + off + 4 in var_map:
                print(f'{off:04x}: VAR {var_map[abs_off+off+4]}')
        break
