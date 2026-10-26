#pragma once
#include <filesystem>
#include <string>

// The user-supplied OpenSSL FIPS provider of a program built with clau --fips (plan 21 step LIB79F,
// docs/crypto.md#fips): its module and settings are copied beside the executable, whose start loads them.
namespace clause::linking {
struct FipsProvider {
    // The provider module (fips.dll, fips.so or fips.dylib) and the fipsmodule.cnf `openssl fipsinstall` wrote.
    std::filesystem::path module;
    std::filesystem::path config;
};

// The settings file name beside a pinned executable.
inline constexpr const char *FIPS_CONFIG_NAME = "fipsmodule.cnf";

// The module file name a pinned executable of `target_triple` loads beside it.
std::string fips_module_name(const std::string &target_triple);

// The FIPS provider in `root`, a directory holding both files or an OpenSSL installation; throws
// std::runtime_error naming the places searched, or what the settings file lacks.
FipsProvider find_fips_provider(const std::filesystem::path &root, const std::string &target_triple);
} // namespace clause::linking
