#include "template.hpp"
#include <string_view>

namespace clause::project {
std::string starter_template(const bool windows) {
    std::string result = R"PROJECT(# Clause project. Paths are relative to this TOML file's directory.
# Check with: clau --parse-check --project <this-file.toml>
# Also available: --preprocess-check, --print-pp, and --print-ast.
schema_version = 1

# Add another [[targets]] block after this target's option tables for more targets.
# All targets run by default; select with --target app (repeat for more names).
[[targets]]
name = "app"

# List files or patterns, e.g. ["main.erl", "src/*.erl", "shared/**/*.erl"].
# Patterns support *, ?, and ** as a whole directory component.
sources = []
# Recursively collect .erl files. Create/populate src or edit these selections.
# Set source_dirs = [] when selecting files exclusively through sources.
source_dirs = ["src"]
# Executable linked by `clau --project`; --emit/checks/printing do not write it. Windows: build/app.exe.
output = "@OUTPUT@"
# Clause's temporary files (and caches) go below this directory; --build-dir overrides it.
build_dir = "build"

[targets.options]
# Fallback roots for listed source filenames, e.g. ["src", "generated", "deps/*/src"].
# Patterns support *, ?, and ** as a whole directory component, like sources.
# These do not discover extra files or search for headers.
source_search_paths = []
# Header search directories, first listed first, e.g. ["include", "vendor/include", "deps/*/include"].
# Patterns expand to the matching directories. CLI -I directories take precedence; the last CLI -I
# is searched first.
include_dirs = []
# Predefined macros, e.g. ["DEBUG", "LIMIT=100", 'LABEL="demo"'].
# A bare name means true; values are Erlang literal terms. Duplicates are errors.
defines = []
# Empty lists preserve compiler feature defaults; CLI feature settings apply last.
# Example overrides: enable_features = ["maybe_expr"].
# Do not list a feature in both arrays.
enable_features = []
disable_features = []
# Sources never discovered, searched or compiled: paths and module names, both with wildcards, e.g.
# ignore_dirs = ["vendor/*/test", "**/test"], ignore_modules = ["legacy", "*_SUITE"]. Calls into such
# modules return {module_ignored, Module}.
ignore_dirs = []
ignore_modules = []
# Executables leave out exports nothing can call; true keeps every export callable by name.
keep_exports = false
# true also writes clause-epmd, Clause's epmd program, beside the executable.
epmd_executable = false
# false leaves the bundled epmd out of the executable; --epmd bundled then acts as --epmd none.
bundled_epmd = true
# Directories whose modules keep their inference results in the build cache (relative patterns match the
# trailing directories of any path), besides the implicit OTP ones "*/stdlib", "*/kernel", "*/compiler",
# "*/syntax_tools" and their versioned "*/stdlib-*" forms, e.g. ["*/deps/*"].
cache_inference_in_dirs = []

# Explicit include_lib application roots. Empty by default.
# Example entry: my_dependency = "vendor/my_dependency"
[targets.options.applications]
)PROJECT";
    const auto position = result.find("@OUTPUT@");
    result.replace(position, std::string_view("@OUTPUT@").size(), windows ? "build/app.exe" : "build/app");
    return result;
}

std::string starter_template() {
#ifdef _WIN32
    return starter_template(true);
#else
    return starter_template(false);
#endif
}
} // namespace clause::project
