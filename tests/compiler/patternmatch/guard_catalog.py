"""Audit every pinned guard signature through owned native results and explicit dependency gates."""
import itertools
import json
import pathlib
import sys
from matrix import rejection_lists
from evidence import digest, run
from immediate import native
from stored import load
from bindings import compile_case

BIG = 1 << 100
BIN = {'bits': '0102', 'length': 16}
BITS = {'bits': 'a0', 'length': 3}
MAP = {'map': [('a', 1), (1, 'true')]}
VALUES = [-BIG, -1, 0, 1, BIG, 1.0, 'a', 'true', 'false', 'nil', ('r', 1), [1],
          {'cons': [1, 'a']}, MAP, BIN, BITS]
BLOCKED = {('node', 0): 'F07/F22/F26', ('node', 1): 'F07/F22/F26',
           ('self', 0): 'F07/F22/F26', ('is_record', 1): 'F17 native records'}


def arguments(name, arity):
    """Cover admitted kinds, bad arguments and useful exact boundaries without unbounded oracle shifts."""
    if name == 'is_record':
        return [[v] for v in VALUES + [('r',), ('r', 1, 2), ('s', 1), ()]]
    if arity == 1:
        return [[v] for v in VALUES]
    if name == 'is_integer' and arity == 3:
        return [[v, lo, hi] for v in [-BIG, -1, 0, 1, BIG, 1.0, 'a']
                for lo, hi in [(-BIG, BIG), (-1, 1), (1, 1), (1, -1), ('a', 1), (0, 1.0)]]
    if name == 'binary_part':
        pairs = [(BIN, 0, 2), (BIN, 2, -2), (BIN, 2, 0), (BIN, -1, 1),
                 (BIN, 0, 3), (BITS, 0, 0), ('a', 0, 0), (BIN, 'a', 1), (BIN, BIG, 1)]
        return [[v, (p, n)] if arity == 2 else [v, p, n] for v, p, n in pairs]
    if name in ['map_get', 'is_map_key']:
        return [[k, m] for k in ['a', 'missing', 1, 1.0, ()] for m in [MAP, {'map': []}, 'a']]
    if name == 'element':
        return [[n, v] for n in [-1, 0, 1, 2, 3, BIG, 'a', 1.0] for v in [('a', 1), (), 'a']]
    if name == 'is_function':
        return [[v, n] for v in [0, 'a', (), MAP] for n in [-BIG, -1, 0, 1, BIG, 'a', 1.0]]
    if name in ['and', 'or', 'xor']:
        return [list(p) for p in itertools.product(['true', 'false', 'a', 0], repeat=2)]
    if name in ['==', '/=', '=<', '<', '>=', '>', '=:=', '=/=', 'min', 'max']:
        return [[v, w] for v, w in zip(VALUES, reversed(VALUES))] + [[v, v] for v in VALUES] + [[1, 1.0], [1.0, 1]]
    if name in ['bsl', 'bsr']:
        return [[v, n] for v in [0, -1, 1, BIG, 'a', 1.0] for n in [-65, -1, 0, 1, 65, 'a', 1.0]]
    return [[v, w] for v, w in [(0, 0), (-1, 1), (BIG, BIG), (-BIG, 3),
                              (1, 0), (1.0, 2), (2, 1.0), ('a', 1), (1, 'a')]]


def main():
    tool,cmake,root,directory,settings,config,suffix=sys.argv[1:]
    source,work=pathlib.Path(root),pathlib.Path(directory)
    evidence=load(source,'guard_catalog',work)
    assert evidence['catalog_sha256'] == digest(source/'tests/fixtures/patternmatch/guards.tsv')
    (work/'out').mkdir(exist_ok=True)
    (work/'out/sentinel').write_bytes(b'preserve')
    for row in evidence['cases']:
        if row['diagnostic'] or row['capability']:
            for policy,project in rejection_lists():
                compile_case(tool,work,row,policy,project)
        else:
            run([tool,'--print-types',str(work/(row['name']+'.erl'))])
    assert list((work/'out').iterdir()) == [work/'out/sentinel']
    (work/'project.toml').write_bytes((source/'tests/fixtures/patternmatch/generated/guard_catalog/project.toml').read_bytes())
    native(tool,cmake,source,work,settings,config,suffix)
    (work/'evidence.json').write_bytes((json.dumps(evidence,indent=2)+'\n').encode())
    print(f'{len(evidence["catalog"])} catalog signatures; {evidence["calls"]} native outcomes in four policies; {len(evidence["cases"])} legality/gate cases.')


if __name__ == '__main__':
    main()
