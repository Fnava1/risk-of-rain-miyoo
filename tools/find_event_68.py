import struct

exe = open('butterscotch/tests/array_copy/com.gamemaker.exe', 'rb').read()
target = b'\xba\x44\x00\x00\x00'
pos = 0
while True:
    idx = exe.find(target, pos)
    if idx == -1: break
    print(f"Found mov edx, 68 at 0x{idx:06x}")
    pos = idx + 1
