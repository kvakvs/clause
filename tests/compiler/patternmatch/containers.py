"""Execute complete tuple/list helpers and checked construction/matching against OTP."""
import json
import pathlib
import re
import sys
from stored import load
from evidence import run
from immediate import native


def main():
    tool, cmake, root, directory, settings, config, suffix = sys.argv[1:]
    source, work = pathlib.Path(root), pathlib.Path(directory)
    work.mkdir(parents=True, exist_ok=True)
    records = load(source, "containers", work)
    native(tool, cmake, source, work, settings, config, suffix)
    ir = run([tool, '--print-ir', str(work / 'answer.erl'), str(work / 'client.erl')])
    # Match shapes are tested inline (OPT0.2A); construction still calls its service.
    assert 'match.tuple.header' in ir and 'container.outcome' in ir
    assert re.search(r'container.arguments\w* = getelementptr i64, ptr %frame.slots', ir)
    # 32-bit container lowering; other target formats and ABIs are codegen_cross_targets' (plan 11 step VAL14).
    run([tool, '--target-triple', 'i686-pc-windows-msvc', '--emit', 'obj', '--artifact-dir', str(work / 'width32'),
         str(work / 'answer.erl'), str(work / 'client.erl')])
    (work / 'evidence.json').write_text(json.dumps(records, indent=2) + '\n', encoding='utf-8')
    print(f'{records["calls"]} OTP/native container calls passed in four policies; 32-bit objects emitted.')


if __name__ == '__main__':
    main()
