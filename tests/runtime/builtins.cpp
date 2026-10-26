#include <array>
#include <clause/abi/startup.hpp>
#include <clause/runtime/code_server.hpp>
#include <clause/runtime/runtime.hpp>
#include <iostream>
#include <stdexcept>

namespace {
using namespace clause::runtime;

// Keep validation active in optimized standalone runtime builds.
void require(bool value, const char *message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

// Supply a real, validated immediate without invoking unimplemented heap factories.
Term integer(std::int64_t value) { return *Term::from_word(*encode_integer(value)); }

// Malformed host words and reserved identities cannot arise in the linked consumer's supported values.
void check_terms() {
    require(Term::from_word(0).error() == TermError::invalid_encoding, "invalid host word accepted");
    require(Term::from_word(0xb).error() == TermError::not_implemented, "unbound atom admitted");
    require(Term::from_word(1).error() == TermError::wrong_type, "heap word admitted");
}

// Observe capture destruction while the executable image it depends on is still alive.
struct Capture final {
    // Borrow test-owned counters that outlive every module pin.
    bool &image_alive;
    bool &destroyed_safely;

    // Verify target/capture teardown precedes image teardown.
    ~Capture() { destroyed_safely = image_alive; }
};

struct Image final : CodeImage {
    // Borrow a test flag to expose the image's exact lifetime.
    bool &alive;

    // Mark the image as mapped until its last module owner releases it.
    explicit Image(bool &value) : alive(value) { alive = true; }

    // Simulate releasing executable storage.
    ~Image() override { alive = false; }
};

// Retain a callable past runtime destruction and prove its capture dies before executable storage.
void check_pinning() {
    bool alive = false;
    bool safe = false;
    std::optional<ResolvedFunction> pinned;
    {
        auto runtime = Runtime::start().value();
        auto registry = std::make_unique<ModuleRegistry>();
        auto capture = std::make_shared<Capture>(alive, safe);
        require(registry
                    ->add("state", 0,
                          [capture, count = 0](ProcessContext &, std::span<const Term>) mutable {
                              return CallResult<Term>(integer(++count));
                          })
                    .has_value(),
                "state registration failed");
        require(runtime->code_server()->load({"pin", std::make_shared<Image>(alive), std::move(registry)}).has_value(),
                "pin publication failed");
        pinned = runtime->code_server()->resolve({.module = "pin", .function = "state", .arity = 0}).value();
        auto second = runtime->code_server()->resolve({.module = "pin", .function = "state", .arity = 0}).value();
        auto *context = runtime->create_context().value();
        require(pinned->call(*context, {})->integer_value() == 1, "first state call failed");
        require(second.call(*context, {})->integer_value() == 2, "lookup copied callable state");
    }
    require(alive && !safe, "resolution did not pin target/image");
    pinned.reset();
    require(!alive && safe, "image destroyed before target capture");
}

// Check arguments before invoking a body, preserving explicit failures and host exceptions.
void check_calls() {
    auto runtime = Runtime::start().value();
    auto *context = runtime->create_context().value();
    auto registry = std::make_unique<ModuleRegistry>();
    int calls = 0;
    require(registry
                ->add("id", 1,
                      [&calls](ProcessContext &, std::span<const Term> args) {
                          ++calls;
                          return CallResult<Term>(args.front());
                      })
                .has_value(),
            "id fixture failed");
    require(runtime->code_server()->load({"calls", CodeImage::linked(), std::move(registry)}).has_value(),
            "load failed");
    const auto target = runtime->code_server()->resolve({.module = "calls", .function = "id", .arity = 1}).value();
    require(target.call(*context, {}).error().code == CallError::bad_arity, "arity unchecked");
    const std::array invalid{Term{}};
    auto bad = target.call(*context, invalid);
    require(!bad && bad.error().argument == 0 && bad.error().term_error == TermError::invalid_encoding,
            "bad argument lost");
    require(calls == 0, "invalid arguments entered target");
    const std::array valid{integer(-42)};
    require(target.call(*context, valid)->integer_value() == -42 && calls == 1, "generic identity failed");
}

// A builtin body that is never called.
Word unused(ProcessContext &, std::span<const Word>) { return 0; }

// The runtime extensions of this build (the crypto archive's), which programs linking them register after the
// production builtins.
std::vector<const RuntimeExtension *> extensions() {
#ifdef CLAUSE_TEST_CRYPTO
    return {static_cast<const RuntimeExtension *>(CLAUSE_crypto_extension_v1())};
#else
    return {};
#endif
}

// Whether the bridge catalog entry belongs to an extension this build lacks.
bool absent_extension(const clause::abi::v1::BuiltinName &name) {
    return extensions().empty() && name.module == "clause_crypto";
}

// Production registration with the extensions covers the bridge catalog by index; duplicates and invalid entries roll
// back a batch.
void check_builtin_registry() {
    RuntimeOptions options;
    options.extensions = extensions();
    auto runtime = Runtime::start(options).value();
    const auto &production = runtime->code_server()->builtins();
    for (std::size_t index = 0; index < clause::abi::v1::bridge_builtins.size(); ++index) {
        const auto &name = clause::abi::v1::bridge_builtins[index];
        if (absent_extension(name)) {
            continue;
        }
        const auto *frame = production.bridge(index);
        require(frame && frame == production.find(name.module, name.function, name.arity), "bridge builtin missing");
        require(!frame->frame.body && frame->frame.arity == name.arity, "builtin frame malformed");
    }
    require(!production.bridge(clause::abi::v1::bridge_builtins.size()), "bridge index out of range found");
    BuiltinRegistry registry;
    for (const auto family : production_builtins()) {
        require(registry.add(family).has_value(), "family rejected");
    }
    for (const auto *extension : extensions()) {
        for (const auto family : extension->families) {
            require(registry.add(family).has_value(), "extension family rejected");
        }
    }
    require(registry.size() == production.size(), "families differ from startup registration");
    require(registry.add(erlang_builtins()).error() == RegistryError::duplicate_key, "duplicate table accepted");
    require(registry.size() == production.size(), "duplicate table changed the registry");
    const std::array fresh{BuiltinEntry{"extra", "first", 0, unused}, BuiltinEntry{"extra", "second", 1, unused}};
    const std::array repeated{fresh[0], BuiltinEntry{"erlang", "abs", 1, unused}};
    require(registry.add(repeated).error() == RegistryError::duplicate_key, "existing name accepted");
    require(!registry.find("extra", "first", 0), "rejected batch kept its first entry");
    const std::array twice{fresh[0], fresh[0]};
    require(registry.add(twice).error() == RegistryError::duplicate_key && !registry.find("extra", "first", 0),
            "batch duplicate accepted");
    const std::array invalid{fresh[0], BuiltinEntry{"extra", "", 0, unused}, BuiltinEntry{"extra", "big", 256, unused},
                             BuiltinEntry{"extra", "none", 0, nullptr}};
    for (std::size_t bad = 1; bad < invalid.size(); ++bad) {
        const std::array batch{invalid[0], invalid[bad]};
        require(registry.add(batch).error() == RegistryError::invalid_entry, "invalid entry accepted");
    }
    require(registry.size() == production.size() && registry.add(fresh).has_value() &&
                registry.find("extra", "second", 1),
            "valid batch rejected after rollbacks");
}
} // namespace

// Validate the host registry boundary and the production builtin registry.
int main() {
    try {
        check_terms();
        check_pinning();
        check_calls();
        check_builtin_registry();
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
