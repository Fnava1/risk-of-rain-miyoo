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

strg_pos, _ = find_chunk('STRG')
num_strg = struct.unpack('<I', data[strg_pos:strg_pos+4])[0]
strings = [get_str(struct.unpack('<I', data[strg_pos+4+i*4:strg_pos+8+i*4])[0]) for i in range(num_strg)]

keywords = ['water', 'swim', 'drown', 'underwater', 'tomb', 'mud', 'tar', 'slime', 'sticky', 'geyser', 'gravity', 'friction']
for i, s in enumerate(strings):
    for kw in keywords:
        if kw in s.lower() and len(s) < 40:
            print(f"Str {i}: '{s}'")
            break
