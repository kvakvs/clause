"""Library modules in linked programs (plan step 41K, docs/library.md#which-module-a-name-resolves-to).

  library_programs.py stubs ROOT CLAU WORK SUFFIX       every provided module's stub raises not_implemented in every call form
  library_programs.py precedence ROOT CLAU WORK SUFFIX  Clause modules win over search paths, explicit inputs win
                                                        over Clause modules with a warning, missing names are reserved
"""

import pathlib
import shutil
import subprocess
import sys

MANIFEST = """schema_version = 1

[[targets]]
name = "app"
sources = [{sources}]
entry = "main"
output = "app"

[targets.options]
source_search_paths = [{search}]
ignore_modules = [{ignore}]
"""


def run(command, cwd=None):
    """Run a command; returns (status, stdout, stderr) with LF line ends."""
    result = subprocess.run([str(part) for part in command], cwd=cwd, capture_output=True, text=True, timeout=600,
                            check=False)
    return result.returncode, result.stdout.replace("\r\n", "\n"), result.stderr.replace("\r\n", "\n")


def stubs(root, clau, work, suffix):
    """The stubs fixture links and confirms every call form."""
    shutil.rmtree(work, ignore_errors=True)
    work.mkdir(parents=True)
    status, _, err = run([clau, "-o", work / "stubs", root / "tests/fixtures/library/stubs/main.erl"])
    assert status == 0, err
    status, out, err = run([work / ("stubs" + suffix)])
    assert status == 0 and out == "library_stubs: ok\n", (out, err)
    print("library_stubs: ok")


class Project:
    """A copy of the precedence fixture with one manifest; builds into its own directory."""

    def __init__(self, root, clau, work, name):
        self.clau, self.directory = clau, work / name
        shutil.copytree(root / "tests/fixtures/library/precedence", self.directory)

    def build(self, sources, search=(), ignore=()):
        """Build with the given options; returns (status, stderr lines without the target header)."""
        quoted = lambda items: ", ".join(f'"{item}"' for item in items)
        manifest = MANIFEST.format(sources=quoted(sources), search=quoted(search), ignore=quoted(ignore))
        (self.directory / "project.toml").write_bytes(manifest.encode())
        status, _, err = run([self.clau, "--project", "project.toml"], cwd=self.directory)
        return status, [line for line in err.splitlines() if not line.endswith("[target app]:")]

    def output(self, suffix):
        status, out, err = run([self.directory / ("app" + suffix)])
        assert status == 0, err
        return out


def precedence(root, clau, work, suffix):
    shutil.rmtree(work, ignore_errors=True)
    work.mkdir(parents=True)

    # A same-named module on a search path never replaces Clause's lists: same output with and without it.
    plain, searched = Project(root, clau, work, "plain"), Project(root, clau, work, "searched")
    without, with_search = plain.build(["src/main.erl"]), searched.build(["src/main.erl"], search=["standins"])
    assert without == (0, []) and with_search == (0, []), (without, with_search)
    assert plain.output(suffix) == searched.output(suffix) == "[2,1]\n"
    print("library_precedence: a search path never replaces a Clause module")

    # An explicit input replaces Clause's lists, with a warning naming both files.
    explicit = Project(root, clau, work, "explicit")
    status, err = explicit.build(["src/main.erl", "standins/lists.erl"])
    assert status == 0 and len(err) == 1 and "module lists replaces Clause's library module" in err[0], err
    assert explicit.output(suffix) == "stand_in\n"
    print("library_precedence: an explicit input replaces a Clause module with a warning")

    # A missing module's name is reserved: the stand-in on the search path is not used.
    reserved = Project(root, clau, work, "reserved")
    status, err = reserved.build(["reserved/main.erl"], search=["standins"])
    assert status != 0 and any("Clause does not provide module prim_file" in line for line in err), err
    assert any("unknown module prim_file" in line for line in err), err
    print("library_precedence: a missing module is never taken from a search path")

    # ignore_modules applies to Clause modules: calls reach the ignored-module placeholder, without a warning.
    ignored = Project(root, clau, work, "ignored")
    status, err = ignored.build(["src/main.erl"], ignore=["lists"])
    assert status == 0 and err == [], err
    assert ignored.output(suffix) == "{module_ignored,lists}\n"
    print("library_precedence: ok")


def main(arguments):
    actions = {"stubs": stubs, "precedence": precedence}
    if len(arguments) != 5 or arguments[0] not in actions:
        print(__doc__, file=sys.stderr)
        return 2
    action, root, clau, work, suffix = arguments
    actions[action](pathlib.Path(root), clau, pathlib.Path(work), suffix)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
