"""firsts.py CLAUSE.trace: functions in first-entry order that are Clause stubs, unavailable BIFs or OTP NIF stubs."""
import os, subprocess, sys
order = []
with open(sys.argv[1], encoding='latin-1') as f:
    for line in f:
        s = line.strip()
        if s.startswith('FIRST '):
            order.append(s[6:])
stubs = set(open('stubs.txt').read().split())
library = {os.path.splitext(n)[0] for d in ('erts', 'kernel', 'stdlib') for n in os.listdir(f'F:/CLAUSE/clau/library/{d}')}
otp_mods = sorted({n.split(':')[0] for n in order if ':' in n and not n.startswith('(')} - library)
open('otp_mods.txt', 'w').write('\n'.join(otp_mods) + '\n')
subprocess.run(['erl', '-noshell', '-pa', 'otp_ebin', '-run', 'nifs', 'main', 'otp_mods.txt', 'nifs.txt'], check=True)
nifs = set(open('nifs.txt').read().split())
for i, n in enumerate(order):
    if n in stubs:
        print(i, n, 'clause-stub' if not n.startswith('erlang:') else 'erlang-unavailable')
    elif n in nifs:
        print(i, n, 'otp-nif-stub')
print('modules compiled from OTP and entered:', ' '.join(otp_mods), file=sys.stderr)

# Context: the nearest boundary CALL before each flagged first entry.
