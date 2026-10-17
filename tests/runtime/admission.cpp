#include "terms.hpp"
#include <array>
#include <clause/runtime/runtime.hpp>
#include <iostream>
#include <stdexcept>

// Admission checks ownership: a heap word is accepted only inside this process's used heap and only when
// the object it names matches its tag. Process pointers always name object starts, so no interior case.
namespace {
using namespace clause::runtime;
using clause::abi::v1::Status;

// Keep every check active in optimized builds.
void require(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

// The word must not be admitted by context.
void rejects(ProcessContext &context, Word word, const char *message) {
    require(!clause::runtime::CHECKED_ADMISSION ||
                Term::from_word(word, context) == std::unexpected(TermError::wrong_owner),
            message);
}

// Every object start of every layout is admitted, including later cells of a list.
void starts(ProcessContext &context) {
    TermFactory factory(context);
    const auto one = factory.integer(1).value();
    const auto list = factory.list(std::array{one, one, one}).value();
    const auto values = {factory.tuple(std::array{one, one}).value(),
                         factory.map(std::array{std::pair{one, one}}).value(),
                         factory.integer_decimal(std::string(60, '7')).value(),
                         factory.floating(2.5).value(),
                         factory.binary(std::vector(40, std::byte{9})).value(),
                         factory.binary(std::vector(200, std::byte{9})).value(),
                         list};
    for (const auto &value : values) {
        require(Term::from_word(value.word(), context).has_value(), "object start rejected");
    }
    const auto second = Term::from_word(list.word() + 2 * sizeof(Word), context);
    require(second && second->list_length() == 2, "later cons cell rejected");
}

// Words past the used area, of a rolled-back reservation, or of another process are not this heap's.
void ownership(ProcessContext &context, ProcessContext &other) {
    Word abandoned = 0;
    {
        auto reservation = context.heap().reserve(2).value();
        abandoned = reinterpret_cast<Word>(reservation.bytes().data()) | 2U;
    }
    rejects(context, abandoned, "rolled-back word admitted");
    const auto tuple = TermFactory(context).tuple(std::array{Term::from_word(0x3b).value()}).value();
    rejects(context, tuple.word() + 64 * sizeof(Word), "word past used area admitted");
    const auto foreign = TermFactory(other).tuple(std::array{Term::from_word(0x3b).value()}).value();
    rejects(context, foreign.word(), "foreign word admitted");
}
} // namespace

int main() {
    try {
        auto runtime = Runtime::start().value();
        auto &context = *runtime->create_context().value();
        auto &other = *runtime->create_context().value();
        starts(context);
        ownership(context, other);
        require(context.heap().verify().has_value() && other.heap().verify().has_value(), "heaps unparseable");
        require(runtime->destroy_context(&other) == Status::ok, "teardown failed");
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
