#include "../text.hpp"
#include "pretty_format.hpp"
#include <algorithm>

// io_lib_pretty's pp_bin/11 (plan 21 step LIB4D): the layout of pp/8 appended to one UTF-8 binary, counting the
// newlines written and the position reached, which print_bin/2 reports as the size and column of the text.
namespace clause::runtime::builtins::io_lib::pretty {
namespace {
// map_value_indent/1.
std::int64_t value_indent(std::int64_t tag) { return tag > 0 ? tag : 4; }

// The bytes before a comma at offset 1, 2 or 3 of the rest (pp_binary_bin_ind/10's patterns), 0 for none.
std::size_t comma_after(const std::string &text, std::size_t rest) {
    for (std::size_t k = 1; k <= 3; ++k) {
        if (rest + k < text.size() && text[rest + k] == ',') {
            return k;
        }
    }
    return 0;
}

class BinaryBroken final {
  public:
    BinaryBroken(const Arena &arena, const Bounds &bounds) : arena_(arena), deep_(arena.deep()), bounds_(bounds) {}

    // pp_bin/11: appends the value laid out from `at` with `ind` spaces of indentation, moving the position.
    void pp(Index index, const Place &at, std::int64_t ind) {
        const auto &node = arena_.node(index);
        if (fits(node, at)) {
            append_written(index);
            position_ += node.length;
            return;
        }
        switch (node.shape) {
        case Shape::list:
            open("[", 1);
            return list(node.seq, 0, {at.column + 1, at.last, at.width + 1}, ind + 1, {'|', ']'});
        case Shape::tuple:
            open("{", 1);
            if (node.tagged) {
                return tag_tuple(node.seq, {at.column, at.last, at.width + 1}, ind);
            }
            return list(node.seq, 0, {at.column + 1, at.last, at.width + 1}, ind + 1, {',', '}'});
        case Shape::map:
            open("#{", 0);
            return map(node.seq, {at.column + 2, at.last, at.width + 2}, ind + 2);
        case Shape::record:
            append_text(node.text);
            open("{", node.name_length + 1);
            return record(node.seq, node.name_length, {at.column, at.last, at.width + node.name_length + 1}, ind);
        case Shape::bin:
            return binary(utf8(deep_.flat(node.text)), {at.column + 2, at.last, at.width}, ind + 2);
        default:
            append_text(node.text);
            position_ += node.length;
        }
    }

    // Reserve the flat layout's length; line breaks add only a little.
    void reserve(std::int64_t length) { out_.reserve(static_cast<std::size_t>(std::max<std::int64_t>(length, 0))); }

    std::string take() { return std::move(out_); }

    std::int64_t newlines() const { return newlines_; }

    std::int64_t position() const { return position_; }

  private:
    // The separator and closing character of a list or tuple.
    struct Brackets {
        char separator;
        char close;
    };

    // Where the continuation lines of a list start: their column and indentation.
    struct Lines {
        std::int64_t column;
        std::int64_t ind;
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

    bool atomic_pair(const Node &pair) const {
        return atomic(arena_.node(pair.key)) && atomic(arena_.node(pair.value));
    }

    bool atomic_field(const Node &field) const { return atomic(arena_.node(field.value)); }

    // An opening text and the position OTP moves by for it.
    void open(std::string_view text, std::int64_t moved) {
        out_ += text;
        position_ += moved;
    }

    // A closing text after the elements (",...}", "}", ...): the position moves by its length.
    void close(std::string_view text) {
        out_ += text;
        position_ += static_cast<std::int64_t>(text.size());
    }

    void append_text(Deep::Ref text) { out_ += utf8(deep_.flat(text)); }

    void append_written(Index index) { append_text(write(arena_, index)); }

    // A newline and `ind` spaces.
    void break_line(std::int64_t ind) {
        out_ += '\n';
        out_.append(static_cast<std::size_t>(ind), ' ');
        ++newlines_;
    }

    // pp_tag_tuple_bin/11.
    void tag_tuple(const Seq &seq, const Place &at, std::int64_t ind) {
        if (seq.items.empty()) {
            return close("...}");
        }
        const auto &tag = arena_.node(seq.items[0]);
        const auto tag_indent = tag.length + 2;
        const auto tag_column = at.column + tag_indent;
        append_text(tag.text);
        if (bounds_.tag > 0 && tag_indent > bounds_.tag) {
            position_ += tag.length;
            return tail(seq, 1, {tag_column, at.last, at.width + tag.length},
                        {at.column + bounds_.tag, ind + bounds_.tag}, {',', '}'});
        }
        open(",", tag.length + 1);
        list(seq, 1, {tag_column, at.last, at.width + tag.length + 1}, ind + tag_indent, {',', '}'});
    }

    // pp_list_bin/13.
    void list(const Seq &seq, std::size_t from, const Place &at, std::int64_t ind, Brackets brackets) {
        if (from >= seq.items.size()) {
            out_ += "...";
            out_ += brackets.close;
            position_ += 4;
            return;
        }
        const auto width = element(seq.items[from], {at.column, last_depth(seq, from + 1, at.last), at.width}, ind);
        tail(seq, from + 1, {at.column + width, at.last, at.width + width}, {at.column, ind}, brackets);
    }

    // pp_tail_bin/14 from element `from`.
    void tail(const Seq &seq, std::size_t from, Place at, const Lines &lines, Brackets brackets) {
        for (auto i = from; i < seq.items.size(); ++i) {
            const auto &node = arena_.node(seq.items[i]);
            const auto last = last_depth(seq, i + 1, at.last);
            if (fits_next(node.length, {at.column, last, at.width}) && atomic(node)) {
                out_ += ',';
                append_written(seq.items[i]);
                at.column += node.length + 1;
                at.width += node.length + 1;
                position_ += node.length + 1;
            } else {
                out_ += ',';
                break_line(lines.ind);
                position_ = lines.column;
                const auto width = element(seq.items[i], {lines.column, last, 0}, lines.ind);
                at = {lines.column + width, at.last, width};
            }
        }
        tail_end(seq, at, lines, brackets);
    }

    // What ends pp_tail_bin/14: the closing character after [], dots or an improper tail.
    void tail_end(const Seq &seq, const Place &at, const Lines &lines, Brackets brackets) {
        if (seq.end == NONE) {
            return close(std::string_view(&brackets.close, 1));
        }
        const auto &end = arena_.node(seq.end);
        out_ += brackets.separator;
        if (end.shape == Shape::dots) {
            out_ += "...";
            out_ += brackets.close;
            position_ += 5;
            return;
        }
        const auto length = end.length + 1;
        const auto last = at.last + 1;
        if (length < bounds_.line_length - at.column - last && length + at.width + last <= bounds_.most &&
            atomic(end)) {
            append_written(seq.end);
            out_ += brackets.close;
            position_ += 1 + end.length;
            return;
        }
        break_line(lines.ind);
        position_ = lines.column;
        pp(seq.end, {lines.column, last, 0}, lines.ind);
        close(std::string_view(&brackets.close, 1));
    }

    // pp_element_bin/11: the width the element takes on its line.
    std::int64_t element(Index index, const Place &at, std::int64_t ind) {
        const auto &node = arena_.node(index);
        if (fits(node, at) && atomic(node)) {
            append_written(index);
            position_ += node.length;
            return node.length;
        }
        pp(index, at, ind);
        return bounds_.line_length;
    }

    // pp_map_bin/11 (the second pair starts with W = PW, as in OTP).
    void map(const Seq &seq, const Place &at, std::int64_t ind) {
        if (seq.items.empty()) {
            return close(seq.end != NONE ? "...}" : "}");
        }
        const auto width = pair(arena_.node(seq.items[0]), {at.column, last_depth(seq, 1, at.last), at.width}, ind);
        pairs_tail(seq, at, {at.column + width, at.last, width}, ind);
    }

    // pp_pairs_tail_bin/12: pairs after the first, continuation lines at the map's column.
    void pairs_tail(const Seq &seq, const Place &at, Place next, std::int64_t ind) {
        for (std::size_t i = 1; i < seq.items.size(); ++i) {
            const auto &pair_node = arena_.node(seq.items[i]);
            const auto last = last_depth(seq, i + 1, at.last);
            if (fits_next(pair_node.length, {next.column, last, next.width}) && atomic_pair(pair_node)) {
                out_ += ',';
                append_written(seq.items[i]);
                next.column += pair_node.length + 1;
                next.width += pair_node.length + 1;
                position_ += pair_node.length + 1;
            } else {
                out_ += ',';
                break_line(ind);
                position_ = at.column;
                const auto width = pair(pair_node, {at.column, last, 0}, ind);
                next = {at.column + width, at.last, width};
            }
        }
        close(seq.end != NONE ? ",...}" : "}");
    }

    // pp_pair_bin/11: the width the pair takes, the line length when the value goes under the key.
    std::int64_t pair(const Node &pair_node, const Place &at, std::int64_t ind) {
        if (fits(pair_node, at)) {
            append_text(one_line_pair(pair_node));
            position_ += pair_node.length;
            return atomic_pair(pair_node) ? pair_node.length : bounds_.line_length;
        }
        const auto shift = value_indent(bounds_.tag);
        pp(pair_node.key, at, ind);
        out_ += " =>";
        break_line(ind + shift);
        position_ = at.column + shift;
        pp(pair_node.value, {at.column + shift, at.last, 0}, ind + shift);
        return bounds_.line_length;
    }

    // write_pair/1 of a pair node.
    Deep::Ref one_line_pair(const Node &pair_node) {
        return deep_.list({write(arena_, pair_node.key), deep_.ascii(" => "), write(arena_, pair_node.value)});
    }

    // write_field/1 of a field node.
    Deep::Ref one_line_field(const Node &field) {
        return deep_.cons(field.text, deep_.cons(deep_.ascii(" = "), write(arena_, field.value)));
    }

    // rec_indent/5 over spaces: the column, indentation and width after a name, and whether a line break precedes.
    struct RecordIndent {
        std::int64_t column;
        std::int64_t ind;
        bool newline;
        std::int64_t width;
    };

    RecordIndent record_indent(std::int64_t indent_by, const Place &at, std::int64_t ind) const {
        const bool newline = bounds_.tag > 0 && indent_by > bounds_.tag;
        const auto shift = newline ? bounds_.tag : indent_by;
        return {at.column + shift, ind + shift, newline, newline ? 0 : at.width};
    }

    // ?IND(S): the line break rec_indent/5 chose, if any.
    void append_break(const RecordIndent &at) {
        if (at.newline) {
            out_ += '\n';
            out_.append(static_cast<std::size_t>(at.ind), ' ');
        }
    }

    // pp_record_bin/12: a new line before the fields counts when the name is wider than the tag indentation.
    void record(const Seq &seq, std::int64_t name_length, const Place &at, std::int64_t ind) {
        if (seq.items.empty()) {
            return close(seq.end != NONE ? "...}" : "}");
        }
        const auto start = record_indent(name_length + 1, at, ind);
        if (start.width == 0) {
            position_ = start.column;
            ++newlines_;
        }
        append_break(start);
        const auto width =
            field(arena_.node(seq.items[0]), {start.column, last_depth(seq, 1, at.last), start.width}, start.ind);
        fields_tail(seq, {start.column, at.last, 0}, {start.column + width, at.last, start.width + width}, start.ind);
    }

    // pp_fields_tail_bin/12: fields after the first, continuation lines at the first field's column.
    void fields_tail(const Seq &seq, const Place &at, Place next, std::int64_t ind) {
        for (std::size_t i = 1; i < seq.items.size(); ++i) {
            const auto &field_node = arena_.node(seq.items[i]);
            const auto last = last_depth(seq, i + 1, at.last);
            if (fits_next(field_node.length, {next.column, last, next.width}) && atomic_field(field_node)) {
                out_ += ',';
                append_text(one_line_field(field_node));
                next.column += field_node.length + 1;
                next.width += field_node.length + 1;
                position_ += field_node.length + 1;
            } else {
                out_ += ',';
                break_line(ind);
                position_ = at.column;
                const auto width = field(field_node, {at.column, last, 0}, ind);
                next = {at.column + width, at.last, width};
            }
        }
        close(seq.end != NONE ? ",...}" : "}");
    }

    // pp_field_bin/11: the width the field takes, the line length when its value is laid out.
    std::int64_t field(const Node &field_node, const Place &at, std::int64_t ind) {
        if (fits(field_node, at)) {
            append_text(one_line_field(field_node));
            position_ += field_node.length;
            return atomic_field(field_node) ? field_node.length : bounds_.line_length;
        }
        const auto next =
            record_indent(field_node.name_length, {at.column, at.last, at.width + field_node.name_length}, ind);
        append_text(field_node.text);
        out_ += next.width == 0 ? " =" : " = ";
        append_break(next);
        if (next.width == 0) {
            ++newlines_;
            position_ = next.column;
        }
        pp(field_node.value, {next.column, at.last, next.width}, next.ind);
        return bounds_.line_length;
    }

    // Where pp_binary_bin_ind/10 stopped: the part not yet written (`start` and `taken` bytes), the rest scanned
    // from, what is left of N and whether a break after a one-digit byte reset the position.
    struct BinaryCut {
        std::size_t start = 0;
        std::size_t taken = 2;
        std::size_t rest = 2;
        std::int64_t n = 0;
        // N0: the characters a line holds.
        std::int64_t n0 = 0;
        bool reset = false;
    };

    // pp_binary_bin_ind/10's comma clauses: bytes with their commas while N lasts, then a line break.
    BinaryCut cut_binary(const std::string &original, BinaryCut cut, std::int64_t ind) {
        for (auto comma = comma_after(original, cut.rest); comma != 0; comma = comma_after(original, cut.rest)) {
            const auto step = static_cast<std::int64_t>(comma + 1);
            if (cut.n - step >= 0) {
                cut.taken += comma + 1;
                cut.n -= step;
            } else {
                out_.append(original, cut.start, cut.taken);
                break_line(ind);
                cut.start += cut.taken;
                cut.taken = comma + 1;
                cut.n = cut.n0 - step;
                cut.reset = cut.reset || comma == 1;
            }
            cut.rest += comma + 1;
        }
        return cut;
    }

    // pp_binary_bin/10: write_binary's text broken after commas once N is used up.
    void binary(const std::string &original, const Place &at, std::int64_t ind) {
        const auto n0 =
            std::max<std::int64_t>(8, std::min(bounds_.line_length - at.column, bounds_.most - 4 - at.width) - at.last);
        const auto cut = cut_binary(original, {.n = n0, .n0 = n0}, ind);
        const auto column = ind + 1;
        if (cut.reset || position_ == 0) {
            position_ = column;
        }
        if (static_cast<std::int64_t>(original.size() - cut.rest) - 2 <= cut.n) {
            out_.append(original, cut.start);
            position_ += static_cast<std::int64_t>(original.size() - cut.start);
            return;
        }
        out_.append(original, cut.start, cut.taken);
        break_line(ind);
        out_.append(original, cut.start + cut.taken);
        position_ = column + static_cast<std::int64_t>(original.size() - (cut.start + cut.taken));
    }

    const Arena &arena_;
    Deep &deep_;
    // The line length, M and the tag indentation.
    const Bounds &bounds_;
    std::string out_;
    std::int64_t newlines_ = 0;
    // Pos: the position print_bin/2 reports, as pp_bin/11 threads it.
    std::int64_t position_ = 0;
};
} // namespace

BinaryLayout layout_bin(const Arena &arena, Index root, const Bounds &bounds, std::int64_t column) {
    BinaryBroken broken(arena, bounds);
    broken.reserve(arena.node(root).length);
    broken.pp(root, {column, 0, 0}, column - 1);
    return {.bytes = broken.take(), .newlines = broken.newlines(), .position = broken.position()};
}
} // namespace clause::runtime::builtins::io_lib::pretty
