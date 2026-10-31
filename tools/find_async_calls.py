import struct

exe = open('butterscotch/tests/array_copy/com.gamemaker.exe', 'rb').read()
target = 0x28b090
for i in range(len(exe)-5):
    if exe[i] == 0xe8:
        rel = struct.unpack('<i', exe[i+1:i+5])[0]
        if i + 5 + rel == target:
            print(f'Call to 0x28b090 at 0x{i:06x}')
