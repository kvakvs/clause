#include "startup.hpp"
#include "../process/exceptions.hpp"
#include "../scheduler/executor.hpp"
#include <algorithm>
#include <array>
#include <clause/abi/startup.hpp>
#include <clause/runtime/modules.hpp>
#include <clause/runtime/output.hpp>
#include <cstdio>
#include <fstream>
#include <span>

namespace clause::runtime::detail {
namespace {
using abi::v1::StartupDescriptor;
using abi::v1::Status;

// Readable names of infrastructure statuses for runtime-failure messages.
std::string status_name(Status status) {
    static constexpr std::array<std::string_view, 14> names{"ok",
                                                            "not_implemented",
                                                            "invalid_argument",
                                                            "diagnostic_failure",
                                                            "out_of_memory",
                                                            "busy",
                                                            "wrong_owner",
                                                            "resource_limit",
                                                            "stopped",
                                                            "abi_mismatch",
                                                            "internal_error",
                                                            "unknown_builtin",
                                                            "erlang_error",
                                                            "output_failure"};
    const auto index = static_cast<std::size_t>(status);
    return index < names.size() ? std::string(names[index]) : "status " + std::to_string(index);
}

// Print one line on stderr after flushing program output, so both streams keep their relative order.
void report(std::string_view prefix, std::string_view text) noexcept {
    std::fflush(stdout);
    std::fwrite(prefix.data(), 1, prefix.size(), stderr);
    std::fwrite(text.data(), 1, text.size(), stderr);
    std::fputc('\n', stderr);
    std::fflush(stderr);
}

// Report a failure outside Erlang code (startup, registration, infrastructure) and select its exit status.
// Writing never allocates, so allocation-failure handlers can use it too.
int runtime_failure(std::string_view what) noexcept {
    report("clau: runtime failure: ", what);
    return abi::v1::exit_runtime_failure;
}

// Whether every array and spelling the descriptor borrows is present.
bool borrowed(const StartupDescriptor &startup) {
    return (startup.module_count == 0 || startup.modules) && (startup.extension_count == 0 || startup.extensions) &&
           startup.entry_module && startup.entry_function;
}

// Reject startup and module descriptors of another ABI revision or width before anything is registered.
bool compatible(const StartupDescriptor &startup) {
    constexpr auto bits = sizeof(abi::v1::TermWord) * 8;
    if (startup.abi_version != abi::v1::version || startup.term_bits != bits ||
        (startup.flags & ~(abi::v1::startup_escript | abi::v1::startup_no_arguments)) != 0 || !borrowed(startup)) {
        return false;
    }
    return std::ranges::all_of(std::span(startup.modules, startup.module_count), [](const auto *module) {
        return module && module->abi_version == abi::v1::version && module->term_bits == bits;
    });
}

// Whether the entry process was ended by an exit signal with reason normal, which exits like a return.
bool exited_normally(const CallFailure &failure) {
    return failure.code == CallError::exited && failure.value && failure.value->is_atom() &&
           failure.value->atom_spelling_unchecked() == "normal";
}

// Map a failed entry call to its report and exit status: halt request, uncaught exception, exit signal (reported as
// an uncaught exit) or runtime failure.
int failed_entry(const CallFailure &failure, bool escript) {
    if (failure.code == CallError::halted) {
        if (failure.value) {
            report("", slogan_text(*failure.value).value_or(""));
        }
        return failure.halt_status.value_or(abi::v1::exit_uncaught);
    }
    if (exited_normally(failure)) {
        return 0;
    }
    if (failure.code != CallError::erlang_exception && failure.code != CallError::exited) {
        return runtime_failure("entry call failed: " + status_name(failure.status.value_or(Status::internal_error)));
    }
    const auto reason = exception_reason(failure);
    if (!reason) {
        return runtime_failure("cannot format the uncaught exception");
    }
    const auto prefix = std::string(escript ? "escript: exception " : "uncaught exception ") +
                        std::string(exception_class(failure)) + ": ";
    report(prefix, *reason);
    return escript ? abi::v1::exit_escript_uncaught : abi::v1::exit_uncaught;
}

// Whether the program is an escript, whose uncaught exceptions report and exit differently.
bool is_escript(const StartupDescriptor &startup) { return (startup.flags & abi::v1::startup_escript) != 0; }

// The entry's arity: 1 receives the argument list, 0 runs without it.
unsigned entry_arity(const StartupDescriptor &startup) {
    return (startup.flags & abi::v1::startup_no_arguments) != 0 ? 0 : 1;
}

// Release a context the program no longer needs; a teardown failure replaces `status`.
int released(Runtime &runtime, ProcessContext &context, int status) {
    const auto destroyed = runtime.destroy_context(&context);
    return destroyed == Status::ok ? status : runtime_failure("process teardown: " + status_name(destroyed));
}

// The frame of the exported entry function; null when no registered module exports it with a frame.
const abi::v1::FrameDescriptor *entry_frame(ProcessContext &context, const StartupDescriptor &startup) {
    TermFactory factory(context);
    const auto module_atom = factory.atom(std::string_view{startup.entry_module, startup.entry_module_size});
    const auto function_atom = factory.atom(std::string_view{startup.entry_function, startup.entry_function_size});
    if (!module_atom || !function_atom) {
        return nullptr;
    }
    return static_cast<const abi::v1::FrameDescriptor *>(
        context.code_server().function_frame(*module_atom, *function_atom, entry_arity(startup)));
}

// Call an entry export without a frame (a host function) to completion in the main process.
int call_host_entry(ProcessContext &main, const StartupDescriptor &startup, const Term &arguments) {
    const std::string_view module(startup.entry_module, startup.entry_module_size);
    const std::string_view function(startup.entry_function, startup.entry_function_size);
    const auto arity = entry_arity(startup);
    const auto entry = main.code_server().resolve({module, function, arity});
    if (!entry) {
        return runtime_failure("entry function " + std::string(module) + ":" + std::string(function) + "/" +
                               std::to_string(arity) + " is not registered");
    }
    const auto result =
        arity == 0 ? entry->call(main, std::span<const Term>{}) : entry->call(main, std::array{arguments});
    return result ? 0 : failed_entry(result.error(), is_escript(startup));
}

// Run the main process and every process it spawns until the program ends, then release them all; the exit status
// comes from the process whose outcome ended the program (docs/processes.md).
int run_processes(Runtime &runtime, ProcessContext &main, const StartupDescriptor &startup) {
    auto &executor = detail::Executor::of(main);
    auto &ended = executor.run(main);
    const auto &failure = ended.generated_calls().failure();
    const int status = failure ? failed_entry(*failure, is_escript(startup)) : 0;
    // The main process is still queued when another process ended the program.
    executor.clear();
    return released(runtime, ended, status);
}

// Build argv without the runtime options and run the entry as the main process, passing argv to an arity-1 entry;
// no context remains after.
int run_entry(Runtime &runtime, ProcessContext &main, const StartupDescriptor &startup) {
    const auto arguments = argument_list(main, runtime.program_arguments().plain);
    if (!arguments) {
        return released(runtime, main, runtime_failure("cannot build the argument list"));
    }
    const auto *frame = entry_frame(main, startup);
    if (!frame) {
        return released(runtime, main, call_host_entry(main, startup, *arguments));
    }
    if (entry_arity(startup) == 1) {
        main.stack().registers()[0] = arguments->word();
    }
    if (!detail::Executor::of(main).start(main, *frame)) {
        const auto &failure = main.generated_calls().failure();
        const int status =
            failure ? failed_entry(*failure, is_escript(startup)) : runtime_failure("cannot start the entry process");
        return released(runtime, main, status);
    }
    return run_processes(runtime, main, startup);
}

// Register every module before the entry runs; a failure discards the whole runtime before entry.
int run_program(Runtime &runtime, const StartupDescriptor &startup) {
    for (const auto *module : std::span(startup.modules, startup.module_count)) {
        if (!register_module(runtime, *module)) {
            return runtime_failure("cannot register module " + std::string(module->name, module->name_size));
        }
    }
    const auto context = runtime.create_context();
    if (!context) {
        return runtime_failure("cannot create the entry process: " + status_name(context.error()));
    }
    return run_entry(runtime, **context, startup);
}

// Write the profile report (--profile FILE) once every process ended; a failure is reported on stderr but keeps
// the program's exit status.
void write_profile(const Runtime &runtime, const std::string &file) {
    if (file.empty()) {
        return;
    }
    std::ofstream out(file, std::ios::binary);
    out << runtime.profile_report();
    out.close();
    if (!out) {
        static_cast<void>(std::fprintf(stderr, "clau: cannot write profile %s\n", file.c_str()));
    }
}

// Run the start hooks of the linked extensions; the first failure, as "name: reason", else empty. Added for crypto's
// pinned FIPS mode (plan 21 step LIB79F).
std::string start_extensions(std::span<const RuntimeExtension *const> extensions) {
    for (const auto *extension : extensions) {
        if (!extension->start) {
            continue;
        }
        if (auto failure = extension->start(); !failure.empty()) {
            return std::string(extension->name) + ": " + failure;
        }
    }
    return {};
}

// Own the runtime for one program run and shut it down in order on every path.
int run(const StartupDescriptor &startup, int argc, char **argv) {
    if (!compatible(startup)) {
        return runtime_failure("startup ABI mismatch (rebuild objects and runtime together)");
    }
    auto options = program_options(std::span(startup.options, startup.option_count), argc, argv);
    if (!options) {
        return runtime_failure(options.error());
    }
    // init's flags come from the runtime options, the rest from the command line; the plain arguments after an args
    // file's -extra follow those of the command line, as erlexec puts them last.
    auto from_options = std::move(options->runtime.program);
    options->runtime.program = command_line(argc, argv, options->consumed);
    options->runtime.program.flags = std::move(from_options.flags);
    auto &plain = options->runtime.program.plain;
    plain.insert(plain.end(), from_options.plain.begin(), from_options.plain.end());
    for (const auto entry : std::span(startup.extensions, startup.extension_count)) {
        options->runtime.extensions.push_back(static_cast<const RuntimeExtension *>(entry()));
    }
    if (const auto failure = start_extensions(options->runtime.extensions); !failure.empty()) {
        return runtime_failure(failure);
    }
    auto runtime = Runtime::start(options->runtime);
    if (!runtime) {
        return runtime_failure("cannot start the runtime: " + status_name(runtime.error()));
    }
    const int status = run_program(**runtime, startup);
    write_profile(**runtime, options->runtime.profile);
    const auto stopped = (*runtime)->shutdown();
    std::fflush(stdout);
    return stopped == Status::ok ? status : runtime_failure("shutdown: " + status_name(stopped));
}
} // namespace

TermResult<std::string> exception_reason(const CallFailure &failure) {
    const auto name = failure.reason ? error_name(*failure.reason) : std::string_view{};
    // Raised reasons (error/exit/throw) are the whole payload term.
    if (name.empty() && failure.reason && failure.value) {
        return format_term(*failure.value, TermStyle::write);
    }
    if (name.empty()) {
        return std::unexpected(TermError::invalid_argument);
    }
    if (!failure.value) {
        return std::string(name);
    }
    return format_term(*failure.value, TermStyle::write).transform([&](const std::string &value) {
        return "{" + std::string(name) + "," + value + "}";
    });
}
} // namespace clause::runtime::detail

int CLAUSE_main_v1(int argc, char **argv, const void *startup) noexcept {
    using namespace clause;
    if (!startup) {
        return runtime::detail::runtime_failure("missing startup descriptor");
    }
    try {
        return runtime::detail::run(*static_cast<const abi::v1::StartupDescriptor *>(startup), argc, argv);
    } catch (const std::bad_alloc &) {
        return runtime::detail::runtime_failure("out of memory");
    } catch (...) {
        return runtime::detail::runtime_failure("internal error");
    }
}
