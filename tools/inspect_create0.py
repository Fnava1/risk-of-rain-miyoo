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
for ci in range(num_codes):
    ptr = struct.unpack('<I', data[code_pos+4+ci*4:code_pos+8+ci*4])[0]
    name_ptr = struct.unpack('<I', data[ptr:ptr+4])[0]
    cname = data[name_ptr:data.find(b'\x00', name_ptr)].decode('latin1', 'ignore')
    if cname == 'gml_Object_objClient_Create_0':
        clen = struct.unpack('<I', data[ptr+4:ptr+8])[0]
        rel_pos = ptr + 12
        rel_addr = struct.unpack('<i', data[ptr+12:ptr+16])[0]
        abs_off = rel_pos + rel_addr
        raw = data[abs_off:abs_off+clen]
        print('Create_0 variables:')
        for o in range(0, clen, 4):
            vname = var_map.get(abs_off + o + 4, '')
            val = struct.unpack('<h', raw[o:o+2])[0]
            if vname:
                print(f'{o:04x}: {vname} (val={val})')
