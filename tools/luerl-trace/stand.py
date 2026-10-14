"""stand.py MODULE.erl F/A,G/B BODY.erl: replace those stubs with the hand-written body."""
import re, sys
path, names, body = sys.argv[1], [n.split('/') for n in sys.argv[2].split(',')], open(sys.argv[3]).read()
s = open(path).read()
keep = [l for l in s.split('\n') if not any(re.match(r"^'?%s'?\(.*erlang:error\(\{not_implemented, '?%s'?, %s\}\)" % (n, n, a), l) for n, a in names)]
removed = len(s.split('\n')) - len(keep)
assert removed == len(names), f"removed {removed} of {len(names)}"
open(path, 'w').write('\n'.join(keep) + '\n%% Hand-written stand-ins (tracing).\n' + body)
