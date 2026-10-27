#include <array>
#include <clause/runtime/modules.hpp>
#include <cstdio>
#include <stdexcept>
#include <utility>

// Match ABI v1's reversible registration symbols; these are project C++ machine interfaces. Mach-O prefixes
// C-level symbol names with '_'.
#ifdef __APPLE__
#define GENERATED_SYMBOL(name) asm("_" name)
#else
#define GENERATED_SYMBOL(name) asm(name)
#endif
extern clause::abi::v1::GeneratedRegistration register_answer GENERATED_SYMBOL("clausev1_616e73776572__0.register");
extern clause::abi::v1::GeneratedRegistration register_client GENERATED_SYMBOL("clausev1_636c69656e74__0.register");

// Convert failed host operations into one contained example error before accessing their values.
template <class Result> auto checked(Result result) {
    if (!result) {
        throw std::runtime_error("runtime operation failed");
    }
    return std::move(*result);
}

// Execute each supported pattern family through the separately compiled remote demo.
void demonstrate(clause::runtime::ProcessContext &context) {
    const auto entry = checked(context.code_server().resolve({"client", "demo", 0}));
    const auto labels = checked(checked(entry.call(context, {})).tuple_elements());
    for (const auto &label : labels) {
        const auto text = checked(label.atom_spelling());
        std::printf("%.*s\n", static_cast<int>(text.size()), text.data());
    }
}

// Initialize one real runtime, register separate objects, decode results and shut down explicitly.
int execute() {
    using namespace clause::runtime;
    auto runtime = checked(Runtime::start());
    if (register_answer(runtime.get()) != 0 || register_client(runtime.get()) != 0) {
        return 1;
    }
    auto *context = checked(runtime->create_context());
    const auto entry = checked(runtime->code_server()->resolve({"client", "value", 0}));
    const auto value = checked(entry.call(*context, {}));
    const auto identity = checked(runtime->code_server()->resolve({"answer", "identity", 1}));
    const std::array arguments{checked(Term::from_word(checked(encode_integer(-7))))};
    const auto copied = checked(identity.call(*context, arguments));
    std::printf("%lld\n%lld\n", static_cast<long long>(checked(value.integer_value())),
                static_cast<long long>(checked(copied.integer_value())));
    demonstrate(*context);
    if (runtime->destroy_context(context) != clause::abi::v1::Status::ok ||
        runtime->shutdown() != clause::abi::v1::Status::ok) {
        return 2;
    }
    return 0;
}

// Keep startup/lookup failures visible to a native caller without providing a production Erlang launcher.
int main() {
    try {
        return execute();
    } catch (const std::exception &error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    } catch (...) {
        return 1;
    }
}
