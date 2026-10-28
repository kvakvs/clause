#include "ssl.hpp"

// Added for ssl (plan 21 step LIB81A): the ssl builtins (docs/builtins.md#ssl).
namespace clause::runtime::builtins::ssl {
namespace {
constexpr std::array SSL_BUILTINS{
    typed_entry<cipher_suites>("ssl", "cipher_suites"),
    typed_entry<cipher_suite_names>("ssl", "cipher_suites"),
    typed_entry<suite_to_str>("ssl", "suite_to_str"),
    typed_entry<suite_to_openssl_str>("ssl", "suite_to_openssl_str"),
    typed_entry<str_to_suite>("ssl", "str_to_suite"),
    typed_entry<suite_code>("clause_ssl_algorithms", "suite_code"),
    typed_entry<code_suite>("clause_ssl_algorithms", "code_suite"),
};
} // namespace
} // namespace clause::runtime::builtins::ssl

namespace clause::runtime {
std::span<const BuiltinEntry> ssl_builtins() noexcept { return builtins::ssl::SSL_BUILTINS; }
} // namespace clause::runtime
