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

objt_pos, _ = find_chunk('OBJT')
num_objts = struct.unpack('<I', data[objt_pos:objt_pos+4])[0]
obj_names = []
for i in range(num_objts):
    ptr = struct.unpack('<I', data[objt_pos+4+i*4:objt_pos+8+i*4])[0]
    name_ptr = struct.unpack('<I', data[ptr:ptr+4])[0]
    obj_names.append(data[name_ptr:data.find(b'\x00', name_ptr)].decode('latin1'))

room_pos, _ = find_chunk('ROOM')
num_rooms = struct.unpack('<I', data[room_pos:room_pos+4])[0]
# Room 39
ptr = struct.unpack('<I', data[room_pos+4+39*4:room_pos+8+39*4])[0]
name_ptr = struct.unpack('<I', data[ptr:ptr+4])[0]
rname = data[name_ptr:data.find(b'\x00', name_ptr)].decode('latin1')
print(f'Room 39 name: {rname}')

# Check instances in Room 39
# In GM:S ROOM chunk, instance subchunk is at offset in room struct
# Let's inspect instances pointer in Room 39
inst_ptr = struct.unpack('<I', data[ptr+0x18:ptr+0x1c])[0]
if inst_ptr > 0:
    num_inst = struct.unpack('<I', data[inst_ptr:inst_ptr+4])[0]
    print(f'Instances count: {num_inst}')
    for i in range(num_inst):
        # instance struct: x(4), y(4), obj_index(4), id(4)
        ioff = inst_ptr + 4 + i * 24 # or 32
        obj_idx = struct.unpack('<I', data[ioff+8:ioff+12])[0]
        if obj_idx < len(obj_names):
            print(f'  Inst {i}: {obj_names[obj_idx]} (idx={obj_idx})')
