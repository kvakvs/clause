#include "../io_format.hpp"
#include "../string/string.hpp"
#include "../text.hpp"
#include "../typed.hpp"
#include "format.hpp"
#include <algorithm>
#include <array>

// io_lib's format functions as builtins (plan 21 step LIB4C, docs/builtins.md#io-lib): format/2,3, fwrite/2,3,
// bformat/2,3, bfwrite/2,3, scan_format/2, unscan_format/1 and build_text/1,2; io:format writes the same text.
namespace clause::runtime::builtins::io_lib {
namespace {
using string::Terms;

// The roles of the format functions' term arguments (typed.hpp's Named).
struct FormatRole;
struct ArgsRole;
using FormatArg = Named<FormatRole>;
using ArgsArg = Named<ArgsRole>;

// Whether io_lib's catch turns a failure into badarg: Erlang errors, but not Clause's nesting limit.
bool caught(const BuiltinFailure &failure) {
    return !failure.term && failure.reason != abi::v1::ErrorReason::raised_exit &&
           failure.reason != abi::v1::ErrorReason::raised_throw && failure.reason != abi::v1::ErrorReason::system_limit;
}

// io_lib's try ... catch C:R:S -> erlang:error(badarg, ...) around a body.
template <typename Body> auto badarg_on_error(Body &&body) -> decltype(body()) {
    try {
        return body();
    } catch (const BuiltinFailure &failure) {
        if (!caught(failure)) {
            throw;
        }
        bad_argument();
    }
}

// io_lib_format:fwrite/3 (fwrite_bin/3 when `binary`) as a term, with chars_limit `limit`.
Term formatted(ProcessContext &context, const FormatArg &format, const ArgsArg &arguments, const Term &limit,
               bool binary = false) {
    return badarg_on_error([&] {
        Deep deep;
        const auto elements = scan(context, format.term, arguments.term);
        return deep.to_term(context, build(context, deep, elements, limit, binary));
    });
}

Term minus_one(ProcessContext &context) { return Terms(context).integer(-1); }

Term format2(ProcessContext &context, const FormatArg &format, const ArgsArg &arguments) {
    return formatted(context, format, arguments, minus_one(context));
}

Term format3(ProcessContext &context, const FormatArg &format, const ArgsArg &arguments, const Term &options) {
    return badarg_on_error(
        [&] { return formatted(context, format, arguments, option_of(options, "chars_limit", minus_one(context))); });
}

// unicode:characters_to_binary/1 of the formatted text, as io_lib_format:fwrite_bin ends.
Term bformat2(ProcessContext &context, const FormatArg &format, const ArgsArg &arguments) {
    return Terms(context).characters_to_binary(formatted(context, format, arguments, minus_one(context), true));
}

Term bformat3(ProcessContext &context, const FormatArg &format, const ArgsArg &arguments, const Term &options) {
    const auto limit = badarg_on_error([&] { return option_of(options, "chars_limit", minus_one(context)); });
    return Terms(context).characters_to_binary(formatted(context, format, arguments, limit, true));
}

Term scan_format2(ProcessContext &context, const FormatArg &format, const ArgsArg &arguments) {
    return badarg_on_error([&] {
        std::vector<Term> items;
        for (const auto &element : scan(context, format.term, arguments.term)) {
            const auto *spec = std::get_if<Spec>(&element);
            items.push_back(spec ? spec_term(context, *spec) : std::get<Term>(element));
        }
        return Terms(context).list(items);
    });
}

Term unscan_format1(ProcessContext &context, const Term &format_list) { return unscan(context, format_list); }

// io_lib:build_text/1,2 with chars_limit `limit`.
Term built(ProcessContext &context, const FormatArg &format_list, const Term &limit) {
    return badarg_on_error([&] {
        Deep deep;
        const auto elements = elements_of(context, format_list.term);
        return deep.to_term(context, build(context, deep, elements, limit));
    });
}

Term build_text1(ProcessContext &context, const Term &format_list) {
    return built(context, {format_list}, minus_one(context));
}

Term build_text2(ProcessContext &context, const FormatArg &format_list, const Term &options) {
    return badarg_on_error(
        [&] { return built(context, format_list, option_of(options, "chars_limit", minus_one(context))); });
}

constexpr std::array IO_LIB_FORMAT_BUILTINS{
    typed_entry<format2>("io_lib", "format"),           typed_entry<format3>("io_lib", "format"),
    typed_entry<format2>("io_lib", "fwrite"),           typed_entry<format3>("io_lib", "fwrite"),
    typed_entry<bformat2>("io_lib", "bformat"),         typed_entry<bformat3>("io_lib", "bformat"),
    typed_entry<bformat2>("io_lib", "bfwrite"),         typed_entry<bformat3>("io_lib", "bfwrite"),
    typed_entry<scan_format2>("io_lib", "scan_format"), typed_entry<unscan_format1>("io_lib", "unscan_format"),
    typed_entry<build_text1>("io_lib", "build_text"),   typed_entry<build_text2>("io_lib", "build_text"),
};
} // namespace
} // namespace clause::runtime::builtins::io_lib

namespace clause::runtime::builtins {
std::u32string format_text(ProcessContext &context, const Term &format, std::optional<Term> arguments) {
    const auto args = arguments ? *arguments : string::Terms(context).nil();
    auto text = io_lib::badarg_on_error([&] {
        io_lib::Deep deep;
        const auto elements = io_lib::scan(context, format, args);
        return deep.flat(io_lib::build(context, deep, elements, string::Terms(context).integer(-1)));
    });
    if (!std::ranges::all_of(text, [](char32_t c) { return unicode_character(c); })) {
        bad_argument();
    }
    return text;
}
} // namespace clause::runtime::builtins

namespace clause::runtime {
std::span<const BuiltinEntry> io_lib_format_builtins() noexcept { return builtins::io_lib::IO_LIB_FORMAT_BUILTINS; }
} // namespace clause::runtime
