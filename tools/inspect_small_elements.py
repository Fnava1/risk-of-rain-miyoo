import struct

data = bytearray(open('dist/Roms/PORTS/Games/Risk of Rain/data_mmp.win', 'rb').read())
def find_chunk(name):
    pos = 8
    while pos < len(data):
        ch = data[pos:pos+4].decode('latin1', 'ignore')
        ln = int.from_bytes(data[pos+4:pos+8], 'little')
        if ch == name: return pos + 8, ln
        pos += 8 + ln
    return None, None

def get_str(ptr):
    return data[ptr+4:data.find(b'\x00', ptr+4)].decode('latin1', 'ignore')

code_pos, _ = find_chunk('CODE')
func_pos, _ = find_chunk('FUNC')
vari_pos, vari_len = find_chunk('VARI')
strg_pos, _ = find_chunk('STRG')

num_strg = struct.unpack('<I', data[strg_pos:strg_pos+4])[0]
strings = [get_str(struct.unpack('<I', data[strg_pos+4+i*4:strg_pos+8+i*4])[0]) for i in range(num_strg)]

num_funcs = struct.unpack('<I', data[func_pos:func_pos+4])[0]
func_entries = []
for i in range(num_funcs):
    name_ptr, occurrences, first_addr = struct.unpack('<III', data[func_pos+4+i*12:func_pos+16+i*12])
    fname = data[name_ptr:data.find(b'\x00', name_ptr)].decode('latin1', 'ignore')
    func_entries.append((fname, occurrences, first_addr))

call_map = {}
for fidx, (fname, occurrences, first_addr) in enumerate(func_entries):
    if occurrences == 0 or first_addr == 0xFFFFFFFF: continue
    addr = first_addr
    for occ in range(occurrences):
        call_map[addr] = fname
        if occ + 1 < occurrences:
            operand = struct.unpack('<I', data[addr+4:addr+8])[0]
            delta = operand & 0x07FFFFFF
            if delta == 0: break
            addr += delta

var_count = (vari_len - 12) // 20
vari_entries = []
for i in range(var_count):
    name_ptr, instance_type, var_id, occurrences, first_addr = struct.unpack('<IIiII', data[vari_pos+12+i*20:vari_pos+32+i*20])
    vname = data[name_ptr:data.find(b'\x00', name_ptr)].decode('latin1', 'ignore')
    vari_entries.append((vname, occurrences, first_addr))

var_map = {}
for vidx, (vname, occurrences, first_addr) in enumerate(vari_entries):
    if occurrences == 0 or first_addr == 0xFFFFFFFF: continue
    addr = first_addr
    for occ in range(occurrences):
        var_map[addr+4] = vname
        if occ + 1 < occurrences:
            operand = struct.unpack('<I', data[addr+4:addr+8])[0]
            delta = operand & 0x07FFFFFF
            if delta == 0: break
            addr += delta

def disasm_code(target_name):
    num_codes = struct.unpack('<I', data[code_pos:code_pos+4])[0]
    for ci in range(num_codes):
        ptr = struct.unpack('<I', data[code_pos+4+ci*4:code_pos+8+ci*4])[0]
        name_ptr = struct.unpack('<I', data[ptr:ptr+4])[0]
        cname = data[name_ptr:data.find(b'\x00', name_ptr)].decode('latin1', 'ignore')
        if cname == target_name:
            clen = struct.unpack('<I', data[ptr+4:ptr+8])[0]
            rel_pos = ptr + 12
            rel_addr = struct.unpack('<i', data[ptr+12:ptr+16])[0]
            abs_off = rel_pos + rel_addr
            print(f"\n==========================================")
            print(f"=== {cname} (len={clen}) ===")
            off = 0
            while off < clen:
                abs_a = abs_off + off
                w1 = struct.unpack('<I', data[abs_a:abs_a+4])[0]
                op = (w1 >> 24) & 0xFF
                t1 = (w1 >> 16) & 0x0F
                line = f"{off:04x}: "
                if abs_a in call_map:
                    fname = call_map[abs_a]
                    argc = w1 & 0xFFFF
                    line += f"CALL {fname} (argc={argc})"
                    off += 8
                elif abs_a+4 in var_map:
                    vname = var_map[abs_a+4]
                    line += f"VAR {vname} (op={hex(op)})"
                    off += 8
                elif op in (0xc0, 0x84) and t1 == 6:
                    sidx = struct.unpack('<I', data[abs_a+4:abs_a+8])[0]
                    sval = strings[sidx] if sidx < len(strings) else ''
                    line += f'PUSH.s "{sval}"'
                    off += 8
                elif op in (0xc0, 0x84) and t1 == 5:
                    dval = struct.unpack('<d', data[abs_a+4:abs_a+12])[0]
                    line += f"PUSH.d {dval}"
                    off += 12
                elif op in (0xc0, 0x84) and t1 == 0:
                    ival = struct.unpack('<i', data[abs_a+4:abs_a+8])[0]
                    line += f"PUSH.i {ival}"
                    off += 8
                elif op == 0x84:
                    hval = struct.unpack('<h', data[abs_a:abs_a+2])[0]
                    line += f"PUSHI.s {hval}"
                    off += 4
                elif op == 0x07:
                    line += "POP"
                    off += 4
                else:
                    line += f"OP {hex(op)}"
                    off += 4
                print(line)

targets = [
    "gml_Script_instance_nearest_var",
    "gml_Script_instance_nearest_var_yoyo"
]

for t in targets:
    disasm_code(t)
