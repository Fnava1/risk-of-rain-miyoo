import struct

exe = open('butterscotch/tests/array_copy/com.gamemaker.exe', 'rb').read()

pos = 0
while True:
    idx = exe.find(b'buffer\x00', pos)
    if idx == -1: break
    # check nearby for 'size\x00'
    window = exe[max(0, idx-100):min(len(exe), idx+100)]
    if b'size\x00' in window:
        print(f"Found buffer & size around 0x{idx:06x}: {window}")
    pos = idx + 7
