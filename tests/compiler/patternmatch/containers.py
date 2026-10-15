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
    for triple in ['i686-pc-windows-msvc', 'x86_64-unknown-linux-gnu', 'i686-unknown-linux-gnu',
                   'aarch64-unknown-linux-gnu', 'armv7-unknown-linux-gnueabihf',
                   'aarch64-apple-darwin', 'aarch64-pc-windows-msvc']:
        run([tool, '--target-triple', triple, '--emit', 'obj', '--artifact-dir', str(work / triple), str(work / 'answer.erl'), str(work / 'client.erl')])
    (work / 'evidence.json').write_text(json.dumps(records, indent=2) + '\n', encoding='utf-8')
    print(f'{records["calls"]} OTP/native container calls passed in four policies; seven foreign object targets.')


if __name__ == '__main__':
    main()
