"""mismatch.py OTP.trace CLAUSE.trace: aligned boundary calls whose results differ (needs norm.py output rules)."""
import re, subprocess, sys
from collections import OrderedDict
QUOTED = re.compile(r"^(\s*(?:CALL|RET|EXC) erlang:)'([^']+)'/")

def records(path, known=None):
    with open(path, encoding='latin-1') as f:
        lines = [QUOTED.sub(r"\1\2/", l.rstrip('\r\n')) for l in f]
    lines = [l for l in lines if l.lstrip().startswith(('CALL', 'RET', 'EXC'))]
    recs, open_ = [], []
    for l in lines:
        ind = len(l) - len(l.lstrip())
        word = l.split()
        fn = word[1].split('(')[0]
        if known is not None and fn.startswith('erlang:') and fn not in known:
            continue
        if word[0] == 'CALL':
            open_.append(len(recs))
            recs.append([l.strip(), None])
        elif open_:
            recs[open_.pop()][1] = l.strip()
    return recs

otp = records(sys.argv[1])
known = {r[0].split()[1].split('(')[0] for r in otp}
cl = records(sys.argv[2], known)
for name, recs in (('a.calls', otp), ('b.calls', cl)):
    with open(name, 'w', encoding='latin-1') as f:
        f.write('\n'.join(r[0] for r in recs) + '\n')
out = subprocess.run(['diff', '-U0', 'a.calls', 'b.calls'], capture_output=True, text=True, encoding='latin-1').stdout
hunks = [tuple(int(x or 1) for x in m.groups()) for m in re.finditer(r'^@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@', out, re.M)]
# Walk equal regions between hunks.
ia = ib = 0
seen = OrderedDict()
def compare(n):
    global ia, ib
    for _ in range(n):
        ra, rb = otp[ia], cl[ib]
        if ra[1] != rb[1]:
            key = ra[0].split('(')[0]
            seen.setdefault(key, []).append((ra[0], ra[1], rb[1]))
        ia += 1; ib += 1
for m in re.finditer(r'^@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@', out, re.M):
    a1, an, b1, bn = int(m[1]), int(m[2] if m[2] is not None else 1), int(m[3]), int(m[4] if m[4] is not None else 1)
    start_a = a1 - 1 if an else a1
    compare(start_a - ia)
    ia += an; ib += bn
compare(min(len(otp) - ia, len(cl) - ib))
w = int(sys.argv[3]) if len(sys.argv) > 3 else 300
for key, items in seen.items():
    print(f"### {key}  ({len(items)} aligned calls differ)")
    for c, a, b in items[:3]:
        print("   ", c[:w]); print("   OTP   :", (a or 'none')[:w]); print("   Clause:", (b or 'none')[:w])
