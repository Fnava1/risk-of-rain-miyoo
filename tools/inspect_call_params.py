import struct

exe = open('butterscotch/tests/array_copy/com.gamemaker.exe', 'rb').read()

for call_addr in [0x28f21b, 0x28fe1f, 0x290b07, 0x29149b]:
    print(f"=== Call at 0x{call_addr:06x} ===")
    raw = exe[call_addr-40:call_addr+5]
    for i in range(0, len(raw), 16):
        print(" ".join(f"{b:02x}" for b in raw[i:i+16]))
