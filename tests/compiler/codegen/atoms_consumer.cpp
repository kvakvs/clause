#include <array>
#include <clause/abi/builtins.hpp>
#include <clause/abi/calls.hpp>
#include <clause/runtime/modules.hpp>
#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <terms.hpp>

extern clause::abi::v1::GeneratedRegistration register_answer asm("clausev1_616e73776572__0.register");
extern clause::abi::v1::GeneratedRegistration register_client asm("clausev1_636c69656e74__0.register");

namespace {
using namespace clause::runtime;
using clause::abi::v1::Status;

// Keep behavioral assertions enabled in every native optimization configuration.
void require(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

// Enter emitted code through its registered public callable, never a fabricated word result.
Term call(ProcessContext &context, std::string_view module, std::string_view name) {
    return context.code_server().resolve({module, name, 0}).value().call(context, {}).value();
}

// Compare only stable UTF-8 bytes with OTP, avoiding console encoding and raw atom identities.
void print(const Term &term) {
    static constexpr std::string_view digits = "0123456789abcdef";
    for (const unsigned char byte : term.atom_utf8().value()) {
        std::cout << digits[byte >> 4] << digits[byte & 15];
    }
    std::cout << '\n';
}

// Require an actual offending term before inspecting a structured error's spelling or lifetime.
const Term &payload(const CallFailure &failure) {
    if (!failure.value) {
        throw std::runtime_error("missing error payload");
    }
    return *failure.value;
}

// Return through the real native error service and retain the offending atom beyond invocation cleanup.
CallFailure error_payload(ProcessContext &context, const Term &atom) {
    GeneratedInvocation invocation(context.generated_calls());
    require(CLAUSE_raise_v2(&context, clause::abi::v1::ErrorReason::badmatch, atom.word()) == 0,
            "atom error payload rejected");
    const auto failure = context.generated_calls().failure();
    if (!failure) {
        throw std::runtime_error("missing generated failure");
    }
    require(payload(*failure).atom_utf8() == atom.atom_utf8(), "atom error spelling lost");
    return *failure;
}

// Registered generated modules share spellings; expressions perform no interning after registration.
Term execution(Runtime &runtime, ProcessContext &context) {
    const auto count = runtime.atom_storage()->size();
    const auto yes = call(context, "answer", "truth");
    const auto no = call(context, "answer", "falsity");
    require(yes.is_boolean() && yes.boolean_value() == true && no.boolean_value() == false, "boolean values wrong");
    require(call(context, "client", "same").word() == yes.word(), "equal local spellings have different identities");
    for (const auto name : {"truth", "falsity", "ok_value", "unicode", "empty", "nul", "projected"}) {
        const auto value = call(context, "client", name);
        print(value);
        require(value.is_atom() && value.kind() == TermKind::atom, "atom classification wrong");
    }
    const auto identity = runtime.code_server()->resolve({"answer", "id", 1}).value();
    const std::array arguments{yes};
    require(identity.call(context, arguments)->word() == yes.word(), "host argument atom lost");
    Word result = 0;
    const auto word = yes.word();
    require(clause::abi::v1::dispatch_builtin(&context, "answer", 6, "id", 2, &word, 1, &result) == Status::ok &&
                result == word,
            "builtin atom bridge failed");
    require(yes.copy_to(context.heap())->word() == word, "same-runtime atom copy failed");
    require(runtime.atom_storage()->size() == count, "expression evaluation interned new atoms");
    return yes;
}

// Native callbacks must validate owned atoms in successful results and structured error payloads alike.
void native_payloads(ProcessContext &local, ProcessContext &foreign, const Term &atom) {
    auto functions = std::make_unique<ModuleRegistry>();
    require(functions
                ->add("fail", 0,
                      [atom](ProcessContext &, std::span<const Term>) -> CallResult<Term> {
                          return std::unexpected(CallFailure{.code = CallError::erlang_exception,
                                                             .reason = clause::abi::v1::ErrorReason::badmatch,
                                                             .value = atom});
                      })
                .has_value(),
            "native error fixture failed");
    require(
        functions->add("value", 0, [atom](ProcessContext &, std::span<const Term>) -> CallResult<Term> { return atom; })
            .has_value(),
        "native result fixture failed");
    require(local.code_server().load({"native_atoms", CodeImage::linked(), std::move(functions)}).has_value(),
            "native payload module failed");
    const auto failure = local.code_server().resolve({"native_atoms", "fail", 0}).value();
    require(payload(failure.call(local, {}).error()).atom_utf8() == atom.atom_utf8(), "local error payload lost");
    const auto rejected = failure.call(foreign, {});
    require(rejected.error().term_error == TermError::wrong_owner && !rejected.error().value,
            "foreign native error payload admitted");
    const auto value = local.code_server().resolve({"native_atoms", "value", 0}).value();
    require(value.call(foreign, {}).error().term_error == TermError::wrong_owner, "foreign native result admitted");
}

// Reject foreign handles and raw words even when both runtimes intern the same spelling first.
void foreign(Runtime &runtime, ProcessContext &context, const Term &atom) {
    auto other = Runtime::start().value();
    require(register_answer(other.get()) == 0 && register_client(other.get()) == 0,
            "second runtime registration failed");
    auto &second = *other->create_context().value();
    native_payloads(context, second, atom);
    require(call(second, "answer", "ok_value").atom_utf8() == atom.atom_utf8(), "independent runtime spelling differs");
    require(Term::from_word(atom.word(), second).error() == TermError::wrong_owner, "foreign raw word aliased");
    require(atom.copy_to(second.heap()).error() == TermError::wrong_owner, "implicit cross-runtime copy admitted");
    const auto target = other->code_server()->resolve({"answer", "id", 1}).value();
    const std::array arguments{atom};
    require(!target.call(second, arguments), "foreign host atom admitted");
    const auto local = runtime.code_server()->resolve({"answer", "truth", 0}).value();
    require(local.call(second, {}).error().code == CallError::wrong_owner, "foreign module handle admitted");
    require(other->atom_storage()->intern(atom.atom_utf8().value())->atom_utf8() == atom.atom_utf8(),
            "explicit spelling remap failed");
    {
        GeneratedInvocation invocation(second.generated_calls());
        require(CLAUSE_raise_v2(&second, clause::abi::v1::ErrorReason::badmatch, atom.word()) != 0,
                "foreign error payload admitted");
    }
    require(call(context, "answer", "truth").boolean_value() == true, "foreign failure poisoned local calls");
}

// A missing remote module's bindings fail through the normal channel, then registration enables retry.
void registration_order() {
    auto runtime = Runtime::start().value();
    require(register_client(runtime.get()) == 0, "client registration failed");
    auto &context = *runtime->create_context().value();
    // ok, not true: false and true have fixed words, so a folded true needs no binding (OPT0.2A).
    const auto target = runtime->code_server()->resolve({"client", "ok_value", 0}).value();
    require(!target.call(context, {}), "unregistered remote literal fabricated an atom");
    require(register_answer(runtime.get()) == 0, "remote registration failed");
    require(target.call(context, {})->atom_utf8() == "ok", "post-registration retry failed");
}

// Validate spelling boundaries and factory lifetime using real owned host atoms.
void spelling(ProcessContext &context) {
    TermFactory factory(context);
    require(factory.boolean(true)->boolean_value() == true, "factory boolean failed");
    require(factory.atom("")->atom_utf8() == "", "empty atom rejected");
    require(factory.atom(std::string_view("a\0b", 3))->atom_utf8() == std::string("a\0b", 3), "NUL spelling truncated");
    std::string maximum;
    for (unsigned index = 0; index < 255; ++index) {
        maximum += "\xf0\x9f\x98\x80";
    }
    require(factory.atom(maximum).has_value(), "255 Unicode scalars rejected");
    require(!factory.atom(maximum + "a"), "256 Unicode scalars admitted");
    for (const auto *invalid : {"\xc0\x80", "\xed\xa0\x80", "\xf4\x90\x80\x80", "\x80", "\xe2\x82"}) {
        require(factory.atom(invalid).error() == TermError::invalid_encoding, "malformed UTF-8 admitted");
    }
}

// A simple native export lets malformed/capacity descriptors exercise the production registration path.
Word entry(clause::abi::v1::Context *, const Word *) { return encode_integer(1).value(); }

// Failed initialization publishes neither a module nor slots; retained valid prefixes count toward the cap.
void capacity() {
    using namespace clause::abi::v1;
    require(!Runtime::start({.max_atoms = 0}) && !Runtime::start({.max_atoms = AtomStorage::hard_limit + 1}),
            "invalid atom limits admitted");
    auto runtime = Runtime::start({.max_atoms = 3}).value();
    const ExportDescriptor item{"value", 5, 0, entry};
    std::array atoms{AtomDescriptor{"first", 5}, AtomDescriptor{"second", 6}};
    ModuleDescriptor descriptor{version, sizeof(Word) * 8, "bounded", 7, &item, 1, atoms.data(), atoms.size()};
    require(register_module(*runtime, descriptor).error() == CodeError::resource_limit, "capacity did not fail");
    require(!runtime->code_server()->find_module("bounded") && !runtime->code_server()->atom_word(&descriptor, 0),
            "failed registration published partial bindings");
    require(runtime->atom_storage()->size() == 3, "retained-prefix policy changed");
    descriptor.atom_count = 1;
    require(register_module(*runtime, descriptor).has_value(), "retry using retained atoms failed");
    require(runtime->atom_storage()->intern("first").has_value() && !runtime->atom_storage()->intern("new"),
            "deduplication at capacity failed");
    require(!register_module(*runtime, descriptor), "duplicate publication admitted");
    auto clean = Runtime::start().value();
    atoms[0] = {"\xc0\x80", 2};
    require(register_module(*clean, descriptor).error() == CodeError::invalid_module &&
                clean->atom_storage()->size() == 0,
            "malformed spelling retained a registration prefix");
    atoms[0] = {"first", 5};
    require(register_module(*clean, descriptor).has_value(), "valid descriptor retry failed");
    descriptor.name = "alias";
    descriptor.name_size = 5;
    require(register_module(*clean, descriptor).error() == CodeError::invalid_module,
            "reused descriptor address aliased published slots");
    require(!clean->code_server()->find_module("alias"), "duplicate descriptor published a module");
    descriptor.abi_version = 2;
    require(register_module(*clean, descriptor).error() == CodeError::abi_mismatch, "old descriptor admitted");
}
} // namespace

// Run the separate linked consumer and retain host/error atom pins past full runtime destruction.
int main() {
    try {
        registration_order();
        capacity();
        auto runtime = Runtime::start().value();
        require(register_answer(runtime.get()) == 0 && register_client(runtime.get()) == 0, "registration failed");
        auto &context = *runtime->create_context().value();
        TermFactory expired(context);
        const auto atom = execution(*runtime, context);
        const auto failure = error_payload(context, atom);
        // false and true have the same words in every runtime (OPT0.2A); foreign words are checked with ok.
        foreign(*runtime, context, call(context, "client", "ok_value"));
        spelling(context);
        runtime.reset();
        require(atom.atom_utf8() == "true" && payload(failure).boolean_value() == true,
                "atom pin expired with runtime");
        require(expired.atom("late").error() == TermError::expired_context, "factory used destroyed storage");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    } catch (...) {
        std::fputs("unexpected atom test exception\n", stderr);
        return 1;
    }
}
