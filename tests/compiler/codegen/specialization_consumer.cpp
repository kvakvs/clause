#include "link_name.hpp"
#include <array>
#include <clause/runtime/modules.hpp>

extern clause::abi::v1::GeneratedRegistration register_guards CLAUSE_LINK_NAME("clausev1_677561726473__0.register");
extern clause::abi::v1::GeneratedFunction
    reference_select CLAUSE_LINK_NAME("clausev1_677561726473_73656c656374_2.reference");

// Compare native guarded dispatch against the untouched generic body over hits, misses and word boundaries.
int main() {
    using namespace clause::runtime;
    auto runtime = Runtime::start().value();
    if (register_guards(runtime.get()) != 0) {
        return 1;
    }
    auto *context = runtime->create_context().value();
    const auto selected = runtime->code_server()->resolve({"guards", "select", 2}).value();
    const auto budgeted = runtime->code_server()->resolve({"guards", "budgeted", 2}).value();
    const auto minimum = -(std::int64_t{1} << (sizeof(Word) * 8 - 5));
    const std::array words{*encode_integer(minimum),      *encode_integer(-1), *encode_integer(0),
                           *encode_integer(-minimum - 1), Word{0x2b},          Word{0x3b}};
    for (const auto first : words) {
        for (const auto second : words) {
            const std::array raw{first, second};
            const std::array arguments{Term::from_word(first).value(), Term::from_word(second).value()};
            const auto result = selected.call(*context, arguments);
            if (!result || result->word() != reference_select(context, raw.data())) {
                return 2;
            }
            const auto expected = decode_integer(first) ? first : second;
            if (result->word() != expected) {
                return 3;
            }
            const auto fallback = budgeted.call(*context, arguments);
            if (!fallback || fallback->word() != expected) {
                return 4;
            }
        }
    }
}
