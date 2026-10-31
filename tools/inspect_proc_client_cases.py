import struct

data = open('dist/Roms/PORTS/Games/Risk of Rain/data_mmp.win', 'rb').read()

def find_chunk(name):
    pos = 8
    while pos < len(data):
        cname = data[pos:pos+4].decode('latin1', 'ignore')
        clen = int.from_bytes(data[pos+4:pos+8], 'little')
        if cname == name: return pos + 8, clen
        pos += 8 + clen
    return None, None

code_pos, _ = find_chunk('CODE')
num_codes = struct.unpack('<I', data[code_pos:code_pos+4])[0]
for ci in range(num_codes):
    ptr = struct.unpack('<I', data[code_pos+4+ci*4:code_pos+8+ci*4])[0]
    name_ptr = struct.unpack('<I', data[ptr:ptr+4])[0]
    cname = data[name_ptr:data.find(b'\x00', name_ptr)].decode('latin1', 'ignore')
    if cname == 'gml_Script_proc_client':
        clen = struct.unpack('<I', data[ptr+4:ptr+8])[0]
        rel_pos = ptr + 12
        rel_addr = struct.unpack('<i', data[ptr+12:ptr+16])[0]
        abs_off = rel_pos + rel_addr
        raw = data[abs_off:abs_off+clen]
        # Look for CMP with sid
        # In GM:S bytecode, switch-case on sid:
        # PUSHI val, CMP cond 3 (==), BT
        print(f'proc_client sid cases:')
        for o in range(0x00c0, min(clen, 0x1000), 4):
            w = struct.unpack('<I', raw[o:o+4])[0]
            op = (w >> 24) & 0xff
            if op == 0x84:
                val = w & 0xffff
                # check if next is CMP
                if o + 4 < clen:
                    w_next = struct.unpack('<I', raw[o+4:o+8])[0]
                    if ((w_next >> 24) & 0xff) == 0x15:
                        print(f'Case sid == {val} at offset {o:04x}')
