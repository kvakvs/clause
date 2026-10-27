#include "terms/funs.hpp"
#include "terms.hpp"
#include "terms/structural_order.hpp"
#include <array>
#include <clause/abi/equality.hpp>
#include <clause/abi/funs.hpp>
#include <clause/abi/modules.hpp>
#include <clause/runtime/modules.hpp>
#include <clause/runtime/output.hpp>
#include <clause/runtime/runtime.hpp>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// Fun cells, CLAUSE_make_fun_v1, CLAUSE_apply_v1 and the dynamic call services over hand-written
// descriptors (docs/funs.md).
namespace {
using namespace clause;
using namespace clause::runtime;
using abi::v1::FrameDescriptor;
using abi::v1::FunDescriptor;

// Keep assertions active in optimized builds.
void require(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

// Atom slots: 0 module, 1 the anonymous fun, 2 a local function, 3 and 4 an external module and function.
constexpr std::array<abi::v1::AtomDescriptor, 5> ATOMS{
    {{"fm", 2}, {"-f/0-fun-0-", 11}, {"g", 1}, {"other", 5}, {"h", 1}}};

extern const abi::v1::ModuleDescriptor fm;
// Code is never entered here: frames only name the functions and their arities (one argument, two captured values).
const FrameDescriptor LAMBDA{&fm, 0, 1, 3, nullptr, 3, 3};
const FrameDescriptor LOCAL{&fm, 0, 2, 1, nullptr, 1, 1};
const std::array<FunDescriptor, 3> FUNS{
    {{&fm, 0, 1, 1, 0, 0, &LAMBDA}, {&fm, 0, 2, 1, 1, 0, &LOCAL}, {&fm, 3, 4, 2, 0, 1, nullptr}}};

// The host entry of the export g/1; never called here.
abi::v1::TermWord entry(abi::v1::Context *, const abi::v1::TermWord *) { return 0; }

const std::array EXPORTS{abi::v1::ExportDescriptor{"g", 1, 1, &entry, &LOCAL}};
const abi::v1::ModuleDescriptor fm{abi::v1::version, sizeof(Word) * 8, "fm",    2, EXPORTS.data(), EXPORTS.size(),
                                   ATOMS.data(),     ATOMS.size(),     nullptr, 0, FUNS.data(),    FUNS.size()};

// Build a fun of a descriptor inside a generated invocation; the status of a refused build.
TermResult<Term> make(ProcessContext &context, const FunDescriptor &descriptor, std::span<const Term> captures) {
    std::vector<Word> words;
    for (const auto &value : captures) {
        words.push_back(value.word());
    }
    Word output = 0;
    GeneratedInvocation scope(context.generated_calls());
    const auto status =
        static_cast<abi::v1::Status>(CLAUSE_make_fun_v1(&context, &descriptor, words.data(), words.size(), &output));
    if (status != abi::v1::Status::ok) {
        return std::unexpected(status == abi::v1::Status::wrong_owner ? TermError::wrong_owner
                                                                      : TermError::invalid_argument);
    }
    return Term::from_word(output, context);
}

// The result of preparing a call: the frame to enter, the registers after it and the error it raised, as text.
struct Applied {
    const void *frame;
    std::vector<Word> registers;
    std::string error;
};

// Prepare calling `fun` with `arguments` in a register array.
Applied apply(ProcessContext &context, const Term &fun, std::span<const Term> arguments) {
    std::vector<Word> registers(abi::v1::register_count, 0);
    for (std::size_t i = 0; i < arguments.size(); ++i) {
        registers[i] = arguments[i].word();
    }
    GeneratedInvocation scope(context.generated_calls());
    const auto *frame = CLAUSE_apply_v1(&context, fun.word(), arguments.size(), registers.data());
    const auto &failure = context.generated_calls().failure();
    std::string error;
    if (failure && failure->reason) {
        const auto name = std::to_string(static_cast<int>(*failure->reason));
        error = failure->value ? name + ":" + format_term(*failure->value, TermStyle::write).value() : name;
    }
    return {frame, std::move(registers), std::move(error)};
}

// Render a value as erlang:display text.
std::string text(const Term &value) { return format_term(value, TermStyle::display).value(); }

// Funs keep their definition and captured values, print like OTP's and pass is_function.
void construction(ProcessContext &context) {
    TermFactory factory(context);
    const auto one = factory.integer(1).value();
    const auto pair = factory.tuple(std::array{one, factory.atom("x").value()}).value();
    const auto closure = make(context, FUNS[0], std::array{pair, one}).value();
    const auto local = make(context, FUNS[1], {}).value();
    const auto external = make(context, FUNS[2], {}).value();
    require(closure.is_function() && closure.is_function(1) && !closure.is_function(3) && external.is_function(2) &&
                !pair.is_function(),
            "is_function differs");
    require(text(closure) == "#Fun<fm.0.0>" && text(local) == "#Fun<fm.1.0>" && text(external) == "fun other:h/2" &&
                format_term(external, TermStyle::write).value() == "fun other:h/2",
            "fun text differs");
    require(make(context, FUNS[0], std::array{one}).error() == TermError::invalid_argument,
            "captured value count mismatch accepted");
    const FunDescriptor forged = FUNS[1];
    require(make(context, forged, {}).error() == TermError::wrong_owner, "unregistered descriptor accepted");
}

// Funs order after atoms and before tuples; local before external; same definitions by captured values.
void order(ProcessContext &context) {
    TermFactory factory(context);
    const auto one = factory.integer(1).value();
    const auto real = factory.floating(1.0).value();
    const auto two = factory.integer(2).value();
    const auto compare = [](const Term &left, const Term &right, bool exact) {
        return detail::structural_order(left, right, exact).value();
    };
    const auto a = make(context, FUNS[0], std::array{one, two}).value();
    const auto b = make(context, FUNS[0], std::array{one, two}).value();
    const auto c = make(context, FUNS[0], std::array{two, one}).value();
    const auto d = make(context, FUNS[0], std::array{real, two}).value();
    const auto local = make(context, FUNS[1], {}).value();
    const auto external = make(context, FUNS[2], {}).value();
    require(compare(a, b, true) == 0 && compare(a, c, true) < 0 && compare(a, d, true) != 0 &&
                compare(a, d, false) == 0,
            "captured value order differs");
    require(compare(a, local, true) < 0 && compare(local, external, true) < 0, "fun order differs");
    require(compare(factory.atom("zzz").value(), a, true) < 0 && compare(external, factory.tuple({}).value(), true) < 0,
            "fun category order differs");
}

// A call checks the value and its arity, then appends the captured values after the arguments.
void calls(ProcessContext &context) {
    TermFactory factory(context);
    const auto one = factory.integer(1).value();
    const auto x = factory.atom("x").value();
    const auto closure = make(context, FUNS[0], std::array{one, x}).value();
    const auto ready = apply(context, closure, std::array{x});
    require(ready.frame == &LAMBDA && ready.error.empty() && ready.registers[0] == x.word() &&
                ready.registers[1] == one.word() && ready.registers[2] == x.word(),
            "captured values were not appended");
    const auto arity = apply(context, closure, std::array{x, one});
    require(!arity.frame && arity.error == "23:{#Fun<fm.0.0>,[x,1]}", "badarity differs");
    const auto value = apply(context, one, {});
    require(!value.frame && value.error == "22:1", "badfun differs");
    const auto external = make(context, FUNS[2], {}).value();
    const auto missing = apply(context, external, std::array{x, x});
    require(!missing.frame && missing.error == "24", "undef differs");
}

// The pending error of the current invocation as text: the reason ID, then its payload.
std::string pending(ProcessContext &context) {
    const auto &failure = context.generated_calls().failure();
    if (!failure || !failure->reason) {
        return failure ? "failure" : "";
    }
    const auto name = std::to_string(static_cast<int>(*failure->reason));
    return failure->value ? name + ":" + format_term(*failure->value, TermStyle::write).value() : name;
}

// Look up Module:Function/Arity as M:F(Args) does; the frame and the error raised.
std::pair<const void *, std::string> lookup(ProcessContext &context, const Term &module, const Term &function,
                                            std::size_t arity) {
    GeneratedInvocation scope(context.generated_calls());
    std::array<Word, abi::v1::register_count> registers{};
    const auto *frame = CLAUSE_call_v1(&context, module.word(), function.word(), arity, registers.data());
    return {frame, pending(context)};
}

// Build fun M:F/A from runtime operands; the fun, or the error raised.
std::pair<std::optional<Term>, std::string> external(ProcessContext &context, const Term &module, const Term &function,
                                                     const Term &arity) {
    GeneratedInvocation scope(context.generated_calls());
    Word output = 0;
    CLAUSE_make_external_fun_v1(&context, module.word(), function.word(), arity.word(), &output);
    if (const auto error = pending(context); !error.empty()) {
        return {std::nullopt, error};
    }
    return {Term::from_word(output, context).value(), ""};
}

// Dynamic calls find exported frames by name; external funs built at run time are interned per M:F/A.
void dynamic(ProcessContext &context) {
    TermFactory factory(context);
    const auto module = factory.atom("fm").value();
    const auto g = factory.atom("g").value();
    const auto one = factory.integer(1).value();
    require(lookup(context, module, g, 1) == std::pair<const void *, std::string>{&LOCAL, ""}, "export not found");
    require(lookup(context, module, g, 2).second == "24" && lookup(context, one, g, 1).second == "3",
            "lookup errors differ");
    const auto first = external(context, module, g, one).first.value();
    const auto second = external(context, module, g, one).first.value();
    require(detail::fun_view(first)->definition == detail::fun_view(second)->definition && text(first) == "fun fm:g/1",
            "external fun not interned");
    require(apply(context, first, std::array{one}).frame == &LOCAL, "external fun does not enter its export");
    require(external(context, module, g, factory.integer(256).value()).second == "3" &&
                external(context, module, one, one).second == "3",
            "invalid external fun accepted");
    GeneratedInvocation scope(context.generated_calls());
    require(!CLAUSE_apply_list_v1(&context, first.word(), abi::v1::empty_list, nullptr) &&
                pending(context) == "failure",
            "missing registers accepted");
}

// Captured values survive collections and copies between heaps.
void memory(Runtime &runtime) {
    auto &source = *runtime.create_context().value();
    auto &destination = *runtime.create_context().value();
    TermFactory factory(source);
    const auto inner = factory.tuple(std::array{factory.integer(7).value()}).value();
    const auto list = factory.list(std::array{inner, inner}).value();
    const auto closure = make(source, FUNS[0], std::array{list, inner}).value();
    require(source.heap().verify().has_value(), "fun heap does not verify");
    const auto copy = closure.copy_to(destination.heap()).value();
    require(copy.exactly_equal(closure).value() && destination.heap().verify().has_value(), "copied fun differs");
    std::array roots{closure.word()};
    require(source.heap().collect(roots).has_value() && source.heap().verify().has_value(), "collection failed");
    const auto moved = Term::from_word(roots[0], source).value();
    require(moved.exactly_equal(copy).value(), "fun lost its captured values in a collection");
    require(runtime.destroy_context(&source) == abi::v1::Status::ok && copy.is_function(1),
            "copy depended on its source");
}

// Registration binds only funs that name their own module, and local funs need code.
void registration(Runtime &runtime) {
    auto bad = fm;
    bad.name = "bad";
    bad.name_size = 3;
    const FunDescriptor stray{&fm, 0, 2, 1, 0, 0, &LOCAL};
    bad.funs = &stray;
    bad.fun_count = 1;
    require(register_module(runtime, bad).error() == CodeError::invalid_module, "foreign fun descriptor accepted");
}
} // namespace

int main() {
    try {
        auto runtime = Runtime::start().value();
        require(register_module(*runtime, fm).has_value(), "fun module rejected");
        registration(*runtime);
        auto &context = *runtime->create_context().value();
        construction(context);
        order(context);
        calls(context);
        dynamic(context);
        memory(*runtime);
        std::cout << "funs passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
