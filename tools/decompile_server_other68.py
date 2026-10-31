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

cidx = d = data
code_pos, _ = find_chunk('CODE')
num_codes = struct.unpack('<I', data[code_pos:code_pos+4])[0]
for ci in range(num_codes):
    ptr = struct.unpack('<I', data[code_pos+4+ci*4:code_pos+8+ci*4])[0]
    name_ptr = struct.unpack('<I', data[ptr:ptr+4])[0]
    cname = data[name_ptr:data.find(b'\x00', name_ptr)].decode('latin1', 'ignore')
    if cname == 'gml_Object_objServer_Other_68':
        clen = struct.unpack('<I', data[ptr+4:ptr+8])[0]
        rel_pos = ptr + 12
        rel_addr = struct.unpack('<i', data[ptr+12:ptr+16])[0]
        abs_off = rel_pos + rel_addr
        
        off = 0
        while off < clen:
            abs_a = abs_off + off
            w1 = struct.unpack('<I', data[abs_a:abs_a+4])[0]
            op = (w1 >> 24) & 0xFF
            t1 = (w1 >> 16) & 0x0F
            line = f"{off:04x}: "
            if abs_a in call_map:
                line += f"CALL {call_map[abs_a]}"
                off += 8
            elif op == 0x41 or op in (0x00,0x01,0x02,0x03,0x04,0x05,0x06):
                line += f"{'POP' if op==0x41 else 'PUSH'}.v {var_map.get(abs_a+4, 'unk')}"
                off += 8
            elif op in (0xc0, 0x84) and t1 == 6:
                sidx = struct.unpack('<I', data[abs_a+4:abs_a+8])[0]
                sval = strings[sidx] if sidx < len(strings) else ''
                line += f"PUSH.s \"{sval}\""
                off += 8
            elif op == 0xc0 and t1 == 0:
                dval = struct.unpack('<d', data[abs_a+4:abs_a+12])[0]
                line += f"PUSH.d {dval}"
                off += 12
            elif op == 0x84 and t1 == 5:
                ival = struct.unpack('<h', data[abs_a+2:abs_a+4])[0]
                line += f"PUSH.i {ival}"
                off += 4
            elif op == 0xc0 and t1 == 7:
                ival = struct.unpack('<i', data[abs_a+4:abs_a+8])[0]
                line += f"PUSH.i32 {ival}"
                off += 8
            elif op == 0x15:
                cond = (w1 >> 8) & 0xFF
                line += f"CMP {cond}"
                off += 4
            elif op in (0xb7, 0xb8, 0xb6):
                j = struct.unpack('<i', data[abs_a+4:abs_a+8])[0]
                opname = 'BT' if op == 0xb7 else ('BF' if op == 0xb8 else 'B')
                line += f"{opname} -> {off + j*4:04x}"
                off += 8
            elif op == 0x07:
                line += "POPZ"
                off += 4
            elif op == 0x9e:
                line += "RET"
                off += 4
            else:
                line += f"OP {op:02x}"
                off += 4
            print(line)
        break
