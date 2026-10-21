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

code_pos, _ = find_chunk('CODE')
func_pos, _ = find_chunk('FUNC')
strg_pos, _ = find_chunk('STRG')

num_funcs = struct.unpack('<I', data[func_pos:func_pos+4])[0]
funcs = []
for i in range(num_funcs):
    name_ptr = struct.unpack('<I', data[func_pos+4+i*12:func_pos+8+i*12])[0]
    funcs.append(data[name_ptr:data.find(b'\x00', name_ptr)].decode('latin1', 'ignore'))

num_strg = struct.unpack('<I', data[strg_pos:strg_pos+4])[0]
strings = [get_str(struct.unpack('<I', data[strg_pos+4+i*4:strg_pos+8+i*4])[0]) for i in range(num_strg)]

# We want to see all string and function calls in oInit_Create_0
num_codes = struct.unpack('<I', data[code_pos:code_pos+4])[0]
for ci in range(num_codes):
    ptr = struct.unpack('<I', data[code_pos+4+ci*4:code_pos+8+ci*4])[0]
    name_ptr = struct.unpack('<I', data[ptr:ptr+4])[0]
    cname = data[name_ptr:data.find(b'\x00', name_ptr)].decode('latin1', 'ignore')
    if cname == 'gml_Object_oInit_Create_0':
        clen = struct.unpack('<I', data[ptr+4:ptr+8])[0]
        rel_pos = ptr + 12
        rel_addr = struct.unpack('<i', data[ptr+12:ptr+16])[0]
        abs_off = rel_pos + rel_addr
        
        # Let's inspect the stack or instructions around 0x1900 to 0x2900
        off = 0x1930
        stack = []
        while off < 0x2900:
            w1 = struct.unpack('<I', data[abs_off+off:abs_off+off+4])[0]
            op = (w1 >> 24) & 0xFF
            t1 = (w1 >> 16) & 0x0F
            if op in (0xd9, 0xda): # CALL
                fidx = struct.unpack('<I', data[abs_off+off+4:abs_off+off+8])[0]
                argc = (w1) & 0xFFFF
                fname = funcs[fidx] if fidx < len(funcs) else f'func_{fidx}'
                args = [stack.pop() for _ in range(min(argc, len(stack)))]
                args.reverse()
                print(f'{off:04x}: {fname}({", ".join(map(str, args))})')
                stack.append(f'{fname}()')
                off += 8
            elif op in (0xc0, 0x84) and t1 == 6: # String
                sidx = struct.unpack('<I', data[abs_off+off+4:abs_off+off+8])[0]
                sval = strings[sidx] if sidx < len(strings) else ''
                stack.append(f'"{sval}"')
                off += 8
            elif op == 0xc0 and t1 == 0: # Double
                dval = struct.unpack('<d', data[abs_off+off+4:abs_off+off+12])[0]
                stack.append(str(dval))
                off += 12
            elif op == 0x84 and t1 == 5: # Int16
                ival = struct.unpack('<h', data[abs_off+off:abs_off+off+2])[0]
                stack.append(str(ival))
                off += 4
            elif op == 0xc0 and t1 == 7: # Int32
                ival = struct.unpack('<i', data[abs_off+off+4:abs_off+off+8])[0]
                stack.append(str(ival))
                off += 8
            elif op == 0x07: # POP
                if stack:
                    # check what variable it is popped into if it's pop.v or similar
                    stack.pop()
                off += 4
            elif op in (0x03, 0x04): # PUSH variable
                stack.append("var")
                off += 8
            elif op == 0x45: # POP into variable
                v = stack.pop() if stack else "unknown"
                # print(f'{off:04x}: POP -> {v}')
                off += 8
            else:
                off += 4
