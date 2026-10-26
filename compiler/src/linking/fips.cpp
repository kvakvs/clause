// Created for crypto's FIPS mode (plan 21 step LIB79F).
#include "fips.hpp"
#include "../project/paths.hpp"
#include <algorithm>
#include <fstream>
#include <llvm/TargetParser/Triple.h>
#include <optional>
#include <regex>
#include <stdexcept>
#include <vector>

namespace clause::linking {
namespace {
// The directories of `root` a provider module is installed in: the root itself, then OpenSSL's module directories
// (lib/ossl-modules, lib64/ossl-modules and multiarch lib/<triplet>/ossl-modules).
std::vector<std::filesystem::path> module_directories(const std::filesystem::path &root) {
    std::vector<std::filesystem::path> directories{root, root / "lib" / "ossl-modules",
                                                   root / "lib64" / "ossl-modules"};
    std::error_code error;
    for (const auto &entry : std::filesystem::directory_iterator(root / "lib", error)) {
        if (entry.is_directory(error) && std::filesystem::is_directory(entry.path() / "ossl-modules", error)) {
            directories.push_back(entry.path() / "ossl-modules");
        }
    }
    return directories;
}

// The directories of `root` a fipsmodule.cnf is installed in: the root itself, then OpenSSL's OPENSSLDIR choices.
std::vector<std::filesystem::path> config_directories(const std::filesystem::path &root) {
    return {root, root / "ssl", root / "etc" / "ssl"};
}

// The first regular file `name` in `directories`.
std::optional<std::filesystem::path> first_file(const std::vector<std::filesystem::path> &directories,
                                                const std::string &name) {
    std::error_code error;
    const auto found = std::ranges::find_if(
        directories, [&](const auto &directory) { return std::filesystem::is_regular_file(directory / name, error); });
    return found == directories.end() ? std::nullopt : std::optional(*found / name);
}

// The directories searched, for a diagnostic.
std::string searched(const std::vector<std::filesystem::path> &directories) {
    std::string text;
    for (const auto &directory : directories) {
        text += (text.empty() ? "" : ", ") + project::path_text(directory);
    }
    return text;
}

// Check that a settings file has the provider's section and its integrity value, as the runtime reads them.
void check_config(const std::filesystem::path &config) {
    std::ifstream file(config);
    const std::regex section(R"(^\s*\[\s*fips_sect\s*\]\s*$)");
    const std::regex mac(R"(^\s*module-mac\s*=)");
    bool in_section = false;
    bool has_mac = false;
    for (std::string line; std::getline(file, line);) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.find('[') != std::string::npos && line.find(']') != std::string::npos) {
            in_section = std::regex_search(line, section);
        }
        has_mac = has_mac || (in_section && std::regex_search(line, mac));
    }
    if (!has_mac) {
        throw std::runtime_error("--fips: " + project::path_text(config) +
                                 " has no module-mac in a [fips_sect] section (run openssl fipsinstall)");
    }
}
} // namespace

std::string fips_module_name(const std::string &target_triple) {
    const llvm::Triple triple(target_triple);
    if (triple.isOSWindows()) {
        return "fips.dll";
    }
    return triple.isOSDarwin() ? "fips.dylib" : "fips.so";
}

FipsProvider find_fips_provider(const std::filesystem::path &root, const std::string &target_triple) {
    const auto name = fips_module_name(target_triple);
    const auto modules = module_directories(root);
    const auto module = first_file(modules, name);
    if (!module) {
        throw std::runtime_error("--fips: no FIPS provider module " + name + " in " + project::path_text(root) +
                                 " (searched " + searched(modules) + ")");
    }
    const auto configs = config_directories(root);
    const auto config = first_file(configs, FIPS_CONFIG_NAME);
    if (!config) {
        throw std::runtime_error("--fips: no " + std::string(FIPS_CONFIG_NAME) + " in " + project::path_text(root) +
                                 " (searched " + searched(configs) + ")");
    }
    check_config(*config);
    return {*module, *config};
}
} // namespace clause::linking
