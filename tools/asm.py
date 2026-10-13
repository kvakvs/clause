"""Recompile one Clause C++ source with its recorded build flags and write a readable assembly listing.

Created for comparing Debug and Release code generation (decompile-debug.bat / decompile-release.bat).
Only clang-cl builds (the Windows toolchain) are supported: the listing comes from clang-cl /FA.
"""

import argparse
import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
CONFIGS = ("Debug", "Release", "RelWithDebInfo", "MinSizeRel")
PREFERRED_BUILDS = ("tool", "debug")
SOURCE_SUFFIXES = (".cpp", ".cc", ".cxx", ".c")
# Sections holding code or program data; debug info, unwind tables and linker directives are dropped.
KEPT_SECTIONS = (".text", ".rdata", ".data", ".bss")
# Directives and compiler comments that only feed debug info, unwinding or symbol tables.
NOISE_PREFIXES = (".cv_", ".seh_", "#DEBUG_", ".def", ".scl", ".type", ".endef", ".globl", ".weak",
                  ".addrsig", ".file", ".intel_syntax", "@feat.00", "# kill:", "# implicit-def:")
NOISE_LABEL = re.compile(r"^\.L(tmp|func_begin|func_end)\d+:")
LABEL = re.compile(r'^"?([^"\s:]+)"?:')
QUOTED_SYMBOL = re.compile(r'"([^"]+)"')
CV_FILE = re.compile(r'^\.cv_file\s+(\d+)\s+"((?:[^"\\]|\\.)*)"')
CV_LOC = re.compile(r"^\.cv_loc\s+\d+\s+(\d+)\s+(\d+)")
# .section NAME[,"FLAGS"[,SELECTION,COMDAT_SYMBOL]][,unique,ID]
SECTION = re.compile(r'^\.section\s+([^,\s]+)(?:,"[^"]*")?(?:,\w+,"?([^",]+)"?)?(?:,unique,\d+)?\s*$')
BARE_SECTION = re.compile(r"^(\.text|\.data|\.bss)$")
# Words that make MSVC demangled names long without helping to read them.
DEMANGLE_NOISE = re.compile(r"\b(?:class|struct|enum|union) |__cdecl ")
ACCESS = re.compile(r"^(?:(?:public|private|protected): )?(?:(?:static|virtual) )*")
TEMPLATE_KEEP = 40
SOURCE_TEXT_MAX = 100


class ToolError(Exception):
    """A user-facing failure: printed without a traceback."""


def parse_args(argv):
    """Read the command line; MODULE is a source name without extension, optionally with parent dirs."""
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("module", help="source name without path or extension, e.g. attributes or ast/attributes")
    parser.add_argument("--config", default="Release", choices=CONFIGS, help="build configuration (default Release)")
    parser.add_argument("--function", help="only functions whose demangled name contains this text")
    parser.add_argument("--build-dir", type=Path, help="build directory to take the compile command from")
    parser.add_argument("--output", type=Path, help="output file (default <module>.<config>.asm here)")
    parser.add_argument("--raw", action="store_true", help="write the unfiltered compiler listing")
    return parser.parse_args(argv)


# --- Finding the compile command ---

def build_dirs(explicit):
    """List build directories holding a compilation database in preference order, or the explicit one."""
    if explicit:
        return [explicit.resolve()]
    found = sorted(path.parent for path in (REPO_ROOT / "build").glob("*/compile_commands.json"))
    # The manual build (all configs) and the testing build come first; other builds are fallbacks.
    return sorted(found, key=lambda path: PREFERRED_BUILDS.index(path.name) if path.name in PREFERRED_BUILDS else 99)


def cached_build_type(build_dir):
    """Return CMAKE_BUILD_TYPE of a single-config build directory, or an empty string."""
    cache = build_dir / "CMakeCache.txt"
    if not cache.exists():
        return ""
    match = re.search(r"^CMAKE_BUILD_TYPE:\w+=(.*)$", cache.read_text(errors="replace"), re.MULTILINE)
    return match.group(1).strip() if match else ""


def entry_config(entry, build_type):
    """Return the configuration an entry compiles: its multi-config object directory or the cached type."""
    parts = Path(entry.get("output", "")).parts
    return next((part for part in parts if part in CONFIGS), build_type)


def module_key(module):
    """Normalize a module name or source path into a lowercase slash path without a source suffix."""
    key = module.replace("\\", "/").lower().strip("/")
    for suffix in SOURCE_SUFFIXES:
        key = key.removesuffix(suffix)
    return key


def load_entries(module, dirs):
    """Yield (build_dir, entry, config) for every compile command of a source matching the module."""
    key = module_key(module)
    for build_dir in dirs:
        database = build_dir / "compile_commands.json"
        if not database.exists():
            raise ToolError(f"{database} does not exist; configure that build first")
        build_type = cached_build_type(build_dir)
        for entry in json.loads(database.read_text()):
            path = module_key(entry["file"])
            if path == key or path.endswith("/" + key):
                yield build_dir, entry, entry_config(entry, build_type)


def select_entry(module, config, explicit_dir):
    """Pick the one compile command for the module and configuration, explaining ambiguity or absence."""
    dirs = build_dirs(explicit_dir)
    found = list(load_entries(module, dirs))
    sources = sorted({Path(entry["file"]).as_posix() for _, entry, _ in found})
    if not sources:
        raise ToolError(f"no compiled source matches '{module}' in: {', '.join(d.as_posix() for d in dirs)}")
    if len(sources) > 1:
        listed = "\n  ".join(sources)
        raise ToolError(f"'{module}' matches several sources; add parent directories to choose:\n  {listed}")
    matching = [(d, e) for d, e, c in found if c.lower() == config.lower()]
    if not matching:
        raise ToolError(f"no {config} compile command for {sources[0]}; configure a build with that config")
    return matching[0]


# --- Compiling ---

def compiler_path(command):
    """Return the compiler executable at the start of a compile command."""
    match = re.match(r'\s*(?:"([^"]+)"|(\S+))', command)
    return Path(match.group(1) or match.group(2))


def listing_command(command, work_dir):
    """Rewrite a recorded clang-cl command to also emit an annotated assembly listing into work_dir."""
    if "clang-cl" not in compiler_path(command).name.lower():
        raise ToolError("only clang-cl builds are supported (the Windows toolchain)")
    # /Z7 adds the line tables used for source annotations; it does not change code generation.
    outputs = f'/Fo"{work_dir / "module.obj"}" /FA /Fa"{work_dir / "module.asm"}" -clang:-fverbose-asm /Z7'
    command = re.sub(r'/Fd(?:"[^"]*"|\S+)', "", command)
    return re.sub(r'/Fo(?:"[^"]*"|\S+)', lambda _: outputs, command)


def compile_listing(entry, work_dir):
    """Compile the source in its build directory and return the raw listing text."""
    result = subprocess.run(listing_command(entry["command"], work_dir), cwd=entry["directory"])
    if result.returncode != 0:
        raise ToolError(f"compilation failed with exit code {result.returncode}")
    return (work_dir / "module.asm").read_text(errors="replace")


# --- Demangling ---

def symbol_names(entry, obj):
    """Map every mangled symbol of the object to its demangled name using llvm-nm next to clang-cl."""
    nm = compiler_path(entry["command"]).with_name("llvm-nm.exe")
    if not nm.exists():
        raise ToolError(f"{nm} was not found; it is needed to demangle names")
    # -p keeps symbol table order, so the mangled and demangled lists pair up line by line.
    listings = [subprocess.run([nm, "-p", "-j", *flags, obj], capture_output=True, text=True, check=True).stdout
                for flags in ([], ["-C"])]
    return dict(zip(listings[0].splitlines(), listings[1].splitlines()))


def top_level(text, token, start=0):
    """Return the index of token outside template brackets, or -1."""
    depth = 0
    for index in range(start, len(text)):
        if depth == 0 and text.startswith(token, index):
            return index
        if text[index] == "<":
            depth += 1
        elif text[index] == ">" and text[index - 1] != "-":
            depth -= 1
    return -1


def collapse_templates(name):
    """Shorten long template argument lists to <...>; names with unbalanced brackets stay as they are."""
    result, index = [], 0
    while (start := name.find("<", index)) != -1:
        end = top_level(name, ">", start + 1)
        if end == -1:
            return name
        inner = name[start:end + 1]
        result.append(name[index:start] + (inner if len(inner) <= TEMPLATE_KEEP else "<...>"))
        index = end + 1
    return "".join(result) + name[index:]


def short_name(demangled):
    """Reduce a demangled function to its qualified name: no access, return type or parameters."""
    name = ACCESS.sub("", demangled)
    convention = top_level(name, "__cdecl ")
    if convention != -1:
        name = name[convention + len("__cdecl "):]
    call_operator = name.find("operator()")
    parameters = top_level(name, "(", call_operator + len("operator()") if call_operator != -1 else 0)
    if parameters != -1:
        name = name[:parameters]
    return collapse_templates(DEMANGLE_NOISE.sub("", name))


class Demangler:
    """Readable replacements for mangled symbols in cleaned listings."""

    def __init__(self, names):
        # Mangled name -> llvm-nm demangling, from the freshly compiled object.
        self.names_ = names

    def full(self, symbol):
        """Return the complete demangled signature without class/struct/__cdecl noise."""
        return DEMANGLE_NOISE.sub("", self.names_.get(symbol, symbol))

    def short(self, symbol):
        """Return the short display name, keeping symbols that do not demangle usefully (string literals)."""
        prefix = "__imp_" if symbol.startswith("__imp_") else ""
        demangled = self.names_.get(symbol.removeprefix(prefix))
        if demangled is None or demangled.startswith("`string'"):
            return f'"{symbol}"'
        return prefix + short_name(demangled)

    def line(self, text):
        """Replace every quoted mangled symbol in one listing line."""
        return QUOTED_SYMBOL.sub(lambda match: self.short(match.group(1)), text)


# --- Cleaning the listing ---

class SourceLines:
    """Source text for line annotations, read once per file."""

    def __init__(self):
        # Path -> file lines (empty when unreadable).
        self.files_ = {}

    def text(self, path, line):
        """Return the stripped source text of a 1-based line, or an empty string."""
        if path not in self.files_:
            try:
                self.files_[path] = Path(path).read_text(errors="replace").splitlines()
            except OSError:
                self.files_[path] = []
        lines = self.files_[path]
        return lines[line - 1].strip()[:SOURCE_TEXT_MAX] if 0 < line <= len(lines) else ""


class Cleaner:
    """Filter a clang-cl COFF listing down to code and data, annotated with source lines."""

    def __init__(self, function_filter, demangler):
        # Demangled-name substring selecting functions; None keeps the whole module.
        self.filter_ = function_filter
        # Demangler for function headers and symbol operands.
        self.demangler_ = demangler
        self.sources_ = SourceLines()
        # .cv_file id -> source path, for line annotations.
        self.files_ = {}
        # Symbols declared as functions (.def with .type 32), and the last .def seen.
        self.functions_ = set()
        self.pending_def_ = None
        # Function whose code or associated tables are being read, and whether it is printed.
        self.function_ = None
        self.selected_ = False
        # False inside debug, unwind and directive sections.
        self.section_kept_ = True
        # Last annotated (file id, line), so repeated locations print once.
        self.last_location_ = None
        self.matched_ = 0
        self.out_ = []

    def feed(self, line):
        """Update state from one listing line and print it when it is code or data of a selected region."""
        stripped = line.strip()
        self.track_symbols(stripped)
        if self.track_sections(stripped):
            return
        if stripped.startswith(".cv_"):
            self.track_debug(stripped)
            return
        label = LABEL.match(line)
        if label and label.group(1) in self.functions_:
            self.start_function(label.group(1))
        if self.visible() and not self.is_noise(stripped):
            self.emit(line.rstrip())

    def track_symbols(self, stripped):
        """Record function declarations; a new .def ends the previous function's region."""
        if stripped.startswith(".def"):
            self.pending_def_ = stripped.split(None, 1)[1].rstrip(";").strip('"')
            self.function_ = None
        elif stripped.startswith(".type") and stripped.split()[1].rstrip(";") == "32":
            self.functions_.add(self.pending_def_)

    def track_sections(self, stripped):
        """Follow section switches and return True for section directives; foreign tables end a function."""
        if stripped == ".seh_handlerdata":
            self.section_kept_ = False
            return True
        if match := SECTION.match(stripped):
            name, comdat = match.groups()
        elif match := BARE_SECTION.match(stripped):
            name, comdat = match.group(1), self.function_ if stripped == ".text" else None
        else:
            return False
        self.section_kept_ = name.startswith(KEPT_SECTIONS)
        if comdat != self.function_:
            self.function_ = None
        if self.section_kept_ and not name.startswith(".text") and self.visible():
            self.out_.append(f"\n\t.section\t{name}")
        return True

    def track_debug(self, stripped):
        """Turn .cv_file/.cv_loc into source line comments; the other CodeView directives are dropped."""
        if file_match := CV_FILE.match(stripped):
            self.files_[file_match.group(1)] = file_match.group(2).replace("\\\\", "\\")
            return
        loc = CV_LOC.match(stripped)
        if not loc or not self.visible():
            return
        location = (loc.group(1), int(loc.group(2)))
        if location[1] == 0 or location == self.last_location_ or location[0] not in self.files_:
            return
        self.last_location_ = location
        path = self.files_[location[0]]
        self.out_.append(f"\t# {Path(path).name}:{location[1]}  {self.sources_.text(path, location[1])}".rstrip())

    def start_function(self, symbol):
        """Open a function region and print its demangled signature as a header when selected."""
        self.function_ = symbol
        self.last_location_ = None
        full = self.demangler_.full(symbol)
        self.selected_ = self.filter_ is None or self.filter_ in full
        if self.selected_:
            self.matched_ += 1
            self.out_.append(f"\n# ==== {full}")

    def visible(self):
        """True when the current line belongs to printed code or data."""
        if not self.section_kept_:
            return False
        return self.filter_ is None or (self.function_ is not None and self.selected_)

    def is_noise(self, stripped):
        """True for lines that only serve debug info, unwinding, symbol tables or inter-symbol padding."""
        if stripped.startswith(".p2align") and self.function_ is None:
            return True
        return stripped.startswith(NOISE_PREFIXES) or bool(NOISE_LABEL.match(stripped))

    def emit(self, text):
        """Append a printed line with its symbols demangled."""
        self.out_.append(self.demangler_.line(text))


def clean_listing(listing, function_filter, demangler):
    """Return the cleaned listing text; fail when --function selected nothing."""
    cleaner = Cleaner(function_filter, demangler)
    for line in listing.splitlines():
        cleaner.feed(line)
    if function_filter is not None and not cleaner.matched_:
        raise ToolError(f"no function name contains '{function_filter}'")
    return re.sub(r"\n{3,}", "\n\n", "\n".join(cleaner.out_)).strip("\n") + "\n"


def write_listing(args, build_dir, entry):
    """Compile, clean and write the listing; return the output path."""
    output = args.output or Path.cwd() / f"{Path(entry['file']).stem}.{args.config.lower()}.asm"
    with tempfile.TemporaryDirectory(prefix="clause-asm-") as work:
        listing = compile_listing(entry, Path(work))
        if not args.raw:
            demangler = Demangler(symbol_names(entry, Path(work) / "module.obj"))
            header = f"# {Path(entry['file']).as_posix()} ({args.config}, {build_dir.as_posix()})\n\n"
            listing = header + clean_listing(listing, args.function, demangler)
    output.write_text(listing)
    return output


def main(argv):
    """Entry point: returns the process exit status."""
    args = parse_args(argv)
    try:
        build_dir, entry = select_entry(args.module, args.config, args.build_dir)
        print(f"Wrote {write_listing(args, build_dir, entry)}")
    except ToolError as error:
        print(f"asm.py: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
