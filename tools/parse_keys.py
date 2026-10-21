with open('oInit_decomp.txt', 'r', encoding='utf-8') as f:
    lines = f.readlines()

for i, line in enumerate(lines):
    if 'CALL d_ini_read_real' in line:
        # print previous 10 lines
        prev = lines[max(0, i-6):i]
        keys = [l.strip() for l in prev if 'PUSH.s' in l or 'PUSH.i' in l or 'PUSH.d' in l]
        print(f"Call at line {i+1}: {', '.join(keys)}")
