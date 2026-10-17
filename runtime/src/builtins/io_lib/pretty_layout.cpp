#include "pretty_format.hpp"
#include <algorithm>
#include <array>

// io_lib_pretty's layout (plan 21 step LIB4D): write/1 puts a value on one line, cind/7 checks that a tag
// indentation keeps every line within bounds, and pp/8 breaks what does not fit, all in OTP's deep list shapes.
namespace clause::runtime::builtins::io_lib::pretty {
namespace {
// io_lib_pretty's no_good: an indentation passing half the line without room for the whole term.
struct NoGood {};

// ?ATM_PAIR(Pair) and ?ATM_FLD(Field): the key and value, or the field's value, are chars.
bool atomic_pair(const Arena &arena, const Node &pair) {
    return atomic(arena.node(pair.key)) && atomic(arena.node(pair.value));
}

bool atomic_field(const Arena &arena, const Node &field) { return atomic(arena.node(field.value)); }

// map_value_indent/1.
std::int64_t value_indent(std::int64_t tag) { return tag > 0 ? tag : 4; }

// chars/2: N copies of a space as io_lib_pretty's balanced deep list (one space is [$\s, []]).
Deep::Ref spaces(Deep &deep, std::int64_t n) {
    if (n == 0) {
        return Deep::NIL;
    }
    const auto c = deep.code(' ');
    if (n == 2 || n == 3) {
        const std::array copies{c, c, c};
        return deep.list(std::span(copies).first(static_cast<std::size_t>(n)));
    }
    const auto half = spaces(deep, n >> 1);
    return (n & 1) == 0 ? deep.cons(half, half) : deep.cons(c, deep.cons(half, half));
}

// indent/2: N spaces in front of an indentation.
Deep::Ref indent(Deep &deep, std::int64_t n, Deep::Ref ind) {
    if (n == 1) {
        return deep.cons(deep.code(' '), ind);
    }
    if (n == 4) {
        const auto two = deep.list({deep.code(' '), deep.code(' ')});
        return deep.cons(two, deep.cons(two, ind));
    }
    return deep.cons(spaces(deep, n), ind);
}

// The number of elements of a deep list's top level (length/1).
std::int64_t top_length(const Deep &deep, Deep::Ref ref) {
    std::int64_t count = 0;
    for (; deep.kind(ref) == Deep::Kind::cons; ref = deep.tail(ref)) {
        ++count;
    }
    return count;
}

// Whether a cell is the character `c`.
bool is_code(const Deep &deep, Deep::Ref ref, char32_t c) {
    return deep.kind(ref) == Deep::Kind::code && deep.code_of(ref) == c;
}

// Prepends `cells` in order to `tail`.
Deep::Ref prepend(Deep &deep, const std::vector<Deep::Ref> &cells, Deep::Ref tail) {
    for (auto it = cells.rbegin(); it != cells.rend(); ++it) {
        tail = deep.cons(*it, tail);
    }
    return tail;
}

// write/1 and its helpers over the arena.
class OneLine final {
  public:
    explicit OneLine(const Arena &arena) : arena_(arena), deep_(arena.deep()) {}

    Deep::Ref write(Index index) {
        const auto &node = arena_.node(index);
        switch (node.shape) {
        case Shape::tuple:
            return deep_.list({deep_.code('{'), list(node.seq, ','), deep_.code('}')});
        case Shape::list:
            return deep_.list({deep_.code('['), list(node.seq, '|'), deep_.code(']')});
        case Shape::map:
            return deep_.list({deep_.code('#'), deep_.code('{'), list(node.seq, ','), deep_.code('}')});
        case Shape::map_pair:
            return pair(node);
        case Shape::record:
            return deep_.list({node.text, deep_.code('{'), fields(node.seq), deep_.code('}')});
        default:
            return node.text;
        }
    }

    // write_pair/1.
    Deep::Ref pair(const Node &pair) { return deep_.list({write(pair.key), deep_.ascii(" => "), write(pair.value)}); }

    // write_field/1.
    Deep::Ref field(const Node &field) {
        return deep_.cons(field.text, deep_.cons(deep_.ascii(" = "), write(field.value)));
    }

  private:
    bool is_dots(Index index) const { return index != NONE && arena_.node(index).shape == Shape::dots; }

    // write_list/2 and write_tail/2.
    Deep::Ref list(const Seq &seq, char32_t separator) {
        if (seq.items.empty()) {
            return is_dots(seq.end) ? deep_.ascii("...") : Deep::NIL;
        }
        auto tail = Deep::NIL;
        if (is_dots(seq.end)) {
            tail = deep_.cons(deep_.code(separator), deep_.ascii("..."));
        } else if (seq.end != NONE) {
            tail = deep_.cons(deep_.code(separator), write(seq.end));
        }
        for (auto i = seq.items.size() - 1; i > 0; --i) {
            tail = deep_.cons(deep_.code(','), deep_.cons(write(seq.items[i]), tail));
        }
        return deep_.cons(write(seq.items[0]), tail);
    }

    // write_fields/1 and write_fields_tail/1.
    Deep::Ref fields(const Seq &seq) {
        if (seq.items.empty()) {
            return is_dots(seq.end) ? deep_.ascii("...") : Deep::NIL;
        }
        auto tail = is_dots(seq.end) ? deep_.ascii(",...") : Deep::NIL;
        for (auto i = seq.items.size() - 1; i > 0; --i) {
            tail = deep_.cons(deep_.code(','), deep_.cons(field(arena_.node(seq.items[i])), tail));
        }
        return deep_.cons(field(arena_.node(seq.items[0])), tail);
    }

    const Arena &arena_;
    Deep &deep_;
};

// cind/7 and its helpers: the indentation `ind` when every chosen column is within bounds, else NoGood.
class Indent final {
  public:
    Indent(const Arena &arena, const Bounds &bounds, std::int64_t ind) : arena_(arena), bounds_(bounds), ind_(ind) {}

    void check(Index index, const Place &at) {
        const auto &node = arena_.node(index);
        if (fits(node, at)) {
            return;
        }
        switch (node.shape) {
        case Shape::list:
            list(node.seq, 0, {at.column + 1, at.last, at.width + 1});
            break;
        case Shape::tuple:
            node.tagged ? tag_tuple(node.seq, {at.column, at.last, at.width + 1})
                        : list(node.seq, 0, {at.column + 1, at.last, at.width + 1});
            break;
        case Shape::map:
            map(node.seq, {at.column + 2, at.last, at.width + 2});
            break;
        case Shape::record:
            record(node.seq, node.name_length, {at.column, at.last, at.width + node.name_length + 1});
            break;
        default:
            break;
        }
    }

  private:
    bool fits(const Node &node, const Place &at) const {
        return node.length < bounds_.line_length - at.column - at.last &&
               node.length + at.width + at.last <= bounds_.most;
    }

    // The test for an element after a comma, with LD1 closing characters after it.
    bool fits_next(std::int64_t length, const Place &at) const {
        const auto element = length + 1;
        return at.last == 0 ? element + 1 < bounds_.line_length - at.column && at.width + element + 1 <= bounds_.most
                            : element < bounds_.line_length - at.column - at.last &&
                                  at.width + element + at.last <= bounds_.most;
    }

    // Whether a column is acceptable: room for M characters, or within half the line.
    bool room(std::int64_t column) const {
        return bounds_.most + column <= bounds_.line_length || column <= bounds_.line_length / 2;
    }

    // cind_tag_tuple/7.
    void tag_tuple(const Seq &seq, const Place &at) {
        if (seq.items.empty()) {
            return;
        }
        const auto tag_length = arena_.node(seq.items[0]).length;
        const auto tag_column = at.column + tag_length + 2;
        if (ind_ > 0 && tag_length + 2 > ind_) {
            const auto column = at.column + ind_;
            if (!room(column)) {
                throw NoGood{};
            }
            tail(seq, 1, {tag_column, at.last, at.width + tag_length}, column);
        } else if (bounds_.most + tag_column < bounds_.line_length || tag_column < bounds_.line_length / 2) {
            list(seq, 1, {tag_column, at.last, at.width + tag_length + 1});
        } else {
            throw NoGood{};
        }
    }

    // cind_map/7 and cind_pairs_tail/8.
    void map(const Seq &seq, const Place &at) {
        if (seq.items.empty()) {
            return;
        }
        const auto first = pair(arena_.node(seq.items[0]), {at.column, last_depth(seq, 1, at.last), at.width});
        Place next{at.column + first, at.last, at.width + first};
        for (std::size_t i = 1; i < seq.items.size(); ++i) {
            const auto &pair_node = arena_.node(seq.items[i]);
            const auto last = last_depth(seq, i + 1, at.last);
            if (fits_next(pair_node.length, {next.column, last, next.width}) && atomic_pair(arena_, pair_node)) {
                next.column += pair_node.length + 1;
                next.width += pair_node.length + 1;
            } else {
                const auto width = pair(pair_node, {at.column, last, 0});
                next = {at.column + width, at.last, width};
            }
        }
    }

    // cind_pair/7: the width the pair takes, the line length when it forces a new line.
    std::int64_t pair(const Node &pair_node, const Place &at) {
        if (fits(pair_node, at)) {
            return atomic_pair(arena_, pair_node) ? pair_node.length : bounds_.line_length;
        }
        check(pair_node.key, at);
        check(pair_node.value, {at.column + value_indent(ind_), at.last, 0});
        return bounds_.line_length;
    }

    // cind_record/8 and cind_fields_tail/8 (OTP continues a broken field at Col + FW, not Col0 + FW).
    void record(const Seq &seq, std::int64_t name_length, const Place &at) {
        if (seq.items.empty()) {
            return;
        }
        const auto [column, width] = record_indent(name_length + 1, at);
        const auto first = field(arena_.node(seq.items[0]), {column, last_depth(seq, 1, at.last), width});
        Place next{column + first, at.last, width + first};
        for (std::size_t i = 1; i < seq.items.size(); ++i) {
            const auto &field_node = arena_.node(seq.items[i]);
            const auto last = last_depth(seq, i + 1, at.last);
            if (fits_next(field_node.length, {next.column, last, next.width}) && atomic_field(arena_, field_node)) {
                next.column += field_node.length + 1;
                next.width += field_node.length + 1;
            } else {
                const auto field_width = field(field_node, {column, last, 0});
                next = {next.column + field_width, at.last, field_width};
            }
        }
    }

    // cind_field/7.
    std::int64_t field(const Node &field_node, const Place &at) {
        if (fits(field_node, at)) {
            return atomic_field(arena_, field_node) ? field_node.length : bounds_.line_length;
        }
        const auto [column, width] =
            record_indent(field_node.name_length, {at.column, at.last, at.width + field_node.name_length});
        check(field_node.value, {column, at.last, width});
        return bounds_.line_length;
    }

    // cind_rec/6: the column and width after a record name or field name.
    std::pair<std::int64_t, std::int64_t> record_indent(std::int64_t indent, const Place &at) {
        const bool newline = ind_ > 0 && indent > ind_;
        const auto next = at.column + (newline ? ind_ : indent);
        if (!room(next)) {
            throw NoGood{};
        }
        return {next, newline ? 0 : at.width};
    }

    // cind_list/7.
    void list(const Seq &seq, std::size_t from, const Place &at) {
        if (from >= seq.items.size()) {
            return;
        }
        const auto width = element(seq.items[from], {at.column, last_depth(seq, from + 1, at.last), at.width});
        tail(seq, from + 1, {at.column + width, at.last, at.width + width}, at.column);
    }

    // cind_tail/8 from element `from`, continuation lines starting at `column`.
    void tail(const Seq &seq, std::size_t from, Place at, std::int64_t column) {
        for (auto i = from; i < seq.items.size(); ++i) {
            const auto &node = arena_.node(seq.items[i]);
            const auto last = last_depth(seq, i + 1, at.last);
            if (fits_next(node.length, {at.column, last, at.width}) && atomic(node)) {
                at.column += node.length + 1;
                at.width += node.length + 1;
            } else {
                const auto width = element(seq.items[i], {column, last, 0});
                at = {column + width, at.last, width};
            }
        }
        if (seq.end == NONE || arena_.node(seq.end).shape == Shape::dots) {
            return;
        }
        const auto &end = arena_.node(seq.end);
        const auto length = end.length + 1;
        const auto last = at.last + 1;
        const bool fits_end =
            length < bounds_.line_length - at.column - last && length + at.width + last <= bounds_.most && atomic(end);
        if (!fits_end) {
            check(seq.end, {at.column, last, 0});
        }
    }

    // cind_element/7.
    std::int64_t element(Index index, const Place &at) {
        const auto &node = arena_.node(index);
        if (fits(node, at) && atomic(node)) {
            return node.length;
        }
        check(index, at);
        return bounds_.line_length;
    }

    const Arena &arena_;
    const Bounds &bounds_;
    std::int64_t ind_;
};

// pp/8 and its helpers: the broken layout as deep lists.
class Broken final {
  public:
    Broken(const Arena &arena, const Bounds &bounds)
        : arena_(arena), deep_(arena.deep()), bounds_(bounds), tag_(bounds.tag), line_(arena) {}

    Deep::Ref pp(Index index, const Place &at, Deep::Ref ind) {
        const auto &node = arena_.node(index);
        if (fits(node, at)) {
            return line_.write(index);
        }
        const auto open = [&](char32_t c) { return deep_.code(c); };
        switch (node.shape) {
        case Shape::list:
            return deep_.list({open('['),
                               list(node.seq, 0, {at.column + 1, at.last, at.width + 1}, indent(deep_, 1, ind), '|'),
                               open(']')});
        case Shape::tuple:
            if (node.tagged) {
                return deep_.list({open('{'), tag_tuple(node.seq, {at.column, at.last, at.width + 1}, ind), open('}')});
            }
            return deep_.list({open('{'),
                               list(node.seq, 0, {at.column + 1, at.last, at.width + 1}, indent(deep_, 1, ind), ','),
                               open('}')});
        case Shape::map:
            return deep_.list({open('#'), open('{'),
                               map(node.seq, {at.column + 2, at.last, at.width + 1}, indent(deep_, 2, ind)),
                               open('}')});
        case Shape::record:
            return deep_.list(
                {node.text, open('{'),
                 record(node.seq, node.name_length, {at.column, at.last, at.width + node.name_length + 1}, ind),
                 open('}')});
        case Shape::bin:
            return binary(node.text, at, indent(deep_, 2, ind));
        default:
            return node.text;
        }
    }

  private:
    // A part and the width it takes on its line (the line length when it forces a new line).
    struct Part {
        Deep::Ref text;
        std::int64_t width;
    };

    bool fits(const Node &node, const Place &at) const {
        return node.length < bounds_.line_length - at.column - at.last &&
               node.length + at.width + at.last <= bounds_.most;
    }

    bool fits_next(std::int64_t length, const Place &at) const {
        const auto element = length + 1;
        return at.last == 0 ? element + 1 < bounds_.line_length - at.column && at.width + element + 1 <= bounds_.most
                            : element < bounds_.line_length - at.column - at.last &&
                                  at.width + element + at.last <= bounds_.most;
    }

    // pp_tag_tuple/8: the elements after the tag at the tag indentation when the tag is wider, else after it.
    Deep::Ref tag_tuple(const Seq &seq, const Place &at, Deep::Ref ind) {
        if (seq.items.empty()) {
            return deep_.ascii("...");
        }
        const auto &tag = arena_.node(seq.items[0]);
        const auto tag_indent = tag.length + 2;
        const auto tag_column = at.column + tag_indent;
        if (tag_ > 0 && tag_indent > tag_) {
            return deep_.cons(tag.text, tail(seq, 1, {tag_column, at.last, at.width + tag.length}, at.column + tag_,
                                             indent(deep_, tag_, ind), ','));
        }
        return deep_.cons(tag.text,
                          deep_.cons(deep_.code(','), list(seq, 1, {tag_column, at.last, at.width + tag.length + 1},
                                                           indent(deep_, tag_indent, ind), ',')));
    }

    // pp_list/9.
    Deep::Ref list(const Seq &seq, std::size_t from, const Place &at, Deep::Ref ind, char32_t separator) {
        if (from >= seq.items.size()) {
            return seq.end != NONE ? deep_.ascii("...") : Deep::NIL;
        }
        const auto first = element(seq.items[from], {at.column, last_depth(seq, from + 1, at.last), at.width}, ind);
        return deep_.cons(first.text, tail(seq, from + 1, {at.column + first.width, at.last, at.width + first.width},
                                           at.column, ind, separator));
    }

    // pp_tail/10 from element `from`; continuation lines start at `column`.
    Deep::Ref tail(const Seq &seq, std::size_t from, Place at, std::int64_t column, Deep::Ref ind, char32_t separator) {
        std::vector<Deep::Ref> cells;
        for (auto i = from; i < seq.items.size(); ++i) {
            const auto &node = arena_.node(seq.items[i]);
            const auto last = last_depth(seq, i + 1, at.last);
            if (fits_next(node.length, {at.column, last, at.width}) && atomic(node)) {
                cells.insert(cells.end(), {deep_.code(','), line_.write(seq.items[i])});
                at.column += node.length + 1;
                at.width += node.length + 1;
            } else {
                const auto part = element(seq.items[i], {column, last, 0}, ind);
                cells.insert(cells.end(), {deep_.code(','), deep_.code('\n'), ind, part.text});
                at = {column + part.width, at.last, part.width};
            }
        }
        return prepend(deep_, cells, tail_end(seq, column, at, ind, separator));
    }

    // What ends pp_tail/10: [], "|..." or ",...", or an improper tail.
    Deep::Ref tail_end(const Seq &seq, std::int64_t column, const Place &at, Deep::Ref ind, char32_t separator) {
        if (seq.end == NONE) {
            return Deep::NIL;
        }
        const auto &end = arena_.node(seq.end);
        if (end.shape == Shape::dots) {
            return deep_.cons(deep_.code(separator), deep_.ascii("..."));
        }
        const auto length = end.length + 1;
        const auto last = at.last + 1;
        if (length < bounds_.line_length - at.column - last && length + at.width + last <= bounds_.most &&
            atomic(end)) {
            return deep_.cons(deep_.code(separator), line_.write(seq.end));
        }
        return deep_.list({deep_.code(separator), deep_.code('\n'), ind}, pp(seq.end, {column, last, 0}, ind));
    }

    // pp_element/8.
    Part element(Index index, const Place &at, Deep::Ref ind) {
        const auto &node = arena_.node(index);
        if (fits(node, at) && atomic(node)) {
            return {line_.write(index), node.length};
        }
        return {pp(index, at, ind), bounds_.line_length};
    }

    // pp_map/8 and pp_pairs_tail/9 (the second pair starts with W = PW, as in OTP).
    Deep::Ref map(const Seq &seq, const Place &at, Deep::Ref ind) {
        if (seq.items.empty()) {
            return seq.end != NONE ? deep_.ascii("...") : Deep::NIL;
        }
        const auto first = pair(arena_.node(seq.items[0]), {at.column, last_depth(seq, 1, at.last), at.width}, ind);
        std::vector<Deep::Ref> cells{first.text};
        Place next{at.column + first.width, at.last, first.width};
        for (std::size_t i = 1; i < seq.items.size(); ++i) {
            const auto &pair_node = arena_.node(seq.items[i]);
            const auto last = last_depth(seq, i + 1, at.last);
            if (fits_next(pair_node.length, {next.column, last, next.width}) && atomic_pair(arena_, pair_node)) {
                cells.insert(cells.end(), {deep_.code(','), line_.pair(pair_node)});
                next.column += pair_node.length + 1;
                next.width += pair_node.length + 1;
            } else {
                const auto part = pair(pair_node, {at.column, last, 0}, ind);
                cells.insert(cells.end(), {deep_.code(','), deep_.code('\n'), ind, part.text});
                next = {at.column + part.width, at.last, part.width};
            }
        }
        return prepend(deep_, cells, seq.end != NONE ? deep_.ascii(",...") : Deep::NIL);
    }

    // pp_pair/8: the value under the key, indented by map_value_indent/1, when the pair does not fit.
    Part pair(const Node &pair_node, const Place &at, Deep::Ref ind) {
        if (fits(pair_node, at)) {
            return {line_.pair(pair_node), atomic_pair(arena_, pair_node) ? pair_node.length : bounds_.line_length};
        }
        const auto shift = value_indent(tag_);
        const auto value_ind = indent(deep_, shift, ind);
        const auto key = pp(pair_node.key, at, ind);
        const auto value = pp(pair_node.value, {at.column + shift, at.last, 0}, value_ind);
        return {deep_.list({key, deep_.ascii(" =>\n"), value_ind}, value), bounds_.line_length};
    }

    // rec_indent/5's result: the column, indentation, line break (S) and width after a name.
    struct RecordIndent {
        std::int64_t column;
        Deep::Ref ind;
        Deep::Ref newline;
        std::int64_t width;
    };

    RecordIndent record_indent(std::int64_t indent_by, const Place &at, Deep::Ref ind) {
        const bool newline = tag_ > 0 && indent_by > tag_;
        const auto shift = newline ? tag_ : indent_by;
        const auto next_ind = indent(deep_, shift, ind);
        return {at.column + shift, next_ind, newline ? deep_.cons(deep_.code('\n'), next_ind) : Deep::NIL,
                newline ? 0 : at.width};
    }

    // pp_record/9 and pp_fields_tail/9.
    Deep::Ref record(const Seq &seq, std::int64_t name_length, const Place &at, Deep::Ref ind) {
        if (seq.items.empty()) {
            return seq.end != NONE ? deep_.ascii("...") : Deep::NIL;
        }
        const auto start = record_indent(name_length + 1, at, ind);
        const auto first =
            field(arena_.node(seq.items[0]), {start.column, last_depth(seq, 1, at.last), start.width}, start.ind);
        std::vector<Deep::Ref> cells{start.newline, first.text};
        Place next{start.column + first.width, at.last, start.width + first.width};
        for (std::size_t i = 1; i < seq.items.size(); ++i) {
            const auto &field_node = arena_.node(seq.items[i]);
            const auto last = last_depth(seq, i + 1, at.last);
            if (fits_next(field_node.length, {next.column, last, next.width}) && atomic_field(arena_, field_node)) {
                cells.insert(cells.end(), {deep_.code(','), line_.field(field_node)});
                next.column += field_node.length + 1;
                next.width += field_node.length + 1;
            } else {
                const auto part = field(field_node, {start.column, last, 0}, start.ind);
                cells.insert(cells.end(), {deep_.code(','), deep_.code('\n'), start.ind, part.text});
                next = {start.column + part.width, at.last, part.width};
            }
        }
        return prepend(deep_, cells, seq.end != NONE ? deep_.ascii(",...") : Deep::NIL);
    }

    // pp_field/8: the value after "Name = ", or on the next line at the tag indentation.
    Part field(const Node &field_node, const Place &at, Deep::Ref ind) {
        if (fits(field_node, at)) {
            return {line_.field(field_node),
                    atomic_field(arena_, field_node) ? field_node.length : bounds_.line_length};
        }
        const auto next =
            record_indent(field_node.name_length, {at.column, at.last, at.width + field_node.name_length}, ind);
        const auto separator = deep_.ascii(next.newline != Deep::NIL ? " =" : " = ");
        const auto value = pp(field_node.value, {next.column, at.last, next.width}, next.ind);
        return {deep_.list({field_node.text, separator, next.newline}, value), bounds_.line_length};
    }

    // pp_binary/7: the bytes of io_lib:write_binary/3's [LT, LT, S, GT, GT] broken into lines of N characters.
    Deep::Ref binary(Deep::Ref text, const Place &at, Deep::Ref ind) {
        std::array<Deep::Ref, 5> parts{};
        auto rest = text;
        for (auto &part : parts) {
            part = deep_.head(rest);
            rest = deep_.tail(rest);
        }
        const auto column = at.column + 2;
        const auto n =
            std::max<std::int64_t>(8, std::min(bounds_.line_length - column, bounds_.most - 4 - at.width) - at.last);
        return deep_.list({parts[0], parts[1], binary_lines(parts[2], n, ind), parts[3], parts[4]});
    }

    // pp_binary/4.
    Deep::Ref binary_lines(Deep::Ref s, std::int64_t n0, Deep::Ref ind) {
        std::vector<Deep::Ref> cells;
        auto n = n0;
        while (deep_.kind(s) == Deep::Kind::cons && deep_.kind(deep_.tail(s)) == Deep::Kind::cons &&
               is_code(deep_, deep_.head(deep_.tail(s)), ',')) {
            const auto bs = deep_.head(s);
            const auto length = top_length(deep_, bs) + 1;
            if (n - length < 0) {
                cells.insert(cells.end(), {deep_.code('\n'), ind, bs, deep_.code(',')});
                n = n0 - length;
            } else {
                cells.insert(cells.end(), {bs, deep_.code(',')});
                n -= length;
            }
            s = deep_.tail(deep_.tail(s));
        }
        const auto broken = deep_.list({deep_.code('\n'), ind, s});
        if (top_length(deep_, s) == 3 && is_code(deep_, deep_.head(deep_.tail(s)), ':')) {
            const auto before = top_length(deep_, deep_.head(s));
            const auto after = top_length(deep_, deep_.head(deep_.tail(deep_.tail(s))));
            if (before + after + 1 > n) {
                return prepend(deep_, cells, broken);
            }
        }
        const auto size = static_cast<std::int64_t>(deep_.flat(s).size());
        return prepend(deep_, cells, size > n ? broken : s);
    }

    const Arena &arena_;
    Deep &deep_;
    const Bounds &bounds_;
    // TInd: the tag indentation chosen by tag_indent/4.
    std::int64_t tag_;
    OneLine line_;
};
} // namespace

Deep::Ref write(const Arena &arena, Index index) { return OneLine(arena).write(index); }

std::int64_t tag_indent(const Arena &arena, Index root, const Bounds &bounds, std::int64_t column) {
    for (const std::int64_t ind : {-1, 4}) {
        try {
            Indent(arena, bounds, ind).check(root, {column, 0, 0});
            return ind;
        } catch (const NoGood &) {
            continue; // Try the next indentation, as while_fail/3 does.
        }
    }
    return 1;
}

Deep::Ref layout(const Arena &arena, Index root, const Bounds &bounds, std::int64_t column) {
    auto &deep = arena.deep();
    return Broken(arena, bounds).pp(root, {column, 0, 0}, spaces(deep, column - 1));
}
} // namespace clause::runtime::builtins::io_lib::pretty
