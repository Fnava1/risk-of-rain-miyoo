import struct

exe = open('butterscotch/tests/array_copy/com.gamemaker.exe', 'rb').read()

# Let's inspect the instructions around 0x28b110 to 0x28b1a0
# In x86_64, each ds_map_add call:
# rdx is string key
# xmm1 / r8 is value
# rcx is mapId
off = 0x28b100
raw = exe[off:off+200]
print("Raw instructions around 0x28b100:")
for i in range(0, len(raw), 16):
    print(f"{off+i:06x}: " + " ".join(f"{b:02x}" for b in raw[i:i+16]))
