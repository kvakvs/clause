"""Clause library modules (docs/library.md#runtime-module-replacements): inventories, stubs, checks and the ledger.

  library_modules.py check ROOT [LIBRARY]          inventories, module files and modules.terms agree (no OTP)
  library_modules.py rejects ROOT                  check refuses the deliberately inconsistent fixture library
  library_modules.py generate LIBRARY APP MODULE   write the all-stub module of an inventory, once
  library_modules.py complete ROOT APP MODULE      bring a hand-written library module to its inventory
  library_modules.py include ROOT                  include each observed types header in its module
  library_modules.py coverage ROOT                 documented functions per provided or missing module (plan 21)
  library_modules.py freshness ROOT ERL CLAU WORK  inventories match the host OTP; observe, generate, check, compile
  library_modules.py report STDERR                 classify clau errors for the ledger (docs/library.md#ledger)
  library_modules.py reporting ROOT CLAU WORK      report classifies a real clau run over the fixture program

Added for plan step 41J.
"""

import collections
import contextlib
import filecmp
import io
import pathlib
import re
import shutil
import subprocess
import sys

ATOM = r"'(?:[^'\\]|\\.)*'|[a-z][a-zA-Z0-9_@]*"
INVENTORY_ENTRY = re.compile(rf"^\{{({ATOM}), (\d+), ([a-z][a-zA-Z0-9_]*), (implemented|refused|stub|declined)\}}\.$")
PROVIDED = re.compile(rf"^\{{({ATOM}), ([a-z][a-z_]*), provided\}}\.$")
MISSING = re.compile(rf'^\{{({ATOM}), ([a-z][a-z_]*), missing, "([^"]+)"\}}\.$')
# The body of a function a runtime catalog builtin implements: registration puts the builtin in its place, so the body
# never runs (builtin placeholders).
PLACEHOLDER = "clause_builtin_placeholder"
RESERVED = set(
    "after and andalso band begin bnot bor bsl bsr bxor case catch cond div else end fun if let maybe not of or "
    "orelse receive rem try when xor".split()
)
# Inventories with their own format and generator (docs/builtins.md#erlang-module), and the module list itself.
OWN_FORMAT = {"erlang.terms", "modules.terms"}
DOCUMENTED_ENTRY = re.compile(rf"^\{{({ATOM}), ([a-z]+), ({ATOM}), (\d+)\}}\.$")
# Provided modules whose documented functions may still be stubs, with the plan 21 step that completes each
# (docs/library.md#documented-functions); every other provided module implements, refuses or declines them all.
PARTIAL = {
    "dets": "LIB73",
    "ets": "LIB73",
    "inet": "LIB60B",
}


def unquote(name):
    """An atom's name from its source spelling."""
    return re.sub(r"\\(.)", r"\1", name[1:-1]) if name.startswith("'") else name


def atom(name):
    """Erlang source spelling of an atom."""
    if re.fullmatch(r"[a-z][a-zA-Z0-9_@]*", name) and name not in RESERVED:
        return name
    return "'" + name.replace("\\", "\\\\").replace("'", "\\'") + "'"


def term_lines(path):
    """The non-comment lines of a terms file."""
    return [line for line in path.read_text(encoding="utf-8").splitlines() if line and not line.startswith("%")]


def read_inventory(path, problems):
    """Inventory entries in file order: (name, arity, group, status)."""
    entries = []
    for line in term_lines(path):
        match = INVENTORY_ENTRY.match(line)
        if match:
            entries.append((unquote(match[1]), int(match[2]), match[3], match[4]))
        else:
            problems.append(f"{path.name}: malformed entry: {line}")
    return entries


def read_modules(library, problems):
    """library/modules.terms: {module: (application, reason or None for provided)}."""
    path = library / "modules.terms"
    modules = {}
    if not path.exists():
        return modules
    for line in term_lines(path):
        provided, missing = PROVIDED.match(line), MISSING.match(line)
        if provided:
            modules[unquote(provided[1])] = (provided[2], None)
        elif missing:
            modules[unquote(missing[1])] = (missing[2], missing[3])
        else:
            problems.append(f"modules.terms: malformed entry: {line}")
    return modules


def read_documented(library, problems):
    """library/documented.terms: {module: (application, [(name, arity)])}, the functions OTP documents."""
    path = library / "documented.terms"
    documented = {}
    if not path.exists():
        return documented
    for line in term_lines(path):
        match = DOCUMENTED_ENTRY.match(line)
        if match:
            app, functions = documented.setdefault(unquote(match[1]), (match[2], []))
            functions.append((unquote(match[3]), int(match[4])))
        else:
            problems.append(f"documented.terms: malformed entry: {line}")
    return documented


def check_documented(library, modules, problems):
    """Every documented function of a provided module is in its inventory and, unless the module is PARTIAL, not a
    stub (plan 21 step LIB0)."""
    for module, (app, functions) in sorted(read_documented(library, problems).items()):
        inventory = library / app / f"{module}.terms"
        if modules.get(module, (None, ""))[1] is not None or inventory.name in OWN_FORMAT or not inventory.exists():
            continue
        status = {(name, arity): state for name, arity, _, state in read_inventory(inventory, problems)}
        for key in functions:
            label = f"{module}:{key[0]}/{key[1]}"
            if key not in status:
                problems.append(f"{label} is documented but not in {inventory.name}")
            elif status[key] == "stub" and module not in PARTIAL:
                problems.append(f"{label} is documented but a stub; finish it or list {module} as PARTIAL")


def coverage(library):
    """Print each documented module's implemented and documented function counts."""
    problems = []
    modules = read_modules(library, problems)
    for module, (app, functions) in sorted(read_documented(library, problems).items(), key=lambda m: (m[1][0], m[0])):
        inventory = library / app / f"{module}.terms"
        if module in modules and modules[module][1] is None and inventory.name not in OWN_FORMAT:
            status = {(n, a): s for n, a, _, s in read_inventory(inventory, problems)}
            done = sum(status.get(key) != "stub" for key in functions)
            state = f"{done}/{len(functions)}" + (f" (partial: {PARTIAL[module]})" if module in PARTIAL else "")
        else:
            state = "provided, own format" if module in modules else "not provided"
        print(f"{app:7} {module:24} {state}")


def strip_comments(text):
    """Erlang source without % comments; strings, quoted atoms and $c character literals are kept."""
    out, i, quote = [], 0, None
    while i < len(text):
        c = text[i]
        if quote:
            out.append(c)
            if c == "\\":
                out.append(text[i + 1 : i + 2])
                i += 1
            elif c == quote:
                quote = None
        elif c == "$":
            out.append(text[i : i + 2])
            i += 1
        elif c in "\"'":
            quote = c
            out.append(c)
        elif c == "%":
            while i < len(text) and text[i] != "\n":
                i += 1
            continue
        else:
            out.append(c)
        i += 1
    return "".join(out)


def arity_at(text, start):
    """The number of top-level arguments of a call whose '(' ends just before `start`."""
    depth, commas, nonempty, i = 0, 0, False, start
    while i < len(text):
        c = text[i]
        if c in "\"'":
            j = i + 1
            while j < len(text) and text[j] != c:
                j += 2 if text[j] == "\\" else 1
            i = j
        elif c == "$":
            i += 1
        elif c in "([{":
            depth += 1
        elif c in ")]}":
            if depth == 0:
                return commas + 1 if nonempty else 0
            depth -= 1
        elif c == "," and depth == 0:
            commas += 1
        if not c.isspace():
            nonempty = True
        i += 1
    return None


def read_module(path):
    """A module source's exports, defined functions, stub functions and builtin placeholders, as sets of
    (name, arity)."""
    text = strip_comments(path.read_text(encoding="utf-8"))
    exports = set()
    for attribute in re.finditer(r"-export\(\[(.*?)\]\)\.", text, re.S):
        for item in filter(None, (part.strip() for part in attribute[1].split(","))):
            name, _, arity = item.rpartition("/")
            exports.add((unquote(name.strip()), int(arity)))
    defined = {(unquote(m[1]), arity_at(text, m.end())) for m in re.finditer(rf"(?m)^({ATOM})\(", text)}
    stubs = {
        (unquote(m[1]), m[2].count("_"))
        for m in re.finditer(rf"(?m)^({ATOM})\(((?:_(?:, _)*)?)\) ->\s+erlang:error\(\{{not_implemented, ({ATOM}), "
                             rf"(\d+)\}}\)\.$", text)
        if m[1] == m[3] and m[2].count("_") == int(m[4])
    }
    placeholders = {
        (unquote(m[1]), m[2].count("_"))
        for m in re.finditer(rf"(?m)^({ATOM})\(((?:_(?:, _)*)?)\) ->\s+{PLACEHOLDER}\.$", text)
    }
    if re.search(r"(?m)^-callback\b", text):
        # A behaviour module: the compiler defines and exports behaviour_info/1 from its -callback attributes.
        # Added for plan 21 step LIB20.
        exports.add(("behaviour_info", 1))
        defined.add(("behaviour_info", 1))
    return exports, defined, stubs, placeholders


def local_calls(path, functions):
    """The unqualified calls in a module source of the given (name, arity) functions: they would run the
    placeholder's body, not the builtin (builtin placeholders)."""
    text = strip_comments(path.read_text(encoding="utf-8"))
    calls = set()
    for m in re.finditer(rf"(?<![\w:'@/])({ATOM})\(", text):
        line_start = text.rfind("\n", 0, m.start()) + 1
        key = (unquote(m[1]), arity_at(text, m.end()))
        if key in functions and m.start() != line_start:
            calls.add(key)
    return calls


def catalog(root, module):
    """The functions of `module` that the runtime's bridge catalog implements."""
    text = (root / "abi/include/clause/abi/builtins.hpp").read_text(encoding="utf-8")
    return {(f, int(a)) for f, a in re.findall(rf'BuiltinName\{{"{re.escape(module)}", "([^"]+)", (\d+)(?:, (?:DIRECT|QUICK))?\}}', text)}


def check_module(root, source, inventory, problems):
    """An inventory and its module source agree: same exports, stub entries are stubs, the others are not."""
    module = source.stem
    entries = read_inventory(inventory, problems)
    exports, defined, stubs, placeholders = read_module(source)
    listed = {(name, arity) for name, arity, _, _ in entries}
    problems += [f"{module}:{f}/{a} is exported but not in {inventory.name}" for f, a in sorted(exports - listed)]
    problems += [f"{module}:{f}/{a} is in {inventory.name} but not exported" for f, a in sorted(listed - exports)]
    builtins = catalog(root, module)
    problems += [f"{module}:{f}/{a} has a {PLACEHOLDER} body but is no catalog builtin"
                 for f, a in sorted(placeholders - builtins)]
    problems += [f"{module}:{f}/{a} is a catalog builtin; its stub should be a {PLACEHOLDER} body"
                 for f, a in sorted(stubs & builtins)]
    problems += [f"{module}:{f}/{a} is called unqualified, which runs its {PLACEHOLDER} body; call {module}:{f}"
                 for f, a in sorted(local_calls(source, placeholders))]
    for name, arity, _, status in entries:
        key, label = (name, arity), f"{module}:{name}/{arity}"
        if status in ("stub", "declined") and key not in stubs:
            problems.append(f"{label} is marked {status} but is not a not_implemented stub")
        # A catalog builtin replaces the module's stub at registration (docs/library.md#runtime-module-replacements).
        if status in ("implemented", "refused") and key not in builtins and (key in stubs or key not in defined):
            problems.append(f"{label} is marked {status} but has no body or catalog builtin")


def inventories(library):
    """Every inventory below the library's application directories, except files with their own format."""
    return sorted(p for p in library.glob("*/*.terms") if p.name not in OWN_FORMAT)


def check(root, library):
    """All problems of a library: modules.terms against the files, each inventory against its module."""
    problems = []
    modules = read_modules(library, problems)
    for module, (app, reason) in sorted(modules.items()):
        files = [library / app / f"{module}.erl", library / app / f"{module}.terms"]
        if reason is None:
            problems += [f"provided module {module} lacks {f.relative_to(library).as_posix()}" for f in files
                         if not f.exists()]
        elif list(library.glob(f"*/{module}.erl")):
            problems.append(f"missing module {module} has a module file")
    for inventory in inventories(library):
        module, app = inventory.stem, inventory.parent.name
        if modules.get(module, (None, ""))[0] != app or modules[module][1] is not None:
            problems.append(f"{inventory.relative_to(library).as_posix()} is not a provided module in modules.terms")
        source = inventory.with_suffix(".erl")
        if source.exists():
            check_module(root, source, inventory, problems)
    check_documented(library, modules, problems)
    for header in sorted(library.glob("*/*.types.hrl")):
        source = header.parent / header.name.replace(".types.hrl", ".erl")
        if not source.exists() or include_line(source) not in source.read_text(encoding="utf-8"):
            problems.append(f"{header.relative_to(library).as_posix()} is not included by its module")
    return problems


def stub_clause(name, arity, builtin=False):
    """A stub: raises error({not_implemented, Name, Arity}) (docs/library.md#provided-or-missing); a catalog
    builtin's placeholder instead."""
    head = f"{atom(name)}({', '.join(['_'] * arity)}) ->"
    body = f"{PLACEHOLDER}." if builtin else f"erlang:error({{not_implemented, {atom(name)}, {arity}}})."
    # erlfmt breaks a clause longer than 100 columns after its arrow.
    return f"{head} {body}\n" if len(head) + len(body) < 100 else f"{head}\n    {body}\n"


def generate(library, app, module):
    """Write library/<app>/<module>.erl with every inventory entry as a stub; never overwrite."""
    problems = []
    entries = read_inventory(library / app / f"{module}.terms", problems)
    target = library / app / f"{module}.erl"
    if problems or target.exists():
        raise SystemExit("\n".join(problems) or f"{target} exists; edit it by hand")
    builtins = catalog(pathlib.Path(__file__).resolve().parents[3], module)
    exports = ",\n".join(f"    {atom(name)}/{arity}" for name, arity, _, _ in entries)
    clauses = "".join(stub_clause(name, arity, (name, arity) in builtins) for name, arity, _, _ in entries)
    target.write_bytes(
        f"%% Clause's replacement for OTP's {module} (docs/library.md#runtime-module-replacements), generated from\n"
        f"%% {module}.terms by tests/compiler/library/library_modules.py; edited by hand from here on. A stub raises\n"
        f"%% error({{not_implemented, Name, Arity}}); a catalog builtin replaces its {PLACEHOLDER} body.\n"
        f"-module({atom(module)}).\n-export([\n{exports}\n]).\n\n{clauses}".encode("utf-8"))
    include_types(target)
    print(f"{target}: {len(entries)} stubs")


def write_inventory(path, entries):
    """Rewrite an inventory's entries, keeping its comment header."""
    header = "".join(line + "\n" for line in path.read_text(encoding="utf-8").splitlines() if line.startswith("%"))
    body = "".join(f"{{{atom(name)}, {arity}, {group}, {status}}}.\n" for name, arity, group, status in entries)
    path.write_bytes((header + body).encode("utf-8"))


def include_line(source):
    """The include of a module's observed types header (docs/library.md#types). Added for plan step 41K1."""
    return f'-include("{source.stem}.types.hrl").'


def include_types(source):
    """Include the module's types header after its last export attribute, when the header exists and is missing."""
    text = source.read_text(encoding="utf-8")
    if not source.with_suffix(".types.hrl").exists() or include_line(source) in text:
        return
    last = list(re.finditer(r"-export\(\[.*?\]\)\.", text, re.S))[-1]
    text = f"{text[: last.end()]}\n{include_line(source)}{text[last.end():]}"
    source.write_bytes(text.encode("utf-8"))


def complete(root, library, app, module):
    """Bring a hand-written module to its inventory: export every entry, stub what it does not define, and mark
    each entry implemented (a body or a catalog builtin, whose stub the runtime replaces) or stub."""
    problems = []
    inventory, source = library / app / f"{module}.terms", library / app / f"{module}.erl"
    entries = read_inventory(inventory, problems)
    if problems:
        raise SystemExit("\n".join(problems))
    exports, defined, stubs, _ = read_module(source)
    builtins = catalog(root, module)
    working = {key for key in defined if key not in stubs} | builtins
    absent_exports = [(n, a) for n, a, _, _ in entries if (n, a) not in exports]
    absent_bodies = [(n, a) for n, a, _, _ in entries if (n, a) not in defined]
    text = source.read_text(encoding="utf-8")
    if absent_exports:
        last = list(re.finditer(r"-export\(\[.*?\]\)\.", text, re.S))[-1]
        listing = ",\n".join(f"    {atom(n)}/{a}" for n, a in absent_exports)
        text = (f"{text[: last.end()]}\n%% OTP 29 exports implemented natively or not yet: catalog builtins, whose {PLACEHOLDER} body the runtime's\n"
                f"%% builtin replaces, and stubs raising error({{not_implemented, F, A}}) (docs/library.md#runtime-module-replacements).\n"
                f"-export([\n{listing}\n]).{text[last.end():]}")
    if absent_bodies:
        text = text.rstrip("\n") + "\n\n" + "".join(stub_clause(n, a, (n, a) in builtins) for n, a in absent_bodies)
    source.write_bytes(text.encode("utf-8"))
    include_types(source)
    write_inventory(inventory, [(n, a, g, "implemented" if (n, a) in working else "stub") for n, a, g, _ in entries])
    print(f"{source}: {len(working & {(n, a) for n, a, _, _ in entries})} implemented, {len(absent_bodies)} stubs added")


def run(command):
    """Run a tool; fail with its output unless it succeeds."""
    result = subprocess.run([str(part) for part in command], capture_output=True, text=True, timeout=300,
                            check=False)
    if result.returncode != 0:
        raise AssertionError((command, result.stdout, result.stderr))
    return result


def freshness(root, erl, clau, work):
    """Each inventory equals a fresh observation; a new inventory goes through generate, check and clau."""
    if erl == "-":
        print("SKIP: no host Erlang/OTP")
        return
    escript = pathlib.Path(erl).with_name("escript.exe" if erl.lower().endswith(".exe") else "escript")
    observe = root / "tests/compiler/library/observe_module.escript"
    shutil.rmtree(work, ignore_errors=True)
    for inventory in inventories(root / "library"):
        observed = work / "observed" / inventory.name
        run([escript, observe, root / "library", inventory.parent.name, inventory.stem, observed])
        if not filecmp.cmp(observed, inventory, shallow=False):
            raise AssertionError(f"{inventory} differs from the host OTP; rerun observe_module.escript")
    # Types headers (plan step 41K1): every provided module's header holds the forms a fresh observation gives.
    for module, (app, reason) in sorted(read_modules(root / "library", []).items()):
        if reason is None:
            run([escript, root / "tests/compiler/library/observe_types.escript", "--check", root / "library", app,
                 module])
    library = work / "library"
    run([escript, observe, library, "erts", "persistent_term"])
    (library / "modules.terms").write_text("{persistent_term, erts, provided}.\n", encoding="utf-8")
    generate(library, "erts", "persistent_term")
    problems = check(root, library)
    assert not problems, problems
    run([clau, library / "erts/persistent_term.erl"])
    print("library_freshness: ok")


def rejects(root):
    """The inconsistent fixture library yields exactly the expected problems."""
    fixture = root / "tests/fixtures/library/inconsistent"
    expected = (fixture / "expected.txt").read_text(encoding="utf-8").splitlines()
    found = check(root, fixture / "library")
    assert found == expected, "\n".join(found)
    print("library_rejects: ok")


def called_function(path, row, col, module):
    """name/arity of the call to `module` an error points at; '?' when the source does not show it."""
    lines = pathlib.Path(path).read_text(encoding="utf-8", errors="replace").split("\n")
    text = "\n".join(lines[row - 1 : row + 30])
    # A call's error column points at its '('; the name before it holds even when a macro names the module.
    if text[col - 1 : col] == "(" and (name := re.search(rf"({ATOM})\s*$", text[: col - 1])):
        arity = arity_at(text, col)
        return f"{unquote(name[1])}/{'?' if arity is None else arity}"
    start = text.rfind(module, 0, col + len(module))
    call = re.match(rf"'?{re.escape(module)}'?\s*:\s*({ATOM})\s*(/\s*\d+|\()?", text[max(start, 0) :])
    if not call:
        return "?"
    name = unquote(call[1])
    if call[2] and call[2].startswith("/"):
        return f"{name}/{call[2][1:].strip()}"
    arity = arity_at(text, max(start, 0) + call.end()) if call[2] else None
    return f"{name}/{'?' if arity is None else arity}"


def report(stderr):
    """Markdown tables of a clau run's errors: unknown modules, undefined functions and remote types, the rest."""
    unknown = collections.defaultdict(lambda: (collections.Counter(), set()))
    undefined = collections.defaultdict(collections.Counter)
    types = collections.defaultdict(collections.Counter)
    other = collections.Counter()
    for line in pathlib.Path(stderr).read_text(encoding="utf-8", errors="replace").splitlines():
        match = re.match(r"error: (.+?):(\d+):(\d+): (.*)$", line)
        if not match:
            continue
        path, row, col, message = match[1], int(match[2]), int(match[3]), match[4]
        if module := re.fullmatch(r"unknown module (\S+)", message):
            calls, callers = unknown[module[1]]
            calls[called_function(path, row, col, module[1])] += 1
            callers.add(pathlib.Path(path).stem)
        elif function := re.fullmatch(r"undefined function (.+):(.+/\d+)", message):
            undefined[function[1]][function[2]] += 1
        elif remote := re.fullmatch(r"undefined remote type (.+?):(.+)", message):
            types[remote[1]][remote[2]] += 1
        else:
            other[re.sub(r"(?<!\S)(?:[A-Za-z]:)?[\\/][^ :]+", "<path>", message)] += 1
    print("| Unknown module | Errors | Callers | Functions called |\n| --- | --- | --- | --- |")
    for module, (calls, callers) in sorted(unknown.items(), key=lambda item: -sum(item[1][0].values())):
        print(f"| `{module}` | {sum(calls.values())} | {', '.join(sorted(callers))} | {listing(calls)} |")
    print("\n| Module of undefined functions | Errors | Functions |\n| --- | --- | --- |")
    for module, calls in sorted(undefined.items(), key=lambda item: -sum(item[1].values())):
        print(f"| `{module}` | {sum(calls.values())} | {listing(calls)} |")
    print("\n| Module of undefined remote types | Errors | Types |\n| --- | --- | --- |")
    for module, names in sorted(types.items(), key=lambda item: -sum(item[1].values())):
        print(f"| `{module}` | {sum(names.values())} | {listing(names)} |")
    print("\n| Other errors | Message |\n| --- | --- |")
    for message, count in other.most_common():
        print(f"| {count} | {message} |")


def reporting(root, clau, work):
    """clau's errors for the fixture program come out of report as unknown module and undefined function rows."""
    shutil.rmtree(work, ignore_errors=True)
    work.mkdir(parents=True)
    source = root / "tests/fixtures/library/report/main.erl"
    result = subprocess.run([str(clau), str(source)], capture_output=True, text=True, timeout=300, check=False)
    assert result.returncode != 0, result
    (work / "stderr.txt").write_text(result.stderr, encoding="utf-8")
    output = io.StringIO()
    with contextlib.redirect_stdout(output):
        report(work / "stderr.txt")
    rows = output.getvalue().splitlines()
    assert "| `clause_report_absent` | 2 | main | `close/1`, `open/2` |" in rows, rows
    assert "| `lists` | 1 | `clause_report_absent/1` |" in rows, rows
    print("library_report: ok")


def listing(calls):
    """Functions with their counts above one, sorted."""
    return ", ".join(f"`{f}`" + (f" x{n}" if n > 1 else "") for f, n in sorted(calls.items()))


def main(arguments):
    action = arguments[0] if arguments else ""
    if action == "check" and len(arguments) in (2, 3):
        root = pathlib.Path(arguments[1])
        problems = check(root, pathlib.Path(arguments[2]) if len(arguments) == 3 else root / "library")
        for problem in problems:
            print(f"error: {problem}", file=sys.stderr)
        print("library_inventory: ok" if not problems else f"{len(problems)} problems")
        return 1 if problems else 0
    if action == "rejects" and len(arguments) == 2:
        rejects(pathlib.Path(arguments[1]))
    elif action == "complete" and len(arguments) == 4:
        root = pathlib.Path(arguments[1])
        complete(root, root / "library", arguments[2], arguments[3])
    elif action == "coverage" and len(arguments) == 2:
        coverage(pathlib.Path(arguments[1]) / "library")
    elif action == "include" and len(arguments) == 2:
        for source in sorted(pathlib.Path(arguments[1]).glob("library/*/*.erl")):
            include_types(source)
    elif action == "generate" and len(arguments) == 4:
        generate(pathlib.Path(arguments[1]), arguments[2], arguments[3])
    elif action == "freshness" and len(arguments) == 5:
        freshness(pathlib.Path(arguments[1]), arguments[2], arguments[3], pathlib.Path(arguments[4]))
    elif action == "report" and len(arguments) == 2:
        report(arguments[1])
    elif action == "reporting" and len(arguments) == 4:
        reporting(pathlib.Path(arguments[1]), arguments[2], pathlib.Path(arguments[3]))
    else:
        print(__doc__, file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
