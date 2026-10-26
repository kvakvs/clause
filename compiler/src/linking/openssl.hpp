#pragma once
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// OpenSSL installations and source trees for clau --with-crypto=PATH (plan 21 step LIB79, docs/compile.md#openssl).
namespace clause::linking {
// How the build configured OpenSSL, as clause_runtime_crypto.openssl beside the crypto archive records it.
struct OpensslRecipe {
    // The oldest OpenSSL the crypto archive links with.
    std::string minimum = "3.4";
    // Configure's target (empty: detected) and options.
    std::string target;
    std::vector<std::string> options;
    // The C compiler and the directories of perl and nasm the build used (Windows).
    std::string compiler;
    std::vector<std::string> tools;
    // The recipe text, part of a source build's identity.
    std::string text;
};

// An installed prefix (include/openssl/opensslv.h) or a source tree (Configure, VERSION.dat).
enum class OpensslKind : std::uint8_t { install, source };

// The recipe beside the crypto archive; the defaults when it is missing.
OpensslRecipe read_recipe(const std::filesystem::path &archive);
// What `root` is; throws std::runtime_error for neither.
OpensslKind openssl_kind(const std::filesystem::path &root);
// Throw std::runtime_error when the OpenSSL under `root` is older than `minimum`.
void check_openssl_version(const std::filesystem::path &root, OpensslKind kind, const std::string &minimum);
// libcrypto of an install prefix, its static archive first; throws std::runtime_error when there is none.
std::filesystem::path libcrypto_of(const std::filesystem::path &prefix);
// The system libraries a source build recorded beside its libcrypto (ex_libs.txt); none for other prefixes.
std::optional<std::vector<std::string>> system_libraries(const std::filesystem::path &prefix);
// The install prefix of `source` built with `recipe`, building it under `cache` on first use.
std::filesystem::path built_openssl(const std::filesystem::path &source, const OpensslRecipe &recipe,
                                    const std::filesystem::path &cache);
} // namespace clause::linking
