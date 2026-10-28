// Created for the crypto runtime archive (plan 21 step LIB79).
#include "openssl.hpp"
#include "../project/paths.hpp"
#include "toolchain.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstdlib>
#include <fstream>
#include <llvm/Support/Process.h>
#include <llvm/Support/Program.h>
#include <llvm/Support/xxhash.h>
#include <set>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <utility>

namespace clause::linking {
namespace {
// The non-empty lines of a text file, without line ends; none when it cannot be read.
std::vector<std::string> lines_of(const std::filesystem::path &file) {
    std::ifstream in(file, std::ios::binary);
    std::vector<std::string> lines;
    for (std::string line; std::getline(in, line);) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (!line.empty()) {
            lines.push_back(std::move(line));
        }
    }
    return lines;
}

// The parts of `text` between `separator`s, empty ones left out.
std::vector<std::string> split(const std::string &text, char separator) {
    std::vector<std::string> parts;
    std::istringstream in(text);
    for (std::string part; std::getline(in, part, separator);) {
        if (!part.empty()) {
            parts.push_back(std::move(part));
        }
    }
    return parts;
}

// Store one key=value line of a recipe.
void recipe_entry(OpensslRecipe &recipe, const std::string &line) {
    const auto equals = line.find('=');
    const auto key = line.substr(0, equals);
    const auto value = equals == std::string::npos ? std::string() : line.substr(equals + 1);
    if (key == "minimum" && !value.empty()) {
        recipe.minimum = value;
    } else if (key == "target") {
        recipe.target = value;
    } else if (key == "options") {
        recipe.options = split(value, ' ');
    } else if (key == "cc") {
        recipe.compiler = value;
    } else if (key == "tools") {
        recipe.tools = split(value, '|');
    }
}

// The numeric parts of a dotted version.
std::vector<int> version_parts(const std::string &version) {
    std::vector<int> parts;
    for (const auto &part : split(version, '.')) {
        int number = 0;
        const auto parsed = std::from_chars(part.data(), part.data() + part.size(), number);
        parts.push_back(parsed.ec == std::errc{} ? number : 0);
    }
    return parts;
}

// The MAJOR.MINOR.PATCH version of a source tree's VERSION.dat.
std::string source_version(const std::filesystem::path &root) {
    std::array<std::string, 3> parts{"0", "0", "0"};
    constexpr std::array<std::string_view, 3> KEYS{"MAJOR=", "MINOR=", "PATCH="};
    for (const auto &line : lines_of(root / "VERSION.dat")) {
        for (std::size_t index = 0; index < KEYS.size(); ++index) {
            if (line.starts_with(KEYS[index])) {
                parts[index] = line.substr(KEYS[index].size());
            }
        }
    }
    return parts[0] + "." + parts[1] + "." + parts[2];
}

// The version an installation's opensslv.h states (OPENSSL_VERSION_STR).
std::string installed_version(const std::filesystem::path &root) {
    for (const auto &line : lines_of(root / "include" / "openssl" / "opensslv.h")) {
        const auto at = line.find("OPENSSL_VERSION_STR \"");
        if (at != std::string::npos) {
            const auto start = line.find('"', at) + 1;
            return line.substr(start, line.find('"', start) - start);
        }
    }
    return "0";
}

// Set an environment variable of this process, which the build commands inherit.
void set_environment(const char *name, const std::string &value) {
#ifdef _WIN32
    _putenv_s(name, value.c_str());
#else
    setenv(name, value.c_str(), 1);
#endif
}

// A program from the recipe's tool directories, else PATH; throws std::runtime_error with `hint` when missing.
std::string program(const std::string &name, const OpensslRecipe &recipe, const std::string &hint) {
    std::vector<llvm::StringRef> directories(recipe.tools.begin(), recipe.tools.end());
    auto found = llvm::sys::findProgramByName(name, directories);
    if (!found) {
        found = llvm::sys::findProgramByName(name);
    }
    if (!found) {
        throw std::runtime_error("cannot build OpenSSL from source: " + name + " not found; " + hint);
    }
    return *found;
}

// Restores the working directory a source build changed (Configure and make write into it).
class WorkingDirectory {
  public:
    explicit WorkingDirectory(const std::filesystem::path &directory) : previous_(std::filesystem::current_path()) {
        std::filesystem::current_path(directory);
    }

    ~WorkingDirectory() {
        std::error_code error;
        std::filesystem::current_path(previous_, error);
    }

    WorkingDirectory(const WorkingDirectory &) = delete;
    WorkingDirectory &operator=(const WorkingDirectory &) = delete;

  private:
    // The directory to return to.
    std::filesystem::path previous_;
};

// Run one build command in the directory of `log`, its output in `log`; throws std::runtime_error when it fails.
void run_step(const std::string &command, const std::vector<std::string> &arguments, const std::filesystem::path &log) {
    std::vector<llvm::StringRef> argv{command};
    argv.insert(argv.end(), arguments.begin(), arguments.end());
    const auto log_text = utf8_path(log);
    const std::array<std::optional<llvm::StringRef>, 3> redirects{llvm::StringRef(), llvm::StringRef(log_text),
                                                                  llvm::StringRef(log_text)};
    std::string error;
    const WorkingDirectory inside(log.parent_path());
    if (llvm::sys::ExecuteAndWait(command, argv, std::nullopt, redirects, 0, 0, &error) != 0) {
        throw std::runtime_error("building OpenSSL failed: " + command + " " + (arguments.empty() ? "" : arguments[0]) +
                                 (error.empty() ? "" : ": " + error) + "; see " + project::path_text(log));
    }
}

// The make command and its leading arguments: nmake in a Visual Studio environment, else make with parallel jobs.
std::vector<std::string> make_command(const OpensslRecipe &recipe) {
#ifdef _WIN32
    return {program("nmake", recipe, "run clau from a Visual Studio developer shell"), "/nologo"};
#else
    const auto jobs = std::max(1U, std::thread::hardware_concurrency() * 2 / 3);
    return {program("make", recipe, "install make"), "-j" + std::to_string(jobs)};
#endif
}

// Put the recipe's tool directories first on PATH and select its C compiler, for the build commands.
void build_environment(const OpensslRecipe &recipe) {
    std::string path;
    for (const auto &tool : recipe.tools) {
        path.append(tool).push_back(llvm::sys::EnvPathSeparator);
    }
    set_environment("PATH", path.append(llvm::sys::Process::GetEnv("PATH").value_or("")));
    std::error_code error;
    if (!recipe.compiler.empty() && std::filesystem::exists(project::native_path(recipe.compiler), error)) {
        set_environment("CC", recipe.compiler);
    }
}

// The features a source tree's Configure can enable or disable: the names in its `my @disablables... = (...);` lists
// (comments dropped). Added for the OpenSSL 3.1.2 minimum.
// Add the names of one list's body to `names`, skipping Perl comments.
void list_names(std::string_view body, std::set<std::string> &names) {
    std::string name;
    bool comment = false;
    for (const char c : body) {
        comment = c == '#' || (comment && c != '\n');
        if (!comment && (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-')) {
            name.push_back(c);
        } else if (!name.empty()) {
            names.insert(std::exchange(name, {}));
        }
    }
}

std::set<std::string> disablable_features(const std::string &configure) {
    std::set<std::string> names;
    constexpr std::string_view LIST = "my @disablables";
    for (auto at = configure.find(LIST); at != std::string::npos; at = configure.find(LIST, at + LIST.size())) {
        const auto open = configure.find('(', at);
        const auto close = configure.find(';', at);
        if (open != std::string::npos && close != std::string::npos && open < close) {
            list_names(std::string_view(configure).substr(open + 1, close - open - 1), names);
        }
    }
    return names;
}

// The recipe options the source tree knows: enable-X and no-X are kept when its Configure can enable or disable X
// (older trees lack later options, such as 3.1's no-docs and enable-ec_explicit_curves).
std::vector<std::string> known_options(const std::filesystem::path &source, const std::vector<std::string> &options) {
    std::ifstream file(source / "Configure", std::ios::binary);
    const auto features =
        disablable_features(std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>()));
    std::vector<std::string> known;
    for (const auto &option : options) {
        const auto prefix = option.starts_with("enable-") ? 7U : option.starts_with("no-") ? 3U : 0U;
        if (prefix == 0 || features.contains(option.substr(prefix))) {
            known.push_back(option);
        }
    }
    return known;
}

// OpenSSL 3.x's Windows install_dev of a static build copies ossl_static.pdb, which clang-cl's embedded debug
// information (/Z7) never writes: give it an empty one (fixed in 4.0). Added for the OpenSSL 3.1.2 minimum.
void static_library_symbols(const std::filesystem::path &build) {
    const auto symbols = build / "ossl_static.pdb";
    std::error_code error;
    if (std::filesystem::is_regular_file(build / "makefile", error) && !std::filesystem::exists(symbols, error)) {
        std::ofstream{symbols};
    }
}

// Configure `source` into `directory`/build, then build its libraries and install them into `directory`/install.
void build_source(const std::filesystem::path &source, const OpensslRecipe &recipe,
                  const std::filesystem::path &directory) {
    const auto build = directory / "build";
    const auto install = directory / "install";
    std::filesystem::remove_all(build);
    std::filesystem::create_directories(build);
    build_environment(recipe);
    std::vector<std::string> configure{utf8_path(source / "Configure")};
    if (!recipe.target.empty()) {
        configure.push_back(recipe.target);
    }
    const auto options = known_options(source, recipe.options);
    configure.insert(configure.end(), options.begin(), options.end());
    configure.insert(configure.end(),
                     {"--prefix=" + utf8_path(install), "--openssldir=" + utf8_path(install / "ssl"), "--libdir=lib"});
    run_step(program("perl", recipe, "install Perl"), configure, build / "configure.log");
    auto make = make_command(recipe);
    const auto tool = make.front();
    make.erase(make.begin());
    for (const auto *target : {"build_libs", "install_dev"}) {
        if (std::string_view(target) == "install_dev") {
            static_library_symbols(build);
        }
        auto arguments = make;
        arguments.emplace_back(target);
        run_step(tool, arguments, build / (std::string(target) + ".log"));
    }
}

// The system libraries one makefile line names (CNF_EX_LIBS= or EX_LIBS=); none for other lines.
std::vector<std::string> makefile_libraries(const std::string &line) {
    for (const std::string_view key : {"CNF_EX_LIBS=", "EX_LIBS="}) {
        if (line.starts_with(key)) {
            return split(line.substr(key.size()), ' ');
        }
    }
    return {};
}

// The directories of an install prefix that may hold libcrypto: lib, lib64 and lib's subdirectories (Debian's
// multiarch lib/x86_64-linux-gnu).
std::vector<std::filesystem::path> library_directories(const std::filesystem::path &prefix) {
    std::vector<std::filesystem::path> directories{prefix / "lib", prefix / "lib64"};
    std::error_code error;
    for (const auto &entry : std::filesystem::directory_iterator(prefix / "lib", error)) {
        if (entry.is_directory(error)) {
            directories.push_back(entry.path());
        }
    }
    return directories;
}

// Record the system libraries the build's makefile names beside its libcrypto (install/ex_libs.txt).
void record_libraries(const std::filesystem::path &directory) {
    std::ofstream out(directory / "install" / "ex_libs.txt", std::ios::binary);
    for (const auto &line : lines_of(directory / "build" / "makefile")) {
        for (const auto &library : makefile_libraries(line)) {
            out << library << '\n';
        }
    }
}
} // namespace

OpensslRecipe read_recipe(const std::filesystem::path &archive) {
    OpensslRecipe recipe;
    auto file = archive;
    file.replace_extension(".openssl");
    for (const auto &line : lines_of(file)) {
        recipe_entry(recipe, line);
        recipe.text += line + "\n";
    }
    return recipe;
}

OpensslKind openssl_kind(const std::filesystem::path &root) {
    std::error_code error;
    if (std::filesystem::is_regular_file(root / "Configure", error) &&
        std::filesystem::is_regular_file(root / "VERSION.dat", error)) {
        return OpensslKind::source;
    }
    if (std::filesystem::is_regular_file(root / "include" / "openssl" / "opensslv.h", error)) {
        return OpensslKind::install;
    }
    throw std::runtime_error(project::path_text(root) +
                             " is neither an OpenSSL source tree (Configure, VERSION.dat) nor an OpenSSL installation "
                             "(include/openssl/opensslv.h)");
}

void check_openssl_version(const std::filesystem::path &root, OpensslKind kind, const std::string &minimum) {
    const auto version = kind == OpensslKind::source ? source_version(root) : installed_version(root);
    if (version_parts(version) < version_parts(minimum)) {
        throw std::runtime_error("OpenSSL " + version + " under " + project::path_text(root) + " is older than " +
                                 minimum + ", the oldest the crypto runtime links with");
    }
}

std::filesystem::path libcrypto_of(const std::filesystem::path &prefix) {
    constexpr std::array NAMES{"libcrypto.lib", "libcrypto_static.lib", "libcrypto.a",
                               "crypto.lib",    "libcrypto.dylib",      "libcrypto.so"};
    for (const auto *name : NAMES) {
        for (const auto &directory : library_directories(prefix)) {
            std::error_code error;
            if (std::filesystem::is_regular_file(directory / name, error)) {
                return directory / name;
            }
        }
    }
    throw std::runtime_error("no libcrypto under " + project::path_text(prefix / "lib"));
}

std::filesystem::path libssl_of(const std::filesystem::path &prefix) {
    const auto crypto = libcrypto_of(prefix);
    auto name = project::path_text(crypto.filename());
    name.replace(name.find("crypto"), 6, "ssl");
    auto ssl = crypto.parent_path() / project::native_path(name);
    std::error_code error;
    if (!std::filesystem::is_regular_file(ssl, error)) {
        throw std::runtime_error("no libssl beside " + project::path_text(crypto));
    }
    return ssl;
}

std::optional<std::vector<std::string>> system_libraries(const std::filesystem::path &prefix) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(prefix / "ex_libs.txt", error)) {
        return std::nullopt;
    }
    return lines_of(prefix / "ex_libs.txt");
}

std::filesystem::path built_openssl(const std::filesystem::path &source, const OpensslRecipe &recipe,
                                    const std::filesystem::path &cache) {
    std::ifstream version(source / "VERSION.dat", std::ios::binary);
    const std::string identity = utf8_path(std::filesystem::absolute(source)) + "\n" + recipe.text + "\n" +
                                 std::string(std::istreambuf_iterator<char>(version), {});
    const auto hash = llvm::xxh3_64bits(llvm::StringRef(identity));
    std::ostringstream name;
    name << "openssl-" << std::hex << hash;
    const auto directory = cache / "openssl" / name.str();
    const auto install = directory / "install";
    std::error_code error;
    if (std::filesystem::is_regular_file(install / ".complete", error)) {
        return install;
    }
    build_source(source, recipe, directory);
    record_libraries(directory);
    std::ofstream(install / ".complete", std::ios::binary) << identity;
    return install;
}
} // namespace clause::linking
