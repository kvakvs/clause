"""Execute tuple record expansion with project-owned OTP results and located semantic cases."""
import json
import pathlib
import re
import subprocess
import sys
from matrix import rejection_lists
from bindings import compile_case
from evidence import run
from immediate import native
from stored import load


def semantic(tool,work,rows):
    """Exercise source legality, capability separation and failed-batch nonpublication in both CLI modes."""
    (work/'out').mkdir(exist_ok=True)
    (work/'out/sentinel').write_bytes(b'preserve')
    for policy,project in rejection_lists():
        for row in rows:
            if row['diagnostic'] or row['capability']:
                compile_case(tool,work,row,policy,project)
    for row in rows:
        if not row['diagnostic'] and not row['capability']:
            run([tool,'--print-types',str(work/(row['name']+'.erl'))])
    assert list((work/'out').iterdir()) == [work/'out/sentinel']


def suites(tool,otp):
    """Parse the original suite and data module with real includes; this is syntax evidence only."""
    options = ['-I',str(otp/'lib/compiler/src'),'--enable-feature','maybe_expr','--disable-feature','compr_assign']
    for app in ['stdlib','kernel','common_test','syntax_tools']:
        options += ['--app-dir',f'{app}={otp/"lib"/app}']
    for name in ['record_SUITE.erl','record_SUITE_data/record_access_in_guards.erl']:
        run([tool,'--preprocess-check',*options,str(otp/'lib/compiler/test'/name)])
        run([tool,'--parse-check',*options,str(otp/'lib/compiler/test'/name)])


def limits(tool,work):
    """Repeated omitted fields and shared nested defaults consume bounded expansion work before publication."""
    fields = ','.join('f'+str(i) for i in range(2000))
    body = '-record(r,{'+fields+'}).\nf() -> '+','.join('#r{}' for _ in range(600))+'.\n'
    path = work/'record_limit.erl'
    path.write_bytes(('-module(record_limit).\n'+body).encode())
    result = subprocess.run([tool,'--emit','obj','--artifact-dir',str(work/'limit-out'),str(path)],capture_output=True,text=True,encoding='utf8',timeout=30)
    assert result.returncode == 1 and 'binding analysis work limit exceeded' in result.stderr,result.stderr
    assert not (work/'limit-out').exists()
    (work/'record_origin.hrl').write_bytes(b'-record(r,{a}).\nf(X) -> X#r.missing.\n')
    (work/'record_origin.erl').write_bytes(b'-module(record_origin).\n-include("record_origin.hrl").\n')
    result = subprocess.run([tool,str(work/'record_origin.erl')],capture_output=True,text=True,encoding='utf8',timeout=30)
    assert result.returncode == 1 and 'record_origin.hrl:2:' in result.stderr and 'undefined record field' in result.stderr,result.stderr


def main():
    tool,cmake,root,directory,settings,config,suffix = sys.argv[1:]
    source,work = pathlib.Path(root),pathlib.Path(directory)
    evidence = load(source,'records',work)
    semantic(tool,work,evidence['cases'])
    limits(tool,work)
    # Semantic project checks use a scratch manifest; restore the reviewed native project afterward.
    stored = source/'tests/fixtures/patternmatch/generated/records/project.toml'
    (work/'project.toml').write_bytes(stored.read_bytes())
    native(tool,cmake,source,work,settings,config,suffix)
    ir = run([tool,'--print-ir',str(work/'answer.erl'),str(work/'client.erl')])
    assert 'record.field' in ir and 'record.test' in ir and 'record.badrecord' in ir
    for name,count in [('eval_once',2),('default_calls',2),('wild_alloc',3)]:
        symbol = 'clausev1_616e73776572_' + name.encode().hex() + '_'
        # The generic body enters a callee exactly once per required evaluation.
        bodies = [b for b in re.findall(r'define .*?\n\}',ir,re.S) if symbol in b.split('{',1)[0] and '.body' in b.split('{',1)[0]]
        assert len(bodies) == 1,(name,symbol)
        generated = re.findall(r'ptr @clausev1_[0-9a-f_]+\.frame\)',bodies[0])
        assert len(generated) == count,(name,generated)
    run([tool,'--print-types',str(work/'answer.erl'),str(work/'client.erl')])
    (work/'evidence.json').write_bytes((json.dumps(evidence,indent=2)+'\n').encode())
    print(f'{evidence["calls"]} record golden calls passed in four policies; {len(evidence["cases"])} semantic cases.')


if __name__ == '__main__':
    main()
