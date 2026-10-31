import struct

exe = open('butterscotch/tests/array_copy/com.gamemaker.exe', 'rb').read()

# Let's find references to the string 'buffer' near 0x6390c4
# Let's look at strings around 0x6390c4:
print(exe[0x6390a0:0x639140])
