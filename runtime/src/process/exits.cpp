#include "exits.hpp"
#include "../builtins/io_lib/pretty.hpp"
#include "../builtins/text.hpp"
#include "exceptions.hpp"
#include <array>
#include <chrono>
#include <clause/runtime/output.hpp>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <format>
#include <string>

namespace clause::runtime::detail {
namespace {
using abi::v1::ErrorReason;

// The local calendar time of `time`.
std::tm local_time(std::time_t time) {
    std::tm result{};
#ifdef _WIN32
    localtime_s(&result, &time);
#else
    localtime_r(&time, &result);
#endif
    return result;
}

// OTP's legacy report header: =ERROR REPORT==== 8-Oct-2026::03:42:15.983000 ===
std::string report_header() {
    static constexpr std::array<std::string_view, 12> months{"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                                             "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    const auto now = std::chrono::system_clock::now();
    const auto local = local_time(std::chrono::system_clock::to_time_t(now));
    const auto micros =
        std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count() % 1'000'000;
    return std::format("=ERROR REPORT==== {}-{}-{}::{:02}:{:02}:{:02}.{:06} ===\n", local.tm_mday,
                       months.at(static_cast<std::size_t>(local.tm_mon)), local.tm_year + 1900, local.tm_hour,
                       local.tm_min, local.tm_sec, micros);
}

// A term as ~p prints it at the start of a line, or as ~w when the layout refuses it (nesting too deep).
std::string pretty_text(ProcessContext &process, const Term &term) {
    try {
        return builtins::utf8(builtins::io_lib::pretty_text(process, term));
    } catch (const builtins::BuiltinFailure &) {
        return format_term(term, TermStyle::write).value_or("");
    }
}

// The whole report text: "Error in process ~p with exit value:~n~p~n" under the header, then a blank line.
std::string report_text(ProcessContext &process) {
    const auto reason = exit_reason(process);
    const auto pid = TermFactory(process).pid(process.identity());
    if (!reason || !pid) {
        return {};
    }
    return report_header() + "Error in process " + format_term(*pid, TermStyle::write).value_or("") +
           " with exit value:\n" + pretty_text(process, *reason) + "\n\n";
}

// Whether the logger's default handler writes on standard error: CLAUSE_LOGGER_TYPE=standard_error, as the golden
// runner sets it (library/kernel/clause_logger.erl).
bool logger_on_stderr() noexcept {
    constexpr std::string_view WANTED = "standard_error";
#ifdef _WIN32
    std::array<char, 32> type{};
    std::size_t size = 0;
    return getenv_s(&size, type.data(), type.size(), "CLAUSE_LOGGER_TYPE") == 0 && type.data() == WANTED;
#else
    const auto *type = std::getenv("CLAUSE_LOGGER_TYPE");
    return type != nullptr && type == WANTED;
#endif
}
} // namespace

TermResult<Term> exit_reason(ProcessContext &process) {
    TermFactory factory(process);
    const auto &failure = process.generated_calls().failure();
    if (!failure) {
        return factory.atom("normal"_atom);
    }
    auto reason = exception_reason_term(process, *failure);
    if (!reason || failure->reason == ErrorReason::raised_exit) {
        return reason;
    }
    if (failure->reason == ErrorReason::raised_throw) {
        const auto tag = factory.atom("nocatch"_atom);
        reason = tag ? factory.tuple(std::array{*tag, *reason}) : tag;
    }
    const auto stack = stack_term(process, *failure);
    if (!reason || !stack) {
        return reason ? stack : reason;
    }
    return factory.tuple(std::array{*reason, *stack});
}

void report_exit(ProcessContext &process) noexcept {
    const auto &failure = process.generated_calls().failure();
    if (!failure || failure->code != CallError::erlang_exception || failure->reason == ErrorReason::raised_exit) {
        return;
    }
    try {
        const auto text = report_text(process);
        std::fflush(stdout);
        std::fwrite(text.data(), 1, text.size(), stderr);
        std::fflush(stderr);
    } catch (...) {
        // The report could not be built; say so instead of dropping it, the process ends all the same.
        std::fputs("Error in process: its exit value cannot be printed\n\n", stderr);
    }
}

void report_error(ProcessContext &process, std::string_view text) {
    const auto report = report_header() + std::string(text) + "\n\n";
    if (logger_on_stderr()) {
        std::fflush(stdout);
        std::fwrite(report.data(), 1, report.size(), stderr);
        std::fflush(stderr);
    } else {
        static_cast<void>(write_output(process.standard_output(), report));
    }
}
} // namespace clause::runtime::detail
