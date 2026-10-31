import struct

exe = open('butterscotch/tests/array_copy/com.gamemaker.exe', 'rb').read()

off = 0x28b4c0
raw = exe[off:off+100]
for i in range(0, len(raw), 16):
    print(f"{off+i:06x}: " + " ".join(f"{b:02x}" for b in raw[i:i+16]))
