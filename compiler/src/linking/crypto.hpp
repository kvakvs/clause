#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// Linking the crypto runtime archive (plan 21 step LIB79, docs/libraries/crypto.md): clause_runtime_crypto beside the
// runtime archive, then OpenSSL's libcrypto and its system libraries.
namespace clause::linking {
struct CryptoLink {
    // An OpenSSL installation or source tree to link instead of the one the build recorded (--with-crypto=PATH);
    // without it the CLAUSE_OPENSSL_ROOT environment variable, else the recorded libcrypto.
    std::optional<std::filesystem::path> openssl;
    // Where a source tree is configured and built once (the build cache).
    std::filesystem::path cache;
    // A user-supplied FIPS provider the program is pinned to (--fips=PATH): copied beside the executable. Added for
    // crypto's FIPS mode (plan 21 step LIB79F).
    std::optional<std::filesystem::path> fips = std::nullopt;
};

// The crypto archive's linker inputs: the archive, which goes before the runtime archive, and libcrypto with its
// system libraries, which go after it.
struct CryptoInputs {
    std::string archive;
    std::vector<std::string> libraries;
};

// The inputs of linking the crypto archive beside `runtime`; throws std::runtime_error when the archive or an
// OpenSSL cannot be used.
CryptoInputs crypto_link_inputs(const std::filesystem::path &runtime, const CryptoLink &link);
} // namespace clause::linking
