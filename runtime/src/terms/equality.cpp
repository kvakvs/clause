#include "service_errors.hpp"
#include "structural_order.hpp"
#include "word_order.hpp"
#include <clause/abi/equality.hpp>
#include <clause/runtime/process_context.hpp>

namespace clause::runtime {
TermResult<bool> Term::exactly_equal(const Term &other) const {
    return detail::structural_order(*this, other, true).transform([](int order) { return order == 0; });
}
} // namespace clause::runtime

std::uint8_t CLAUSE_exact_v1(void *context, clause::abi::v1::TermWord left, clause::abi::v1::TermWord right) noexcept {
    using namespace clause;
    if (!context) {
        return static_cast<std::uint8_t>(abi::v1::Equality::failure);
    }
    auto &owner = *static_cast<runtime::ProcessContext *>(context);
    auto &state = owner.generated_calls();
    if (!state.active() || state.failure()) {
        return static_cast<std::uint8_t>(abi::v1::Equality::failure);
    }
    // Words the word-level order decides need no Term admission (reference comparisons, as receive matches replies).
    if (const auto equal = runtime::detail::word_exact(owner.heap(), left, right)) {
        return static_cast<std::uint8_t>(*equal ? abi::v1::Equality::equal : abi::v1::Equality::unequal);
    }
    const auto lhs = runtime::Term::from_word(left, owner);
    const auto rhs = runtime::Term::from_word(right, owner);
    if (!lhs || !rhs) {
        state.fail_service(abi::v1::Status::invalid_argument);
        return static_cast<std::uint8_t>(abi::v1::Equality::failure);
    }
    const auto equal = lhs->exactly_equal(*rhs);
    if (!equal) {
        state.fail_service(runtime::detail::term_status(equal.error()));
        return static_cast<std::uint8_t>(abi::v1::Equality::failure);
    }
    return static_cast<std::uint8_t>(*equal ? abi::v1::Equality::equal : abi::v1::Equality::unequal);
}
