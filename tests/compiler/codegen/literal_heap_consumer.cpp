#include "link_name.hpp"
#include <array>
#include <clause/runtime/modules.hpp>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

// Literal heap: one module's literals read by the runtime that claims its caches (from the literal cache), by one that
// does not (through CLAUSE_literal_v1) and by one that claims them after the first runtime is gone.
extern clause::abi::v1::GeneratedRegistration
    register_literals CLAUSE_LINK_NAME("clausev1_6c69746572616c73__0.register");

namespace {
using namespace clause::runtime;

constexpr std::string_view VALUE =
    "{alpha,[beta,{gamma,1},[116,101,120,116]],-5,true,{},[delta|epsilon],1.5,123456789012345678901234567890,"
    "-98765432109876543210,#{a=>1,b=>[x]},<<5>>,<<79>>}";
constexpr std::string_view EXPORTS = "[{value,0},{prefixed,1},{module_info,0},{module_info,1}]";
constexpr std::string_view PREFIXED = "[beta,gamma,omega]";

void require(const bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

// Require text equal to `expected`, showing both otherwise.
void expect(const std::string &actual, const std::string_view expected, const char *message) {
    if (actual != expected) {
        throw std::runtime_error(std::string(message) + ": " + actual + " instead of " + std::string(expected));
    }
}

std::string text(const Term &term);

// The elements of a list from its first cons cell, then `|` and an improper tail.
std::string list_text(Term term) {
    std::string result = "[";
    for (bool first = true; term.is_cons(); first = false, term = term.tail().value()) {
        result += (first ? "" : ",") + text(term.head().value());
    }
    return result + (term.is_nil() ? "" : "|" + text(term)) + "]";
}

std::string tuple_text(const Term &term) {
    std::string result = "{";
    for (std::size_t index = 0; index < term.tuple_size().value(); ++index) {
        result += (index == 0 ? "" : ",") + text(term.tuple_element(index).value());
    }
    return result + "}";
}

std::string map_text(const Term &term) {
    std::string result = "#{";
    bool first = true;
    for (const auto &[key, value] : term.map_entries().value()) {
        result += (first ? "" : ",") + text(key) + "=>" + text(value);
        first = false;
    }
    return result + "}";
}

// Erlang-like text of the fixture's terms; a binary shows its byte count.
std::string text(const Term &term) {
    if (term.is_atom()) {
        return term.atom_utf8().value();
    }
    if (term.is_integer()) {
        return term.integer_decimal().value();
    }
    if (term.is_float()) {
        return std::to_string(term.float_value().value()).substr(0, 3);
    }
    if (term.is_binary()) {
        return "<<" + std::to_string(term.binary_bytes().value().size()) + ">>";
    }
    if (term.is_tuple()) {
        return tuple_text(term);
    }
    if (term.is_map()) {
        return map_text(term);
    }
    return term.is_nil() ? "[]" : list_text(term);
}

Term call(ProcessContext &context, std::string_view name, std::span<const Term> arguments) {
    return context.code_server().resolve({"literals", name, arguments.size()}).value().call(context, arguments).value();
}

// Check one runtime's literals; its value literal's word, which every call returns unchanged.
Word check(Runtime &runtime) {
    require(register_literals(&runtime) == 0, "registration failed");
    auto &context = *runtime.create_context().value();
    const auto value = call(context, "value", {});
    expect(text(value), VALUE, "literal value differs");
    require(call(context, "value", {}).word() == value.word(), "literal rebuilt");
    TermFactory factory(context);
    const std::array key{factory.atom("exports").value()};
    expect(text(call(context, "module_info", key)), EXPORTS, "module_info(exports) differs");
    const std::array tail{factory.list(std::array{factory.atom("omega").value()}).value()};
    expect(text(call(context, "prefixed", tail)), PREFIXED, "literal prefix differs");
    return value.word();
}
} // namespace

int main() {
    try {
        auto first = Runtime::start().value();
        const auto claimed = check(*first);
        {
            auto second = Runtime::start().value();
            require(check(*second) != claimed, "runtimes share a literal heap");
        }
        first.reset();
        auto third = Runtime::start().value();
        check(*third);
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "literal_heap: ok\n";
    return 0;
}
