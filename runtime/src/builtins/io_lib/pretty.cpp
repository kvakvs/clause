#include "pretty.hpp"
#include "../text.hpp"
#include "pretty_format.hpp"
#include <algorithm>

// io_lib_pretty's entry points (plan 21 step LIB4D): print/10 and print_bin/10 choose between one line and the
// broken layout; io_lib:write/5 and write_bin/5 write intermediate/7's result on one line.
namespace clause::runtime::builtins::io_lib {
namespace {
using pretty::Arena;
using pretty::Bounds;
using pretty::Index;

// The terms print/10 lays out; other terms are written by io_lib:write/1.
bool laid_out(const Term &term) {
    return term.is_tuple() || term.is_list() || term.is_map() || term.is_bitstring() || term.is_native_record();
}

// print_length/7 (no chars_limit) or intermediate/7 of the term.
Index intermediate_of(Arena &arena, const Term &term, const PrettyOptions &options) {
    return options.chars_limit < 0 ? arena.length(term, options.depth, options.chars_limit)
                                   : arena.intermediate(term, options.depth, options.chars_limit);
}

// Whether print/10 writes the whole term on one line: no line length, or room for it.
bool one_line(std::int64_t length, const PrettyOptions &options, const Bounds &bounds, std::int64_t column) {
    if (options.line_length == 0) {
        return true;
    }
    if (!options.numeric) {
        throw BuiltinFailure{.reason = abi::v1::ErrorReason::badarith};
    }
    return length < options.line_length - column && length <= bounds.most;
}

// The text of an atomic term written by io_lib:write_bin/5 and the size it reports.
std::pair<std::string, std::int64_t> written_bin(ProcessContext &context, const Term &term, Encoding encoding) {
    Deep deep;
    Writer writer(context, deep, encoding, MapsOrder::undefined);
    const auto text = writer.write(term, -1);
    return {utf8(deep.flat(text)), writer.size_of(text)};
}
} // namespace

Deep::Ref pretty_print(ProcessContext &context, Deep &deep, const Term &term, const PrettyOptions &options) {
    if (options.depth == 0 || options.chars_limit == 0) {
        return deep.ascii("...");
    }
    if (term.is_atom()) {
        return atom_text(deep, term, options.encoding);
    }
    if (!laid_out(term)) {
        return Writer(context, deep, Encoding::latin1, MapsOrder::undefined).write(term, -1);
    }
    Arena arena(
        context, deep,
        {.encoding = options.encoding, .strings = options.strings, .order = options.order, .records = options.records});
    const auto root = intermediate_of(arena, term, options);
    const auto length = arena.node(root).length;
    Bounds bounds{.line_length = options.line_length,
                  .most = options.line_max_chars < 0 ? length : options.line_max_chars};
    const auto column = std::max<std::int64_t>(options.column, 1);
    if (one_line(length, options, bounds, column)) {
        return pretty::write(arena, root);
    }
    bounds.tag = pretty::tag_indent(arena, root, bounds, column);
    return pretty::layout(arena, root, bounds, column);
}

PrettyBinary pretty_print_bin(ProcessContext &context, const Term &term, const PrettyOptions &options) {
    if (options.depth == 0 || options.chars_limit == 0) {
        return {.bytes = "...", .size = 3, .column = -3};
    }
    if (!laid_out(term)) {
        const auto [bytes, size] = written_bin(context, term, term.is_atom() ? options.encoding : Encoding::unicode);
        return {.bytes = bytes, .size = size, .column = -size};
    }
    Deep deep;
    Arena arena(context, deep,
                {.encoding = options.encoding, .utf8 = true, .strings = options.strings, .order = options.order});
    const auto root = intermediate_of(arena, term, options);
    const auto length = arena.node(root).length;
    Bounds bounds{.line_length = options.line_length,
                  .most = options.line_max_chars < 0 ? length : options.line_max_chars};
    const auto column = std::max<std::int64_t>(options.column, 1);
    if (one_line(length, options, bounds, column)) {
        return {.bytes = utf8(deep.flat(pretty::write(arena, root))), .size = length, .column = -length};
    }
    bounds.tag = pretty::tag_indent(arena, root, bounds, column);
    auto laid = pretty::layout_bin(arena, root, bounds, column);
    if (laid.newlines > 0) {
        return {.bytes = std::move(laid.bytes), .size = length + column * laid.newlines, .column = laid.position - 1};
    }
    return {.bytes = std::move(laid.bytes), .size = length, .column = -laid.position};
}

Deep::Ref limited_write(ProcessContext &context, Deep &deep, const Term &term, const LimitedWrite &options) {
    Arena arena(context, deep, {.encoding = options.encoding, .strings = false, .order = options.order});
    return pretty::write(arena, arena.intermediate(term, options.depth, options.chars_limit));
}

std::pair<std::string, std::int64_t> limited_write_bin(ProcessContext &context, const Term &term,
                                                       const LimitedWrite &options) {
    Deep deep;
    Arena arena(context, deep, {.encoding = options.encoding, .utf8 = true, .strings = false, .order = options.order});
    const auto root = arena.intermediate(term, options.depth, options.chars_limit);
    return {utf8(deep.flat(pretty::write(arena, root))), arena.node(root).length};
}

std::u32string pretty_text(ProcessContext &context, const Term &term) {
    Deep deep;
    return deep.flat(pretty_print(context, deep, term, {}));
}
} // namespace clause::runtime::builtins::io_lib
