#include <clause/abi/builtins.hpp>
#include <clause/runtime/code_server.hpp>
#include <clause/runtime/runtime.hpp>
#include <iostream>
#include <stdexcept>

namespace {
using namespace clause::runtime;
using clause::abi::v1::dispatch_builtin;
using clause::abi::v1::Status;

// Keep tests independent of assertion build flags.
void require(bool value, const char *message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

// Install test-only native registrations with explicit failure modes.
void install(CodeServer &server) {
    auto registry = std::make_unique<ModuleRegistry>();
    require(
        registry
            ->add("id", 1, [](ProcessContext &, std::span<const Term> args) { return CallResult<Term>(args.front()); })
            .has_value(),
        "identity registration failed");
    require(registry
                ->add("zero", 0,
                      [](ProcessContext &, std::span<const Term>) {
                          return CallResult<Term>(*Term::from_word(*encode_integer(42)));
                      })
                .has_value(),
            "constant registration failed");
    require(registry
                ->add("throw", 0,
                      [](ProcessContext &, std::span<const Term>) -> CallResult<Term> {
                          throw std::runtime_error("native body");
                      })
                .has_value(),
            "throw registration failed");
    require(
        registry
            ->add("oom", 0, [](ProcessContext &, std::span<const Term>) -> CallResult<Term> { throw std::bad_alloc(); })
            .has_value(),
        "allocation registration failed");
    require(
        registry->add("invalid", 0, [](ProcessContext &, std::span<const Term>) { return CallResult<Term>(Term{}); })
            .has_value(),
        "invalid-result registration failed");
    require(server.load({"test", CodeImage::linked(), std::move(registry)}).has_value(), "module load failed");
}

// Cross the actual word/pointer/status signature, keeping output unchanged on every failure.
void check_bridge(ProcessContext &context) {
    Word output = 123;
    const auto input = *encode_integer(-42);
    require(dispatch_builtin(&context, "test", 4, "id", 2, &input, 1, &output) == Status::ok, "identity bridge failed");
    require(output == input, "bridge converted generic value");
    require(dispatch_builtin(&context, "test", 4, "zero", 4, nullptr, 0, &output) == Status::ok, "zero arity failed");
    require(decode_integer(output) == 42, "zero arity result wrong");
    for (const auto &[name, status] : {std::pair{"throw", Status::internal_error},
                                       {"oom", Status::resource_limit},
                                       {"invalid", Status::invalid_argument}}) {
        output = 123;
        require(dispatch_builtin(&context, "test", 4, name, std::string_view(name).size(), nullptr, 0, &output) ==
                    status,
                "body error lost in bridge");
        require(output == 123, "failure overwrote output");
    }
    // An atom number no runtime issued here (false and true have the fixed numbers 0 and 1).
    for (Word invalid : {Word{0}, Word{1}, Word{2}, (Word{0xffffff} << 6) | 0xb, Word{0x7}, Word{0x3}}) {
        // Release builds trust pointer, atom, pid and port words (CHECKED_ADMISSION); only Word{0} stays invalid.
        if (invalid != 0 && !clause::runtime::CHECKED_ADMISSION) {
            continue;
        }
        require(dispatch_builtin(&context, "test", 4, "id", 2, &invalid, 1, &output) == Status::invalid_argument,
                "unsupported term crossed bridge");
        require(output == 123, "invalid argument overwrote output");
    }
}

// Reject null pointers and impossible arities before constructing borrowed views.
void check_invalid(ProcessContext &context) {
    Word output = 123;
    require(dispatch_builtin(nullptr, "test", 4, "id", 2, nullptr, 0, &output) == Status::invalid_argument,
            "null context accepted");
    require(dispatch_builtin(&context, nullptr, 0, "id", 2, nullptr, 0, &output) == Status::invalid_argument,
            "null name accepted");
    require(dispatch_builtin(&context, "test", 4, "id", 2, nullptr, 1, &output) == Status::invalid_argument,
            "null args accepted");
    require(dispatch_builtin(&context, "test", 4, "id", 2, &output, 256, &output) == Status::invalid_argument,
            "large arity accepted");
    require(dispatch_builtin(&context, "test", 4, "zero", 4, nullptr, 0, nullptr) == Status::invalid_argument,
            "null result accepted");
    require(output == 123, "invalid input changed result");
}
} // namespace

// The optional mode calls an unknown builtin for the subprocess check that ordinary failures stay silent.
int main(int argc, char **argv) {
    try {
        auto runtime = Runtime::start().value();
        auto *context = runtime->create_context().value();
        install(context->code_server());
        if (argc == 2) {
            const std::string_view function(argv[1]);
            Word output = 123;
            require(dispatch_builtin(context, "test", 4, function.data(), function.size(), nullptr, 0, &output) ==
                        Status::unknown_builtin,
                    "unknown builtin status lost");
            require(output == 123, "unknown builtin returned a fabricated word");
            return 0;
        }
        check_bridge(*context);
        check_invalid(*context);
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
