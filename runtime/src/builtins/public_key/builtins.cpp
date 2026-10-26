#include "../typed.hpp"
#include "public_key.hpp"

// Added for public_key (plan 21 step LIB80B): the builtins of clause_public_key (docs/builtins.md#public-key).
namespace clause::runtime::builtins::public_key {
namespace {
constexpr std::array PUBLIC_KEY_BUILTINS{
    typed_entry<translate>("clause_public_key", "translate"),
    typed_entry<oid_name>("clause_public_key", "oid_name"),
    typed_entry<name_oid>("clause_public_key", "name_oid"),
    typed_entry<asn1_module>("clause_public_key", "asn1_module"),
    typed_entry<os_cacerts>("clause_public_key", "os_cacerts"),
    typed_entry<pem_decode>("clause_public_key", "pem_decode"),
    typed_entry<pem_entry>("clause_public_key", "pem_entry"),
    typed_entry<pbe_cipher>("clause_public_key", "pbe_cipher"),
    typed_entry<pbes2_cipher>("clause_public_key", "pbes2_cipher"),
    typed_entry<pbes1>("clause_public_key", "pbes1"),
    typed_entry<pbes1_oid>("clause_public_key", "pbes1_oid"),
    typed_entry<prf_digest>("clause_public_key", "prf_digest"),
    typed_entry<policy_add>("clause_public_key", "policy_add"),
    typed_entry<policy_map>("clause_public_key", "policy_map"),
    typed_entry<policy_intersection>("clause_public_key", "policy_intersection"),
    typed_entry<policy_prune>("clause_public_key", "policy_prune"),
    typed_entry<policy_constrained>("clause_public_key", "policy_constrained"),
    typed_entry<policy_result>("clause_public_key", "policy_result"),
    typed_entry<pbkdf1_rounds>("clause_public_key", "pbkdf1_rounds"),
};
} // namespace
} // namespace clause::runtime::builtins::public_key

namespace clause::runtime {
std::span<const BuiltinEntry> public_key_builtins() noexcept { return builtins::public_key::PUBLIC_KEY_BUILTINS; }
} // namespace clause::runtime
