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
strg_pos, strg_len = find_chunk('STRG')

num_strg = struct.unpack('<I', data[strg_pos:strg_pos+4])[0]
strings = [get_str(struct.unpack('<I', data[strg_pos+4+i*4:strg_pos+8+i*4])[0]) for i in range(num_strg)]

num_codes = struct.unpack('<I', data[code_pos:code_pos+4])[0]
for ci in range(num_codes):
    ptr = struct.unpack('<I', data[code_pos+4+ci*4:code_pos+8+ci*4])[0]
    name_ptr = struct.unpack('<I', data[ptr:ptr+4])[0]
    cname = data[name_ptr:data.find(b'\x00', name_ptr)].decode('latin1', 'ignore')
    if cname == 'gml_Script_step_player':
        clen = struct.unpack('<I', data[ptr+4:ptr+8])[0]
        rel_pos = ptr + 12
        rel_addr = struct.unpack('<i', data[ptr+12:ptr+16])[0]
        abs_off = rel_pos + rel_addr
        
        # Check where joystick_on or gamepad_button_check is called
        target = b'gamepad_button_check'
        print(f"step_player size: {clen}")
        # Search for gamepad functions in step_player
