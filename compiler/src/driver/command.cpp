#include "../project/command.hpp"
#include "environment.hpp"
#include "frontend.hpp"
#include "options.hpp"
#include <iostream>

namespace clause::cli {

constexpr std::string_view help = R"(Usage: clau [options] <source.erl>...

Ahead-of-time compiler for Erlang/OTP 29.

Options:
  -h, --help           Show this help and exit.
      --version        Show the tool version and exit.
      --verbose        Trace inputs and compilation phases to stderr with [pp]/[parse]/[comp].
      --print-stage-times  Print preprocess/parse/transform time per file, compile time per batch with its phases,
                           inference passes and slowest inferred functions, and link time to stderr.
  -o, --output <path>  Link an executable (Windows targets add .exe when no extension is given).
      --entry <module[:function[/arity]]>  Select the executable entry: function/1 receives argv, function/0
                         runs without it; without /arity, /1 is preferred (default function: main).
      --build-dir <dir>  Keep temporary files below <dir>/tmp and caches below <dir>/cache (projects default:
                         <manifest-dir>/build).
      --no-cache       Neither read nor write build caches.
      --cache-inference-in-dirs <pattern>  Keep inference results of modules in matching directories in the build
                         cache (relative patterns match trailing directories; besides */stdlib, */kernel, ...).
      --linker <path>   Clang driver used to link executables (default: clang++ or clang on PATH).
      --runtime-library <path>  Runtime archive linked into executables (default: the one built with clau).
      --emit <obj|llvm-ir|llvm-bc>  Select one artifact per module (default: in memory).
      --artifact-dir <dir>  Override artifact root; requires --emit.
      --target-triple <triple>  Select machine/OS/ABI, independently of --target.
      -O0 | -O2 | -Os    Select generic O0 (default), speed (specialization, LLVM O2) or size (LLVM Os, unused code stripped at link).
      --no-type-specialization  Disable variants regardless of optimization option order.
      --jobs <n>       Optimize and emit object modules on n threads (default: every hardware thread).
  -g                   Emit Erlang source line tables; linked executables keep them for debuggers.
      --lto            Link modules as bitcode with link-time optimization (LLD: Windows MSVC and ELF targets).
      --keep-exports   Keep every export of an executable callable; by default unreachable ones are left out.
      --print-ir          Print verified IR with Erlang source comments before LLVM optimization.
      --print-types       Print each module as source annotated with inferred types; stop before LLVM lowering.
      --print-optimized-ir  Print verified IR with Erlang source comments after the selected pipeline.
      --print-asm         Print each module's assembly, commented with the IR and Erlang lines it came from.
      --preprocess-check  Preprocess each module and report diagnostics only.
      --parse-check      Preprocess and parse; report syntax diagnostics only.
      --print-pp         Print preprocessed Erlang source to stdout.
      --print-ast        Parse and print an indented syntax tree to stdout.
      --print-source     Parse and print each module as Erlang source to stdout.
      --print-abstr      Parse and print each module as OTP abstract format forms (erlc +to_abstr) to stdout.
      --parse-transform <module>  Apply a parse transform to every module, before its own -compile ones.
      --transform-path <dir>  Look for precompiled transform modules (.beam) in a directory; repeatable.
      --erl <path|none>  Host Erlang/OTP 29 erl that runs parse transforms (default: erl on PATH); none: never.
      --print-inputs     Parse the inputs, add the modules they name, print every source of the batch; do not compile.
      --print-env        Print the resolved compile environment as TOML and exit without processing sources.
  -I, --include <dir>  Add an include directory (last supplied is searched first).
  -D, --define <name[=term]>  Predefine a macro (default value: true).
      --app-dir <app=dir>  Map an include_lib application to a directory.
      --enable-feature <name>  Enable an OTP 29.1 feature.
      --disable-feature <name>  Disable an OTP 29.1 feature.
      --               Treat all remaining arguments as input paths.

Checks do not validate semantics or run parse transforms; compiling and --print-ast/-source/-abstr do.
--print-inputs replaces other check/print actions. It lists the inputs (project globs and source_search_paths
resolved), then each module source found by name in source_search_paths or the library as
<path> (referenced at <file>:<line>), naming where the batch first refers to that module.
--print-env validates the whole command, then prints it with its actions, resolved sources, search and include
directories, and every effective option or default (triple, optimization, outputs, linker, runtime, features).
With no check/print action, source batches compile to verified objects in memory.
Only --emit writes module artifacts (default root: build/aot); --output links an executable.
Compilation switches conflict with frontend check/print actions and --new-project.
--emit conflicts with --output; with --project, --output needs exactly one selected target.
Without --entry, --output uses the only module exporting main/1; the entry receives argv strings.
A source whose first line starts with #! is an escript: implicit -module and main/1 export.
IR inspection accepts target/optimization/preprocessing options, but rejects emission/output options.
Type inspection accepts preprocessing/project/verbosity options; it rejects other actions and backend policy.
Type reports describe conservative analysis and are not an intermediate stage input format.
Multiple IR snapshots use LLVM-comment headers; use --emit llvm-ir for separate machine-readable files.
Value options and optimization levels may appear only once.
Input paths may contain spaces when quoted by the shell.
)";

// Validate the request and dispatch explicit actions or the default compiler pipeline.
int run_command(const std::span<char *> arguments) {
    clause::cli::Options options;
    // Every target and module of the run share one, so duplicates warn once. Added for versioned include_lib.
    options.preprocessing.application_versions = std::make_shared<clause::ApplicationVersions>();
    if (const auto error = clause::cli::parse_options(arguments, options)) {
        std::cerr << "clau: error: " << *error << "\nTry 'clau --help' for usage.\n";
        return 2;
    }
    if (options.show_help) {
        std::cout << help << clause::project::help();
        return 0;
    }
    if (options.show_version) {
        std::cout << "clau " << CLAUSE_VERSION << '\n';
        return 0;
    }

    if (clause::project::active(options.project)) {
        return run_project(options, arguments);
    }

    if (!clause::cli::validate_inputs(options.inputs)) {
        return 1;
    }
    if (options.print_env) {
        clause::cli::print_positional_environment(arguments, options);
        return 0;
    }

    return clause::cli::process_inputs(options);
}

} // namespace clause::cli
