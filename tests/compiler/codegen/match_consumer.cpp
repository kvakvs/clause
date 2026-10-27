#include "link_name.hpp"
#include "match_wire.hpp"
#include <clause/abi/equality.hpp>
#include <clause/runtime/atoms.hpp>
#include <clause/runtime/modules.hpp>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>

extern clause::abi::v1::GeneratedRegistration register_answer CLAUSE_LINK_NAME("clausev1_616e73776572__0.register");
extern clause::abi::v1::GeneratedRegistration register_client CLAUSE_LINK_NAME("clausev1_636c69656e74__0.register");

namespace {
using namespace clause::runtime;

// Retain readable test failures in optimized native builds.
void require(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

// Decode owned terms through the bounded value transport and reject trailing input.
Term term(ProcessContext &context, const std::string &token) {
    std::string_view input(token);
    const auto result = wire::read(context, input);
    require(input.empty(), "trailing argument input");
    return result;
}

// End each successful or retained-error result with exactly one transport newline.
void print_value(const Term &value) {
    wire::write(value);
    std::cout << '\n';
}

// Compare OTP-visible values and structured error reasons without depending on stack formatting.
void print(const CallResult<Term> &result) {
    if (result) {
        print_value(*result);
        return;
    }
    require(result.error().code == CallError::erlang_exception, "unexpected infrastructure failure");
    const auto reason = result.error().reason;
    if (reason == clause::abi::v1::ErrorReason::badarg_value || reason == clause::abi::v1::ErrorReason::badmatch ||
        reason == clause::abi::v1::ErrorReason::badmap || reason == clause::abi::v1::ErrorReason::badkey ||
        reason == clause::abi::v1::ErrorReason::badrecord) {
        require(result.error().value.has_value(), "missing error payload");
        const std::map<clause::abi::v1::ErrorReason, std::string_view> names{
            {clause::abi::v1::ErrorReason::badmatch, "badmatch"},
            {clause::abi::v1::ErrorReason::badarg_value, "badarg_value"},
            {clause::abi::v1::ErrorReason::badmap, "badmap"},
            {clause::abi::v1::ErrorReason::badkey, "badkey"},
            {clause::abi::v1::ErrorReason::badrecord, "badrecord"}};
        std::cout << "error:" << names.at(*reason) << ':';
        print_value(*result.error().value);
        return;
    }
    require(reason == clause::abi::v1::ErrorReason::function_clause || reason == clause::abi::v1::ErrorReason::badarg ||
                reason == clause::abi::v1::ErrorReason::badarith,
            "unexpected Erlang reason");
    if (reason == clause::abi::v1::ErrorReason::badarith) {
        std::cout << "error:badarith\n";
        return;
    }
    std::cout << (reason == clause::abi::v1::ErrorReason::badarg ? "error:badarg\n" : "error:function_clause\n");
}

// Runtime-only invalid word/foreign ownership checks cannot be expressed as legal Erlang source.
void equality_failures(ProcessContext &context) {
    auto other = Runtime::start().value();
    auto &foreign = *other->create_context().value();
    const auto atom = foreign.atom_storage().intern("owned").value();
    for (const auto invalid : {Word{0}, Word{0x6b}, atom.word()}) {
        // Another runtime's atom is trusted in Release builds (CHECKED_ADMISSION).
        if (invalid == atom.word() && !clause::runtime::CHECKED_ADMISSION) {
            continue;
        }
        GeneratedInvocation scope(context.generated_calls());
        require(CLAUSE_exact_v1(&context, invalid, invalid) == 2, "invalid equality fabricated success");
        require(context.generated_calls().failure().has_value(), "invalid equality lost failure");
    }
    GeneratedInvocation scope(context.generated_calls());
    require(CLAUSE_exact_v1(&context, clause::abi::v1::empty_list, clause::abi::v1::empty_tuple) == 0,
            "equality did not recover");
}

// Host Terms stay valid only until a collection: merge fragments here, with the retained words as roots, so
// the safepoints of the next call find nothing to collect in the large heap.
void collect_between_calls(ProcessContext &context, std::vector<std::pair<Word, std::string>> &retained) {
    if (!context.heap().wants_collection()) {
        return;
    }
    std::vector<Word> roots;
    for (const auto &[word, text] : retained) {
        roots.push_back(word);
    }
    require(context.heap().collect(roots).has_value(), "host collection between calls failed");
    for (std::size_t i = 0; i < roots.size(); ++i) {
        retained[i].first = roots[i];
    }
}

// Invoke each complete source helper through its registered generated ABI entry.
void calls(ProcessContext &context) {
    std::vector<std::pair<Word, std::string>> retained;
    std::string module;
    std::string function;
    std::size_t arity = 0;
    while (std::cin >> module >> function >> arity) {
        require(arity <= 255, "call arity limit");
        collect_between_calls(context, retained);
        std::vector<Term> arguments;
        for (std::size_t i = 0; i < arity; ++i) {
            std::string token;
            require(static_cast<bool>(std::cin >> token), "missing argument");
            arguments.push_back(term(context, token));
        }
        const auto result = context.code_server().resolve({module, function, arity}).value().call(context, arguments);
        print(result);
        if (result && (function == "first" || function == "id")) {
            require(result->word() == arguments.front().word(), "projection reconstructed a matched term");
        }
        const auto value = result ? std::optional<Term>{*result} : result.error().value;
        if (value && retained.size() < 128 &&
            (value->is_cons() || value->kind() == TermKind::tuple || value->is_float() || value->is_map())) {
            std::ostringstream text;
            wire::write(*value, text);
            retained.emplace_back(value->word(), text.str());
        }
        require(!context.generated_calls().failure(), "stale failure after invocation");
        require(context.stack().depth() == 0 && context.stack().words() == 0, "generated call leaked root frames");
    }
    require(std::cin.eof(), "invalid calls stream");
    for (const auto &[word, expected] : retained) {
        std::ostringstream text;
        wire::write(Term::from_word(word, context).value(), text);
        require(text.str() == expected, "later calls changed retained result/error graph");
    }
}
} // namespace

// The separate consumer has no LLVM dependency; it also proves recovery and explicit teardown.
int main() {
    try {
        auto runtime = Runtime::start().value();
        require(register_answer(runtime.get()) == 0 && register_client(runtime.get()) == 0, "registration failed");
        auto *context = runtime->create_context(HeapOptions{std::size_t{1} << 20}).value();
        equality_failures(*context);
        calls(*context);
        require(runtime->destroy_context(context) == clause::abi::v1::Status::ok, "context teardown failed");
        require(runtime->shutdown() == clause::abi::v1::Status::ok, "runtime teardown failed");
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
