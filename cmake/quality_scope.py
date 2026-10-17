"""Select project files for scoped quality checks from git changes and Ninja header dependencies."""
import argparse
import hashlib
import json
import os
import subprocess

PRODUCTION = ("compiler/", "runtime/", "abi/")
CPP = (".cpp", ".cc", ".cxx", ".hpp", ".h", ".hh", ".hxx")
# The analyzer's own configuration: a change can alter any finding, so every unit is checked again.
TIDY_CONFIG = (".clang-tidy", "cmake/CheckClangTidy.cmake", "cmake/QualityScope.cmake",
               "cmake/QualityToolchain.cmake.in", "cmake/quality_scope.py", "cmake/tidy_filter.py")


def norm(path):
    """Compare paths case-insensitively where the host does, independent of separators."""
    return os.path.normcase(os.path.normpath(path))


def git_lines(root, *args):
    """Run git in the repository and return its nonempty output lines."""
    result = subprocess.run(["git", "-C", root, *args], capture_output=True, text=True, encoding="utf-8")
    if result.returncode:
        raise SystemExit(f"git {' '.join(args)} failed: {result.stderr.strip()}")
    return [line for line in result.stdout.splitlines() if line]


def changed_files(root, base):
    """Existing files changed since base (including the working tree) plus untracked files."""
    tracked = git_lines(root, "diff", "--name-only", base, "--")
    untracked = git_lines(root, "ls-files", "--others", "--exclude-standard")
    return sorted(path for path in set(tracked + untracked) if os.path.isfile(os.path.join(root, path)))


def affects_everything(path):
    """Production build rules can change every translation unit's command."""
    if path == "CMakeLists.txt" or path.startswith("cmake/"):
        return True
    return path.startswith(PRODUCTION) and path.endswith("CMakeLists.txt")


class Unit:
    """One production translation unit: its objects in every configuration and its first (analyzed) command."""

    def __init__(self, command):
        self.outputs = set()
        self.digest = hashlib.sha1(command.encode("utf-8")).hexdigest()


def production_units(root, build):
    """Map each production translation unit to its objects and command digest, as the database lists them."""
    with open(os.path.join(build, "compile_commands.json"), encoding="utf-8") as stream:
        commands = json.load(stream)
    units = {}
    for entry in commands:
        source = os.path.join(entry["directory"], entry["file"])
        relative = os.path.relpath(source, root).replace(os.sep, "/")
        if relative.startswith(PRODUCTION):
            command = entry.get("command") or " ".join(entry.get("arguments", []))
            unit = units.setdefault(relative, Unit(entry["directory"] + "\n" + command))
            unit.outputs.add(norm(os.path.join(entry["directory"], entry.get("output", ""))))
    return units


def record_path(build):
    """Command digests of the units the last passing clang-tidy run covered."""
    return os.path.join(build, "quality", "tidy-commands.json")


def read_record(build):
    """The recorded digests by unit, or None before the first passing run."""
    try:
        with open(record_path(build), encoding="utf-8") as stream:
            return json.load(stream)
    except (OSError, ValueError):
        return None


def write_record(root, build):
    """Record every unit's current command digest after a passing run."""
    units = production_units(root, build)
    with open(record_path(build), "w", encoding="utf-8", newline="\n") as stream:
        json.dump({path: unit.digest for path, unit in sorted(units.items())}, stream, indent=0)


def header_users(build, ninja, headers):
    """The objects whose recorded dependencies include each changed header; None when Ninja data is missing."""
    if not ninja or not os.path.exists(os.path.join(build, ".ninja_deps")):
        return None
    result = subprocess.run([ninja, "-C", build, "-t", "deps"], capture_output=True, text=True,
                            encoding="utf-8", errors="replace")
    if result.returncode:
        return None
    users, current = {norm(header): set() for header in headers}, None
    for line in result.stdout.splitlines():
        if line and not line[0].isspace():
            current = norm(os.path.join(build, line.split(": #deps", 1)[0]))
        elif current and (header := norm(os.path.join(build, line.strip()))) in users:
            users[header].add(current)
    return users


def representative(header, units, objects):
    """The one unit that analyzes a changed header in the changed scope: its own source when that includes it, else
    its first user. Every other dependent is left to the full scope (check-quality-all, the full gate)."""
    users = sorted(path for path, unit in units.items() if unit.outputs & objects)
    stem = os.path.splitext(os.path.basename(header))[0]
    own = [path for path in users if os.path.splitext(os.path.basename(path))[0] == stem]
    return (own or users)[:1]


def tidy_scope(root, build, ninja, changed):
    """Changed units, one unit per changed production header and units whose command changed since the last passing
    run; every unit when the analyzer configuration changed or nothing tells which commands changed. A header's other
    dependents are checked by the full scope (check-quality-all) in the full gate."""
    units = production_units(root, build)
    if any(path in TIDY_CONFIG for path in changed):
        return sorted(units), "analyzer configuration changed"
    recorded = read_record(build)
    if recorded is None and any(affects_everything(path) for path in changed):
        return sorted(units), "build configuration changed, no record of analyzed commands"
    selected = {path for path in changed if path in units}
    headers = [os.path.join(root, path) for path in changed
               if path.startswith(PRODUCTION) and path.endswith(CPP) and path not in units]
    if headers:
        users = header_users(build, ninja, headers)
        if users is None:
            return sorted(units), "header dependencies unavailable"
        for header, objects in users.items():
            selected.update(representative(header, units, objects))
    if recorded is not None:
        selected |= {path for path, unit in units.items() if recorded.get(path) != unit.digest}
    return sorted(selected), "changed files, one unit per changed header and units with changed commands"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kind", choices=["tidy", "lizard", "record"], required=True)
    parser.add_argument("--root", required=True)
    parser.add_argument("--build")
    parser.add_argument("--ninja", default="")
    parser.add_argument("--base", default="HEAD")
    parser.add_argument("--output")
    args = parser.parse_args()
    if args.kind == "record":
        write_record(args.root, args.build)
        return
    changed = changed_files(args.root, args.base)
    if args.kind == "lizard":
        files = [path for path in changed if path.startswith(PRODUCTION) and path.endswith(CPP)]
        reason = "changed production C++ files"
    else:
        files, reason = tidy_scope(args.root, args.build, args.ninja, changed)
    with open(args.output, "w", encoding="utf-8", newline="\n") as stream:
        stream.write("".join(path + "\n" for path in files))
    print(f"Quality scope ({args.kind}, base {args.base}): {len(files)} file(s); {reason}")


if __name__ == "__main__":
    main()
