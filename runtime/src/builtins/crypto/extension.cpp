#include "crypto.hpp"
#include <array>
#include <clause/abi/startup.hpp>

// The crypto runtime archive's extension entry (plan 21 step LIB79): a program whose startup descriptor lists it
// links this archive, and startup registers the clause_crypto builtins after the production ones.
namespace clause::runtime::builtins::crypto {
namespace {
const std::array FAMILIES{algorithm_builtins(), hash_builtins(), mac_builtins(), random_builtins(), number_builtins()};

const RuntimeExtension EXTENSION{.name = "crypto", .families = FAMILIES};
} // namespace
} // namespace clause::runtime::builtins::crypto

const void *CLAUSE_crypto_extension_v1() noexcept { return &clause::runtime::builtins::crypto::EXTENSION; }
