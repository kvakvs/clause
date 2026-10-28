// Created for the crypto runtime archive (plan 21 step LIB79).
#include "crypto.hpp"
#include "../project/paths.hpp"
#include "openssl.hpp"
#include "toolchain.hpp"
#include <algorithm>
#include <llvm/Support/Process.h>
#include <span>
#include <stdexcept>

namespace clause::linking {
namespace {
constexpr std::string_view RUNTIME_NAME = "clause_runtime";

// An archive beside a runtime archive: its name with `name` for clause_runtime; throws when it was not built.
std::filesystem::path sibling_archive(const std::filesystem::path &runtime, std::string_view name,
                                      std::string_view option) {
    auto file = project::path_text(runtime.filename());
    const auto at = file.rfind(RUNTIME_NAME);
    if (at == std::string::npos) {
        throw std::runtime_error("cannot locate " + std::string(name) + " beside " + project::path_text(runtime));
    }
    file.replace(at, RUNTIME_NAME.size(), name);
    auto archive = runtime.parent_path() / project::native_path(file);
    std::error_code error;
    if (!std::filesystem::is_regular_file(archive, error)) {
        throw std::runtime_error(std::string(name) + " runtime archive not found: " + project::path_text(archive) +
                                 "; build Clause with " + std::string(option) + "=ON (docs/libraries/crypto.md)");
    }
    return archive;
}

// The OpenSSL to link instead of the recorded one: --with-crypto=PATH, else CLAUSE_OPENSSL_ROOT.
std::optional<std::filesystem::path> openssl_root(const CryptoLink &link) {
    if (link.openssl) {
        return project::absolute_path(std::filesystem::current_path(), *link.openssl);
    }
    if (const auto root = llvm::sys::Process::GetEnv("CLAUSE_OPENSSL_ROOT"); root && !root->empty()) {
        return project::native_path(*root);
    }
    return std::nullopt;
}

// A system library as the Clang driver takes it: OpenSSL names Windows ones as `ws2_32.lib`, which the driver
// would read as a file, so they become `-lws2_32`; paths and flags stay as they are.
std::string driver_library(const std::string &library) {
    const bool bare = library.find_first_of("/\\") == std::string::npos && !library.starts_with("-");
    return bare && library.ends_with(".lib") ? "-l" + library.substr(0, library.size() - 4) : library;
}

// The install prefix of an OpenSSL root: an installation, or a source tree built once.
std::filesystem::path openssl_prefix(const std::filesystem::path &root, const CryptoLink &link,
                                     const std::filesystem::path &archive) {
    const auto recipe = read_recipe(archive);
    const auto kind = openssl_kind(root);
    check_openssl_version(root, kind, recipe.minimum);
    return kind == OpensslKind::install ? root : built_openssl(root, recipe, link.cache);
}

// libcrypto and its system libraries of an OpenSSL prefix.
std::vector<std::string> prefix_libraries(const std::filesystem::path &prefix, std::span<const std::string> recorded) {
    std::vector<std::string> libraries{utf8_path(libcrypto_of(prefix))};
    const auto system = system_libraries(prefix);
    if (system) {
        libraries.insert(libraries.end(), system->begin(), system->end());
    } else if (!recorded.empty()) {
        libraries.insert(libraries.end(), recorded.begin() + 1, recorded.end());
    }
    return libraries;
}
} // namespace

CryptoInputs crypto_link_inputs(const std::filesystem::path &runtime, const CryptoLink &link) {
    const auto archive = sibling_archive(runtime, "clause_runtime_crypto", "CLAUSE_RUNTIME_CRYPTO");
    auto recorded = runtime_link_inputs(archive);
    const auto root = openssl_root(link);
    const auto prefix = root ? std::optional(openssl_prefix(*root, link, archive)) : std::nullopt;
    CryptoInputs inputs{{utf8_path(archive)}, prefix ? prefix_libraries(*prefix, recorded) : std::move(recorded)};
    // Added for the ssl runtime archive (plan 21 step LIB81B): it and libssl go before their crypto counterparts.
    if (link.ssl) {
        const auto ssl = sibling_archive(runtime, "clause_runtime_ssl", "CLAUSE_RUNTIME_CRYPTO");
        inputs.archives.insert(inputs.archives.begin(), utf8_path(ssl));
        auto libssl = prefix ? utf8_path(libssl_of(*prefix)) : runtime_link_inputs(ssl).at(0);
        inputs.libraries.insert(inputs.libraries.begin(), std::move(libssl));
    }
    std::ranges::transform(inputs.libraries, inputs.libraries.begin(), driver_library);
    return inputs;
}
} // namespace clause::linking
