"""norm.py OTP.trace CLAUSE.trace: write *.norm files comparable with diff."""
import re, sys
QUOTED = re.compile(r"^(\s*(?:CALL|RET|EXC) erlang:)'([^']+)'/")

def load(path):
    with open(path, encoding='latin-1') as f:
        lines = [QUOTED.sub(r"\1\2/", l.rstrip('\r\n')) for l in f]
    return [l for l in lines if not l.lstrip().startswith(('FIRST', '. ', 'RAISE'))]

def name(line):
    word = line.split()
    return word[0], (word[1].split('(')[0] if len(word) > 1 else '')

otp, cl = load(sys.argv[1]), load(sys.argv[2])
known = {name(l)[1] for l in otp if name(l)[0] == 'CALL'}
# BEAM inlines guard BIFs and operators, so OTP never traces them; drop erlang calls OTP never shows.
cl = [l for l in cl if not (name(l)[1].startswith('erlang:') and name(l)[1] not in known)]
for path, lines in ((sys.argv[1], otp), (sys.argv[2], cl)):
    with open(path + '.norm', 'w', encoding='latin-1') as f:
        f.write('\n'.join(l.strip() for l in lines) + '\n')
