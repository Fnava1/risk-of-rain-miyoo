import struct

data = bytearray(open('dist/Roms/PORTS/Games/Risk of Rain/data_mmp.win', 'rb').read())
def find_chunk(name):
    pos = 8
    while pos < len(data):
        chunk_name = data[pos:pos+4].decode('latin1', 'ignore')
        chunk_len = int.from_bytes(data[pos+4:pos+8], 'little')
        if chunk_name == name:
            return pos + 8, chunk_len
        pos += 8 + chunk_len
    return None, None

def get_str(ptr):
    return data[ptr+4:data.find(b'\x00', ptr+4)].decode('latin1', 'ignore')

code_pos, code_len = find_chunk('CODE')
strg_pos, strg_len = find_chunk('STRG')
vari_pos, vari_len = find_chunk('VARI')

num_strg = struct.unpack('<I', data[strg_pos:strg_pos+4])[0]
strings = [get_str(struct.unpack('<I', data[strg_pos+4+i*4:strg_pos+8+i*4])[0]) for i in range(num_strg)]

var_count = (vari_len - 12) // 20
variables = []
for i in range(var_count):
    name_ptr, instance_type, var_id, occurrences, first_addr = struct.unpack('<IIiII', data[vari_pos+12+i*20:vari_pos+32+i*20])
    vname = data[name_ptr:data.find(b'\x00', name_ptr)].decode('latin1', 'ignore')
    variables.append(vname)

num_codes = struct.unpack('<I', data[code_pos:code_pos+4])[0]
for ci in range(num_codes):
    ptr = struct.unpack('<I', data[code_pos+4+ci*4:code_pos+8+ci*4])[0]
    name_ptr = struct.unpack('<I', data[ptr:ptr+4])[0]
    cname = data[name_ptr:data.find(b'\x00', name_ptr)].decode('latin1', 'ignore')
    if cname == 'gml_Script_step_player':
        clen = struct.unpack('<I', data[ptr+4:ptr+8])[0]
        print(f"step_player code length: {clen}")
        # Search for z_key or z_key_gp or key_player
