import struct

data = open('dist/Roms/PORTS/Games/Risk of Rain/data_mmp.win', 'rb').read()

def find_chunk(name):
    pos = 8
    while pos < len(data):
        chunk_name = data[pos:pos+4].decode('latin1', 'ignore')
        chunk_len = int.from_bytes(data[pos+4:pos+8], 'little')
        if chunk_name == name:
            return pos + 8, chunk_len
        pos += 8 + chunk_len
    return None, None

code_pos, _ = find_chunk('CODE')
func_pos, _ = find_chunk('FUNC')

def get_func(idx):
    ptr = int.from_bytes(data[func_pos+4+idx*12:func_pos+8+idx*12], 'little')
    return data[ptr:data.find(b'\x00', ptr)].decode('latin1')

ptr = struct.unpack('<I', data[code_pos+4+497*4:code_pos+8+497*4])[0]
code_len = struct.unpack('<I', data[ptr+4:ptr+8])[0]
rel_pos = ptr + 12
rel_addr = struct.unpack('<i', data[ptr+12:ptr+16])[0]
abs_offset = rel_pos + rel_addr

funcs = set()
off = 0
while off < code_len:
    w1 = struct.unpack('<I', data[abs_offset+off:abs_offset+off+4])[0]
    op = (w1 >> 24) & 0xFF
    if op == 0xDA: # Call
        fidx = struct.unpack('<I', data[abs_offset+off+4:abs_offset+off+8])[0]
        funcs.add(get_func(fidx))
        off += 8
    else:
        off += 4

print('Functions called in oMenu_Step_0:')
for fn in sorted(funcs):
    print(' ', fn)
