#include <array>
#include <clause/abi/equality.hpp>
#include <clause/abi/startup.hpp>
#include <iostream>
#include <span>
#include <stdexcept>

// Startup invariants that compiled source cannot reach: mismatched or failing descriptors never run the entry.
namespace {
using namespace clause::abi::v1;

// Count entry invocations to prove that failed startups stop before Erlang code.
int calls = 0;

// Stand-in generated entry: records the call and returns [].
TermWord entry(Context *, const TermWord *) {
    ++calls;
    return empty_list;
}

// Retain readable failures in optimized builds.
void require(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

constexpr std::array exports{ExportDescriptor{"main", 4, 1, &entry}};
constexpr std::uint32_t bits = sizeof(TermWord) * 8;
constexpr ModuleDescriptor app{version, bits, "app", 3, exports.data(), exports.size()};
constexpr ModuleDescriptor stale{version - 1, bits, "old", 3, exports.data(), exports.size()};

// Describe a program whose entry is app:main/1 over the given modules.
StartupDescriptor program(std::span<const ModuleDescriptor *const> modules) {
    return {version, bits, modules.data(), modules.size(), "app", 3, "main", 4, 0, nullptr, 0, nullptr, 0};
}

// Run one program from a clean call count and return its exit status.
int run(const StartupDescriptor &startup) {
    calls = 0;
    return CLAUSE_main_v1(0, nullptr, &startup);
}

// Every rejected startup reports a runtime failure without calling the entry.
void rejected(const StartupDescriptor &startup, const char *message) {
    require(run(startup) == exit_runtime_failure && calls == 0, message);
}
} // namespace

int main() {
    try {
        const std::array valid{&app};
        require(run(program(valid)) == 0 && calls == 1, "valid startup did not run the entry once");
        auto changed = program(valid);
        changed.abi_version = version + 1;
        rejected(changed, "startup ABI revision mismatch reached the entry");
        changed = program(valid);
        changed.term_bits = bits == 64 ? 32 : 64;
        rejected(changed, "startup term width mismatch reached the entry");
        changed = program(valid);
        changed.flags = startup_escript << 1;
        rejected(changed, "unknown startup flags reached the entry");
        const std::array mixed{&app, &stale};
        rejected(program(mixed), "module ABI mismatch reached the entry");
        const std::array duplicate{&app, &app};
        rejected(program(duplicate), "failed registration reached the entry");
        changed = program(valid);
        changed.entry_function_size = 3;
        rejected(changed, "missing entry function reached a different export");
        require(CLAUSE_main_v1(0, nullptr, nullptr) == exit_runtime_failure, "null startup accepted");
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
