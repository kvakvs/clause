#pragma once
#include "../typed.hpp"
#include <cstdint>
#include <optional>
#include <pcre2.h>
#include <span>
#include <terms.hpp>

// Shared parts of the re builtins (plan step 41ZB, docs/builtins.md#re-module): OTP's option lists and compiled
// patterns, {re_pattern, CaptureCount, Unicode, UseCRLF, Handle}, whose handle reference owns the pcre2_code.
namespace clause::runtime::builtins::regex {
// What an option list asked for, as OTP's parse_options records it.
struct Options {
    std::uint32_t compile = 0;
    std::uint32_t match = 0;
    std::uint32_t newline = 0;
    std::uint32_t bsr = 0;
    std::size_t offset = 0;
    std::optional<std::uint32_t> match_limit = std::nullopt;
    std::optional<std::uint32_t> depth_limit = std::nullopt;
    // An option only matching takes (global, offset, capture, ...) or only compiling takes (caseless, ...).
    bool exec_only = false;
    bool compile_only = false;
    bool unicode = false;
    bool global = false;
    bool report_errors = false;
    bool export_pattern = false;
    // The capture values and type of {capture, Values} or {capture, Values, Type}.
    std::optional<Term> capture_values = std::nullopt;
    std::optional<Term> capture_type = std::nullopt;
};

// The options of a proper list; none for an option OTP rejects.
std::optional<Options> parse(const Term &list);

// A pattern compiled from `bytes`, or OTP's {error, {Message, Offset}} reason: the message and the offset.
struct CompileError {
    int code;
    std::size_t offset;
};

pcre2_code *compile(std::span<const std::byte> bytes, const Options &options, CompileError &error);

// {re_pattern, CaptureCount, Unicode, UseCRLF, Handle} owning `code`.
Term pattern(ProcessContext &context, pcre2_code *code, bool unicode);
// The code of a compiled pattern term; none for anything else.
const pcre2_code *pattern_code(const Term &term);
// {Message, Offset} of a compile error, the message being PCRE2's own.
Term compile_error(ProcessContext &context, const CompileError &error);
} // namespace clause::runtime::builtins::regex
