import struct

exe = open('butterscotch/tests/array_copy/com.gamemaker.exe', 'rb').read()

off = 0x28b4d0
for i in range(off, off + 200):
    if exe[i] in [0x48, 0x4c] and exe[i+1] == 0x8d and exe[i+2] in [0x15, 0x0d, 0x05]:
        disp = struct.unpack('<i', exe[i+3:i+7])[0]
        str_addr = i + 7 + disp
        if 0 <= str_addr < len(exe):
            s = exe[str_addr:str_addr+32].split(b'\x00')[0]
            print(f'At 0x{i:06x}: string = "{s.decode("latin1", "ignore")}"')
