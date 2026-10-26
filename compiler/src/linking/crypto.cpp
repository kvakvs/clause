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

// The crypto archive beside a runtime archive: its name with clause_runtime_crypto for clause_runtime.
std::filesystem::path crypto_archive(const std::filesystem::path &runtime) {
    auto name = project::path_text(runtime.filename());
    const auto at = name.rfind(RUNTIME_NAME);
    if (at == std::string::npos) {
        throw std::runtime_error("cannot locate the crypto runtime archive beside " + project::path_text(runtime));
    }
    name.replace(at, RUNTIME_NAME.size(), "clause_runtime_crypto");
    return runtime.parent_path() / project::native_path(name);
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

// libcrypto and its system libraries from an OpenSSL root: an installation, or a source tree built once.
std::vector<std::string> root_libraries(const std::filesystem::path &root, const CryptoLink &link,
                                        const std::filesystem::path &archive, std::span<const std::string> recorded) {
    const auto recipe = read_recipe(archive);
    const auto kind = openssl_kind(root);
    check_openssl_version(root, kind, recipe.minimum);
    const auto prefix = kind == OpensslKind::install ? root : built_openssl(root, recipe, link.cache);
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
    const auto archive = crypto_archive(runtime);
    std::error_code error;
    if (!std::filesystem::is_regular_file(archive, error)) {
        throw std::runtime_error("crypto runtime archive not found: " + project::path_text(archive) +
                                 "; build Clause with CLAUSE_RUNTIME_CRYPTO=ON (docs/crypto.md)");
    }
    auto recorded = runtime_link_inputs(archive);
    const auto root = openssl_root(link);
    auto libraries = root ? root_libraries(*root, link, archive, recorded) : std::move(recorded);
    std::ranges::transform(libraries, libraries.begin(), driver_library);
    return {utf8_path(archive), std::move(libraries)};
}
} // namespace clause::linking
