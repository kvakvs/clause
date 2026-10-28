#include "link.hpp"
#include "../artifacts/paths.hpp"
#include "../project/paths.hpp"
#include "fips.hpp"
#include "toolchain.hpp"
#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <llvm/ADT/SmallString.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/Program.h>
#include <llvm/TargetParser/Triple.h>
#include <stdexcept>
#include <utility>

namespace clause::linking {
namespace {
// Remove a staging tree; a failure (even allocation) during cleanup only leaves the directory behind.
bool remove_tree(const std::filesystem::path &directory) noexcept {
    try {
        std::error_code error;
        std::filesystem::remove_all(directory, error);
        return !error;
    } catch (...) {
        return false;
    }
}

// Resolve the invocation-relative output, adding ".exe" for Windows targets when no extension is given.
std::filesystem::path executable_path(const LinkRequest &request) {
    auto path = project::absolute_path(std::filesystem::current_path(), request.output);
    if (!path.has_filename()) {
        throw std::runtime_error("output path has no file name: " + project::path_text(request.output));
    }
    if (llvm::Triple(request.target_triple).isOSWindows() && !path.has_extension()) {
        path += ".exe";
    }
    return path;
}

// Require an existing output directory and a replaceable destination that is not an input.
void check_destination(const std::filesystem::path &path, const std::span<const std::filesystem::path> inputs) {
    std::error_code error;
    if (!std::filesystem::is_directory(path.parent_path(), error)) {
        throw std::runtime_error("output directory does not exist: " + project::path_text(path.parent_path()));
    }
    artifacts::detail::validate_destination(path, inputs);
}

// Create the output directory of a manifest output; an existing directory is kept as it is.
void create_output_directory(const std::filesystem::path &directory) {
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) {
        throw std::runtime_error("cannot create output directory " + project::path_text(directory) + ": " +
                                 error.message());
    }
}

// Write each object under a short indexed name and return the paths in link order.
std::vector<std::string> stage_objects(const StagingDirectory &staging, const LinkRequest &request) {
    const auto extension = llvm::Triple(request.target_triple).isOSBinFormatCOFF() ? ".obj" : ".o";
    std::vector<std::string> paths;
    paths.reserve(request.objects.size());
    for (const auto &object : request.objects) {
        const auto path = staging.path() / ("m" + std::to_string(paths.size()) + extension);
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char *>(object.bytes.data()),
                   static_cast<std::streamsize>(object.bytes.size()));
        file.close();
        if (!file) {
            throw std::runtime_error("cannot write object for linking: " + project::path_text(path));
        }
        paths.push_back(utf8_path(path));
    }
    return paths;
}

// Spell unreferenced-section removal for the target's linker: ld64, MSVC-compatible COFF or GNU-style ELF/MinGW.
std::vector<std::string> strip_arguments(const llvm::Triple &triple) {
    if (triple.isOSBinFormatMachO()) {
        return {"-Wl,-dead_strip"};
    }
    if (triple.isWindowsMSVCEnvironment()) {
        return {"-Wl,/OPT:REF", "-Wl,/OPT:ICF"};
    }
    return {"-Wl,--gc-sections"};
}

// Keep debug information: -g, and for MSVC targets a PDB that the executable names without its staging directory.
std::vector<std::string> debug_arguments(const llvm::Triple &triple) {
    if (triple.isWindowsMSVCEnvironment()) {
        return {"-g", "-Wl,/PDBALTPATH:%_PDB%"};
    }
    return {"-g"};
}

// LLD reads this LLVM's bitcode; other linkers may not, so link-time optimization is refused for their targets.
void check_lto(const std::string &target) {
    const llvm::Triple triple(target);
    if (!triple.isWindowsMSVCEnvironment() && !triple.isOSBinFormatELF()) {
        throw std::runtime_error("--lto is not supported for target " + target +
                                 ": it needs LLD (Windows MSVC or ELF targets)");
    }
}

// Build the Clang driver command: C++ link mode, explicit target, staged output, objects, then the runtime.
std::vector<std::string> link_arguments(const LinkRequest &request, const std::filesystem::path &staged,
                                        std::vector<std::string> objects, const std::filesystem::path &runtime) {
    std::vector<std::string> arguments;
    arguments.reserve(objects.size() + 8);
    arguments.insert(arguments.end(),
                     {"--driver-mode=g++", "--target=" + request.target_triple, "-o", utf8_path(staged)});
    // An embedded manifest runs Windows programs as the invoking user; without one Windows asks to elevate 32-bit
    // programs whose names look like installers (record_update.exe), and a separate manifest file would be left out.
    if (llvm::Triple(request.target_triple).isWindowsMSVCEnvironment()) {
        arguments.emplace_back("-Wl,/MANIFEST:EMBED");
    }
    if (request.strip_unused) {
        std::ranges::move(strip_arguments(llvm::Triple(request.target_triple)), std::back_inserter(arguments));
    }
    if (request.debug_info) {
        std::ranges::move(debug_arguments(llvm::Triple(request.target_triple)), std::back_inserter(arguments));
    }
    if (request.lto) {
        arguments.insert(arguments.end(), {"-flto", "-fuse-ld=lld"});
    }
    arguments.insert(arguments.end(), std::make_move_iterator(objects.begin()), std::make_move_iterator(objects.end()));
    // The crypto archive calls into the runtime and libcrypto, so it goes before both (ELF archives resolve in order).
    auto crypto = request.crypto ? std::optional(crypto_link_inputs(runtime, *request.crypto)) : std::nullopt;
    if (crypto) {
        std::ranges::move(crypto->archives, std::back_inserter(arguments));
    }
    arguments.push_back(utf8_path(runtime));
    std::ranges::move(runtime_link_inputs(runtime), std::back_inserter(arguments));
    if (crypto) {
        std::ranges::move(crypto->libraries, std::back_inserter(arguments));
    }
    return arguments;
}

// Turn a failed driver run into one diagnostic that carries the linker's own output.
std::runtime_error link_failure(const std::filesystem::path &output, const std::string &linker, const LinkerRun &run) {
    auto message = "linking " + project::path_text(output) + " failed: " + linker + " exited with status " +
                   std::to_string(run.status);
    if (!run.output.empty()) {
        message += ":\n" + run.output;
    }
    return std::runtime_error(message);
}
} // namespace

StagingDirectory::StagingDirectory(const std::filesystem::path &parent) {
    llvm::SmallString<256> created;
    if (const auto error = llvm::sys::fs::createUniqueDirectory(utf8_path(parent / ".clause-link"), created)) {
        throw std::runtime_error("cannot create link staging directory in " + project::path_text(parent) + ": " +
                                 error.message());
    }
    directory = project::native_path(created.str());
}

StagingDirectory::~StagingDirectory() {
    if (!directory.empty()) {
        static_cast<void>(remove_tree(directory));
    }
}

StagingDirectory::StagingDirectory(StagingDirectory &&other) noexcept : directory(std::exchange(other.directory, {})) {}

namespace {
// The Clang driver a link would run; empty when none is found (an actual link reports that).
std::string linker_or_empty(const std::optional<std::filesystem::path> &linker) {
    try {
        return find_linker(linker);
    } catch (const std::runtime_error &) {
        return {};
    }
}
} // namespace

LinkTools resolve_link_tools(const LinkRequest &request) {
    return {executable_path(request), linker_or_empty(request.linker), runtime_library_path(request.runtime_library)};
}

namespace {
// Gather a Darwin executable's DWARF into a dSYM bundle while the staged objects it names still exist (clang -g
// links keep it there); `output` names the executable in failures. Added for macOS debugging.
std::filesystem::path gather_dsym(const std::string &linker, const std::filesystem::path &staged,
                                  const std::string &output) {
    const auto tool =
        llvm::sys::findProgramByName("dsymutil", {utf8_path(std::filesystem::path(linker).parent_path())});
    if (!tool) {
        throw std::runtime_error("linking " + output + " with -g needs dsymutil on PATH");
    }
    auto bundle = staged;
    bundle += ".dSYM";
    const std::array<std::string, 3> arguments{utf8_path(staged), "-o", utf8_path(bundle)};
    const auto run = run_linker(*tool, arguments, staged.parent_path() / "dsymutil.log");
    std::error_code error;
    if (run.status != 0 || !std::filesystem::is_directory(bundle, error)) {
        throw std::runtime_error("dsymutil failed for " + output + ":\n" + run.output);
    }
    return bundle;
}

// The staged debug symbols published beside the output: an MSVC target's PDB or a Darwin target's dSYM bundle;
// none without -g or for other targets.
std::filesystem::path staged_symbols(const LinkRequest &request, const std::string &linker,
                                     const std::filesystem::path &staged, const std::filesystem::path &output) {
    const llvm::Triple triple(request.target_triple);
    if (request.debug_info && triple.isOSDarwin()) {
        return gather_dsym(linker, staged, project::path_text(output));
    }
    if (!request.debug_info || !triple.isWindowsMSVCEnvironment()) {
        return {};
    }
    auto symbols = std::filesystem::path(staged).replace_extension(".pdb");
    std::error_code error;
    if (!std::filesystem::is_regular_file(symbols, error)) {
        throw std::runtime_error("linking " + project::path_text(output) + " produced no PDB");
    }
    return symbols;
}

// Publish staged debug symbols: a dSYM bundle as <output>.dSYM, replacing the previous bundle; a PDB beside the
// output with its extension.
void publish_symbols(const StagedExecutable &executable) {
    if (executable.symbols.extension() != ".dSYM") {
        const auto symbols = std::filesystem::path(executable.output).replace_extension(".pdb");
        check_destination(symbols, executable.protected_inputs);
        artifacts::detail::replace(executable.symbols, symbols);
        return;
    }
    auto bundle = executable.output;
    bundle += ".dSYM";
    std::filesystem::remove_all(bundle);
    std::filesystem::rename(executable.symbols, bundle);
}

// The FIPS provider a pinned program copies beside it, found before linking so a missing one fails the build.
// Added for crypto's FIPS mode (plan 21 step LIB79F).
std::optional<FipsProvider> pinned_provider(const LinkRequest &request) {
    if (!request.crypto || !request.crypto->fips) {
        return std::nullopt;
    }
    const auto root = project::absolute_path(std::filesystem::current_path(), *request.crypto->fips);
    return find_fips_provider(root, request.target_triple);
}

// Copy a pinned program's FIPS provider into the staging directory; the copies and their destinations.
std::vector<std::pair<std::filesystem::path, std::filesystem::path>> stage_provider(const StagingDirectory &staging,
                                                                                    const FipsProvider &provider,
                                                                                    const std::filesystem::path &output,
                                                                                    const std::string &target_triple) {
    const std::array files{std::pair{provider.module, fips_module_name(target_triple)},
                           std::pair{provider.config, std::string(FIPS_CONFIG_NAME)}};
    std::vector<std::pair<std::filesystem::path, std::filesystem::path>> companions;
    for (const auto &[source, name] : files) {
        const auto staged = staging.path() / name;
        std::error_code error;
        if (!std::filesystem::copy_file(source, staged, std::filesystem::copy_options::overwrite_existing, error)) {
            throw std::runtime_error("--fips: cannot copy " + project::path_text(source) + ": " + error.message());
        }
        companions.emplace_back(staged, output.parent_path() / name);
    }
    return companions;
}
} // namespace

StagedExecutable stage_executable(const LinkRequest &request) {
    const auto output = executable_path(request);
    const auto provider = pinned_provider(request);
    if (request.create_directory) {
        create_output_directory(output.parent_path());
    }
    check_destination(output, request.protected_inputs);
    if (request.lto) {
        check_lto(request.target_triple);
    }
    const auto linker = find_linker(request.linker);
    const auto runtime = find_runtime_library(request.runtime_library);
    check_runtime_target(runtime, request.target_triple);
    StagingDirectory staging(output.parent_path());
    auto staged = staging.path() / output.filename();
    const auto arguments = link_arguments(request, staged, stage_objects(staging, request), runtime);
    auto run = run_linker(linker, arguments, staging.path() / "link.log");
    std::error_code error;
    if (run.status != 0 || !std::filesystem::is_regular_file(staged, error)) {
        throw link_failure(output, linker, run);
    }
    auto symbols = staged_symbols(request, linker, staged, output);
    auto companions = provider ? stage_provider(staging, *provider, output, request.target_triple)
                               : std::vector<std::pair<std::filesystem::path, std::filesystem::path>>{};
    return {std::move(staging),
            std::move(staged),
            output,
            {request.protected_inputs.begin(), request.protected_inputs.end()},
            std::move(run.output),
            std::move(symbols),
            std::move(companions)};
}

void publish_executable(const StagedExecutable &executable) {
    check_destination(executable.output, executable.protected_inputs);
    artifacts::detail::replace(executable.staged, executable.output);
    if (!executable.symbols.empty()) {
        publish_symbols(executable);
    }
    for (const auto &[staged, destination] : executable.companions) {
        check_destination(destination, executable.protected_inputs);
        artifacts::detail::replace(staged, destination);
    }
}

std::string link_executable(const LinkRequest &request) {
    const auto executable = stage_executable(request);
    publish_executable(executable);
    return executable.warnings;
}
} // namespace clause::linking
