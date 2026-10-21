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
vari_pos, vari_len = find_chunk('VARI')

var_count = (vari_len - 12) // 20
gp_vars = {}
for i in range(var_count):
    name_ptr, instance_type, var_id, occurrences, first_addr = struct.unpack('<IIiII', data[vari_pos+12+i*20:vari_pos+32+i*20])
    vname = data[name_ptr:data.find(b'\x00', name_ptr)].decode('latin1', 'ignore')
    if '_gp' in vname or vname in ('z_key', 'x_key', 'c_key', 'v_key', 'jump_key', 'use_key', 'yes_key', 'swap_key'):
        gp_vars[vname] = (var_id, occurrences, first_addr)
        print(f"Var {vname}: id={var_id}, occ={occurrences}")
