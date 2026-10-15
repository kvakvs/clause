"""Inspect CLI-produced foreign objects with LLVM tools; never execute them."""
import pathlib
import re
import subprocess
import sys
from differential import run


def rejected(tool, source, root, triple, reason):
    """Require a useful target error and no artifact publication."""
    result = subprocess.run([tool, "--target-triple", triple, "--emit", "obj", "--artifact-dir", str(root),
                             str(source)], capture_output=True, text=True, timeout=30)
    assert result.returncode == 1 and reason in result.stderr and not result.stdout, result
    assert not root.exists(), f"failed target published {root}"


def inspect(tool, readobj, nm, work, triple, bits, format_name, architecture):
    """Compare target headers, native symbol surfaces, immediate tags and descriptors."""
    minimum, maximum = -(1 << (bits - 5)), (1 << (bits - 5)) - 1
    answer = work / "answer.erl"
    client = work / "client.erl"
    answer.write_text(f"-module(answer). -export([value/0, identity/1, minimum/0, maximum/0, atom/0]).\n"
                      f"value() -> 42. identity(X) -> X. minimum() -> {minimum}. maximum() -> {maximum}. atom() -> true.\n",
                      encoding="utf-8")
    client.write_text("-module(client). -export([value/0]). value() -> answer:identity(answer:value()).\n",
                      encoding="utf-8")
    extension = ".obj" if format_name == "COFF" else ".o"
    for level in ["-O0", "-O2", "-Os"]:
        root = work / triple / level
        for kind in ["obj", "llvm-ir"]:
            run([tool, "--target-triple", triple, level, "--emit", kind, "--artifact-dir", str(root / kind),
                 str(answer), str(client)])
        objects = sorted((root / "obj").iterdir())
        assert len(objects) == 2 and all(p.suffix == extension for p in objects)
        reports = []
        for path in objects:
            headers = run([readobj, "--file-headers", "--symbols", str(path)]).stdout
            assert f"Format: {format_name}" in headers, headers
            assert f"Arch: {architecture}\n" in headers, headers
            assert f"AddressSize: {bits}bit" in headers, headers
            imports = run([nm, "--undefined-only", str(path)]).stdout
            exports = run([nm, "--defined-only", "--extern-only", str(path)]).stdout
            assert "CLAUSE_register_module_v4" in imports, imports
            assert "CLAUSE_return_v1" in imports and "CLAUSE_invoke_v1" in imports, imports
            assert ".register" in exports and "clausev1_" in exports, exports
            reports.append(headers + imports + exports)
            if "616e73776572" in path.name:
                assert "CLAUSE_atom_v3" in imports, imports
            if "636c69656e74" in path.name:
                assert "clausev1_616e73776572_76616c7565_0" in imports, imports
                assert "clausev1_616e73776572_6964656e74697479_1" in imports, imports
                assert "CLAUSE_enter_v1" in imports and "CLAUSE_tail_v1" in imports, imports
        (root / "inspection.txt").write_text("\n".join(reports), encoding="utf-8")
        ir = (root / "llvm-ir/clausev1_616e73776572__0.ll").read_text(encoding="utf-8")
        assert f"store i{bits} {minimum * 16 + 15}" in ir, ir
        assert f"store i{bits} {maximum * 16 + 15}" in ir, ir
        assert f"i32 20, i32 {bits}" in ir and f"define i{bits} @clausev1_" in ir, ir
        assert ("optsize" in ir) == (level == "-Os"), ir
        # Calls, tail calls and returns leave a body by a guaranteed tail call of the code an inline transfer or
        # its service chose (OPT0.1).
        caller = (root / "llvm-ir/clausev1_636c69656e74__0.ll").read_text(encoding="utf-8")
        for service in ["CLAUSE_enter_v1", "CLAUSE_tail_v1", "CLAUSE_return_v1"]:
            assert re.search(r'call ptr @[^\n]*' + service, caller), caller
        assert re.search(r'musttail call (?:preserve_nonecc )?void %[\w.]+\(ptr[^\n]*\n\s*ret void', caller), caller
    answer.write_text(f"-module(answer). value() -> {maximum + 1}.\n", encoding="utf-8")
    promoted = work / triple / "promoted"
    run([tool, "--target-triple", triple, "--emit", "obj", "--artifact-dir", str(promoted), str(answer)])
    imports = run([nm, "--undefined-only", str(next(promoted.iterdir()))]).stdout
    # A bignum literal is a cell of the module literal area, built by no service (OPT0.4).
    assert "CLAUSE_integer_v1" not in imports, imports


def main():
    """Cover each supported backend and preserve explicit unavailable-backend diagnostics."""
    tool, readobj, nm, directory, backends = sys.argv[1:]
    work = pathlib.Path(directory)
    work.mkdir(parents=True, exist_ok=True)
    targets = [
        ("X86", "i686-unknown-linux-gnu", 32, "elf", "i386"),
        ("X86", "x86_64-unknown-linux-gnu", 64, "elf", "x86_64"),
        ("ARM", "armv7-unknown-linux-gnueabihf", 32, "elf", "arm"),
        ("AArch64", "aarch64-unknown-linux-gnu", 64, "elf", "aarch64"),
        ("AArch64", "arm64-apple-macosx14.0.0", 64, "Mach-O", "aarch64"),
        ("X86", "i686-pc-windows-msvc", 32, "COFF", "i386"),
        ("X86", "x86_64-pc-windows-msvc", 64, "COFF", "x86_64"),
    ]
    source = work / "answer.erl"
    source.write_text("-module(answer). value() -> 42.\n", encoding="utf-8")
    for backend, triple, bits, format_name, architecture in targets:
        if backend in backends.split(","):
            inspect(tool, readobj, nm, work, triple, bits, format_name, architecture)
        else:
            rejected(tool, source, work / triple / "unavailable", triple, "backend unavailable")
    source.write_text("-module(answer). value() -> 42.\n", encoding="utf-8")
    rejected(tool, source, work / "invalid", "notacpu-unknown-linux-gnu", "unknown target architecture")
    rejected(tool, source, work / "unsupported", "riscv64-unknown-linux-gnu", "backend unavailable")


if __name__ == "__main__":
    main()
