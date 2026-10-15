"""Execute normalized immediate patterns against OTP in both CLI modes/four policies."""
import json
import struct
import pathlib
import sys
from stored import load
from evidence import run
from matrix import combinations


def token(value):
    """Encode runtime-admitted values independently of native representation and allocation identity."""
    if isinstance(value, tuple):
        return "t(" + ",".join(map(token, value)) + ")" if value else "tuple"
    if isinstance(value, list):
        return "c(" + token(value[0]) + "," + token(value[1:]) + ")" if value else "nil"
    if isinstance(value, dict) and "bits" in value:
        return "b" + str(value["length"]) + ":" + value["bits"]
    if isinstance(value, dict) and "map" in value:
        return "m(" + ",".join(token(tuple(entry)) for entry in value["map"]) + ")"
    if isinstance(value, dict):
        return "c(" + ",".join(map(token, value['cons'])) + ")"
    if isinstance(value, float):
        return "f" + struct.pack(">d", value).hex()
    if isinstance(value, int):
        return f"i{value}"
    return value if value in ["nil", "tuple"] else "a" + value.encode().hex()


def erl(value):
    """Supply independent Erlang terms to the OTP execution oracle."""
    if isinstance(value, tuple):
        return "{" + ",".join(map(erl, value)) + "}"
    if isinstance(value, list):
        return "[" + ",".join(map(erl, value)) + "]"
    if isinstance(value, dict) and "bits" in value:
        data = bytes.fromhex(value['bits'])
        count = value['length']
        full = count // 8
        parts = list(map(str,data[:full]))
        if count % 8:
            parts.append(str(data[full] >> (8-count%8)) + ':' + str(count%8))
        return '<<' + ','.join(parts) + '>>'
    if isinstance(value, dict) and "map" in value:
        return "#{" + ",".join(erl(k) + "=>" + erl(v) for k,v in value["map"]) + "}"
    if isinstance(value, dict):
        return "[" + "|".join(map(erl, value['cons'])) + "]"
    if isinstance(value, float):
        text = repr(value)
        if "e" in text and "." not in text.split("e")[0]:
            text = text.replace("e", ".0e")
        return text
    if isinstance(value, int):
        return str(value)
    return {"nil": "[]", "tuple": "{}"}.get(value, "'" + value + "'")


def native(tool, cmake, source, work, settings, config, suffix):
    """Use the separate real CLI object consumer, covering positional/project publication."""
    for level, extra, mode, project in combinations():
        run([cmake, f"-DTOOL={tool}", f"-DOPTIMIZATION={level}", f"-DEXTRA_OPTIONS={extra}",
             f"-DPROJECT_MODE={project}", f"-DSOURCE_ROOT={source.as_posix()}",
             f"-DTEST_DIR={(work / mode).as_posix()}", f"-DINPUT_ROOT={work.as_posix()}",
             f"-DHOST_SETTINGS={settings}", f"-DHOST_CONFIG={config}", f"-DHOST_SUFFIX={suffix}",
             "-P", str(source / "tests/compiler/codegen/match.cmake")])


def widths(tool, work):
    """Both-width immediate endpoints and promoted literals are object/IR evidence, not foreign execution."""
    records = []
    for bits, triple, bound in [(32, "i686-pc-windows-msvc", 1 << 27), (64, "x86_64-pc-windows-msvc", 1 << 59)]:
        path = work / "width.erl"
        path.write_text(f"-module(width).\nf(-{bound}, {bound-1}) -> ok.\n", encoding="utf-8")
        ir = run([tool, "--target-triple", triple, "--print-ir", str(path)])
        # Immediate literals compare as words inline (OPT0.2A).
        assert f"icmp eq i{bits}" in ir and "exact.outcome" not in ir and "match.mismatch" in ir
        run([tool, "--target-triple", triple, "--emit", "obj", "--artifact-dir", str(work / f"width{bits}"), str(path)])
        path.write_text(f"-module(width).\nf({bound}) -> ok.\n", encoding="utf-8")
        ir = run([tool, "--target-triple", triple, "--print-ir", str(path)])
        assert "integer.outcome" in ir
        run([tool, "--target-triple", triple, "--emit", "obj", "--artifact-dir", str(work / f"promoted{bits}"), str(path)])
        records.append({"bits": bits, "triple": triple, "execution": "not attempted", "endpoints": "accepted", "overflow": "promoted"})
    return records


def main():
    tool, cmake, root, directory, settings, config, suffix = sys.argv[1:]
    source, work = pathlib.Path(root), pathlib.Path(directory)
    work.mkdir(parents=True, exist_ok=True)
    records = load(source, "immediate", work)
    native(tool, cmake, source, work, settings, config, suffix)
    records["widths"] = widths(tool, work)
    (work / "evidence.json").write_text(json.dumps(records, indent=2) + "\n", encoding="utf-8")
    print(f'{records["calls"]} OTP/native immediate calls; four policies; ownership/recovery; both-width objects passed.')


if __name__ == "__main__":
    main()
