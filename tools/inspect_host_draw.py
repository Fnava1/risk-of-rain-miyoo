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

strg_pos, strg_len = find_chunk('STRG')
def get_str(ptr): return data[ptr+4:data.find(b'\x00', ptr+4)].decode('latin1', 'ignore')

code_pos, _ = find_chunk('CODE')
num_codes = struct.unpack('<I', data[code_pos:code_pos+4])[0]
for ci in range(num_codes):
    ptr = struct.unpack('<I', data[code_pos+4+ci*4:code_pos+8+ci*4])[0]
    name_ptr = struct.unpack('<I', data[ptr:ptr+4])[0]
    cname = data[name_ptr:data.find(b'\x00', name_ptr)].decode('latin1', 'ignore')
    if 'host' in cname.lower():
        clen = struct.unpack('<I', data[ptr+4:ptr+8])[0]
        rel = struct.unpack('<i', data[ptr+12:ptr+16])[0]
        abs_off = ptr + 12 + rel
        for o in range(0, clen, 4):
            w = struct.unpack('<I', data[abs_off+o:abs_off+o+4])[0]
            op = (w >> 24) & 0xff
            if op == 0x08:
                sptr = struct.unpack('<I', data[abs_off+o+4:abs_off+o+8])[0]
                s = get_str(sptr)
                if len(s) > 1: print(f'{cname}: "{s}"')
