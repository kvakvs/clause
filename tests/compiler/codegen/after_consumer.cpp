#include "link_name.hpp"
#include <array>
#include <clause/runtime/modules.hpp>
#include <clause/runtime/output.hpp>
#include <iostream>
#include <process_heap.hpp>
#include <stdexcept>
#include <string>

extern clause::abi::v1::GeneratedRegistration register_cleanup CLAUSE_LINK_NAME("clausev1_636c65616e7570__0.register");

namespace {
using namespace clause::runtime;
using clause::abi::v1::Status;

// Keep behavioral assertions enabled in every native optimization configuration.
void require(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

// Print a returned value or the class of a failure, so the CMake driver compares the whole sequence.
void print(const CallResult<Term> &result) {
    if (result) {
        std::cout << format_term(*result, TermStyle::write).value() << '\n';
    } else if (result.error().code == CallError::erlang_exception && result.error().value) {
        std::cout << "exception " << format_term(*result.error().value, TermStyle::write).value() << '\n';
    } else {
        // The process budget is the only expected infrastructure failure.
        const bool budget = result.error().status == Status::resource_limit;
        std::cout << "runtime failure " << (budget ? "resource_limit" : "unexpected") << '\n';
    }
}

// After every call, nothing generated remains on the root stack and the heap parses with only owned cells.
void check_clean(ProcessContext &context) {
    require(context.stack().depth() == 0, "generated root frames leaked");
    require(context.heap().verify().has_value(), "heap does not verify");
    require(!context.generated_calls().failure().has_value(), "failure channel not cleared");
}

// cleanup:run(Mode, Size) returns (mode 0) or throws (mode 1); its after body builds a Size-byte binary.
void run(const ResolvedFunction &entry, ProcessContext &context, std::int64_t mode, std::int64_t size) {
    const std::array arguments{Term::from_word(encode_integer(mode).value()).value(),
                               Term::from_word(encode_integer(size).value()).value()};
    print(entry.call(context, arguments));
    check_clean(context);
}
} // namespace

// A small process budget makes the after body's allocation fail on the normal and on the raising path; each
// failure must leave the context reusable and the heap consistent.
int main() {
    auto runtime = Runtime::start().value();
    require(register_cleanup(runtime.get()) == 0, "registration failed");
    auto *context = runtime->create_context(HeapOptions{233, std::size_t{256} * 1024}).value();
    const auto entry = runtime->code_server()->resolve({"cleanup", "run", 2}).value();
    for (int repeat = 0; repeat < 2; ++repeat) {
        run(entry, *context, 0, 100);
        run(entry, *context, 1, 100);
        run(entry, *context, 0, 1 << 20);
        run(entry, *context, 1, 1 << 20);
    }
    require(runtime->destroy_context(context) == Status::ok, "context teardown failed");
    require(runtime->shutdown() == Status::ok, "runtime shutdown failed");
    return 0;
}
