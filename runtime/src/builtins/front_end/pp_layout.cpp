#include "../io_lib/deep.hpp"
#include "../io_lib/format.hpp"
#include "../io_lib/write.hpp"
#include "../string/string.hpp"
#include "../text.hpp"
#include "../unicode/code_points.hpp"
#include "pp.hpp"
#include <algorithm>
#include <array>
#include <span>
#include <utility>

// erl_pp's second pass (plan 21 step LIB34C): items laid out as {Chars, Size}, Size the width on one line or the
// width of the last line once the characters span lines; a negative indentation lays everything on one line.
namespace clause::runtime::builtins::front_end::pp {
namespace {
constexpr std::int64_t MIN_PIECE = 5;

// One laid part of a group: its head and the tail stepped onto the next lines (empty when none).
struct Part {
    Laid head;
    Laid tail;
};

Size flat(std::int64_t width) { return {.width = width, .line = false}; }

Size at_line(const Size &size, std::int64_t indent) {
    return size.line ? size : Size{.width = size.width + indent, .line = true};
}

std::int64_t increased(std::int64_t indent, std::int64_t by) { return indent < 0 ? indent : indent + by; }

// The column after laid characters.
std::int64_t advanced(const Laid &laid, std::int64_t indent) {
    return laid.size.line ? laid.size.width : increased(indent, laid.size.width);
}

// The width of sizes one after the other: the last line's width once one of them spans lines.
Size line_size(std::span<const Size> sizes) {
    Size total;
    for (const auto &size : sizes) {
        if (size.line) {
            total = size;
        } else {
            total.width += size.width;
        }
    }
    return total;
}

std::u32string newline(std::int64_t indent) {
    return U"\n" + std::u32string(static_cast<std::size_t>(std::max<std::int64_t>(indent, 0)), U' ');
}

// separate/2: the laid characters with `separator` between them.
std::u32string separate(std::span<const Laid> laid, std::u32string_view separator) {
    std::u32string out;
    for (std::size_t index = 0; index < laid.size(); ++index) {
        if (index > 0) {
            out += separator;
        }
        out += laid[index].chars;
    }
    return out;
}

// nonzero_unzip/1: the laid pieces that are not empty.
std::vector<Laid> nonzero(std::span<const Laid> laid) {
    std::vector<Laid> out;
    std::ranges::copy_if(laid, std::back_inserter(out), [](const Laid &piece) { return !piece.size.empty(); });
    return out;
}

// The non-empty heads and tails of laid parts, in order.
std::vector<Laid> pieces(std::span<const Part> parts) {
    std::vector<Laid> out;
    out.reserve(parts.size() * 2);
    for (const auto &part : parts) {
        out.push_back(part.head);
        out.push_back(part.tail);
    }
    return nonzero(out);
}

std::int64_t width_of(std::span<const Laid> laid) {
    std::int64_t width = 0;
    for (const auto &piece : laid) {
        width += piece.size.width;
    }
    return width;
}

bool spans_lines(std::span<const Laid> laid) {
    return std::ranges::any_of(laid, [](const Laid &piece) { return piece.size.line; });
}

// The grapheme clusters of a string (unicode_util:gc/1 repeated), each as its code points.
std::vector<std::u32string> clusters_of(ProcessContext &context, const Term &string) {
    std::vector<std::u32string> out;
    for (auto rest = unicode::gc_term(context, string); rest.is_cons();) {
        const auto next = unicode::split(rest);
        out.push_back(string::cluster_codes(next.item));
        rest = unicode::gc_term(context, next.rest);
    }
    return out;
}

// The place a joined line continues from: the group's indentation and whether its parts are spaced.
struct JoinAt {
    std::int64_t indent;
    bool spaced;
};

// join_lines/9's state: the column, the width so far and the characters.
struct Joined {
    std::int64_t column = 0;
    Size width;
    std::u32string chars;
};

// The layout of one print.
class Layout final {
  public:
    Layout(Items &items, const Writers &writers, std::span<const Term> answers)
        : items_(items), writers_(writers), answers_(answers), space_(items.leaf(U" ")) {}

    Laid lay(Index index, std::int64_t indent) {
        const auto &item = items_.items_[static_cast<std::size_t>(index)];
        switch (item.kind) {
        case Kind::character:
            return {.chars = std::u32string(1, item.code), .size = flat(1)};
        case Kind::leaf:
        case Kind::word:
            return {.chars = item.text, .size = flat(item.size)};
        case Kind::reserved:
            return lay(item.parts[0], indent);
        case Kind::concat:
            return lay_concat(item.parts, indent);
        case Kind::list:
        case Kind::first:
        case Kind::seq:
            return lay_seq(item, indent);
        default:
            return lay_other(item, indent);
        }
    }

  private:
    Laid lay_other(const Item &item, std::int64_t indent) {
        switch (item.kind) {
        case Kind::prefer_nl:
            return indent < 0 ? lay_seq(item, indent) : lay_prefer_nl(item.parts, item.separator, indent);
        case Kind::force_nl:
            return indent < 0 ? lay(item.parts[1], indent) : lay_prefer_nl(item.parts, std::nullopt, indent);
        case Kind::hook:
            return lay_hook(item, indent);
        case Kind::string:
            return lay_string(item.term, indent);
        default:
            return leaf_of(written(item.kind, item.term));
        }
    }

    Laid lay_concat(std::span<const Index> parts, std::int64_t indent) {
        if (parts.empty()) {
            return {};
        }
        return run_on(parts.subspan(1), lay(parts.front(), indent), indent);
    }

    // run_on/4: items one after the other, each starting where the previous one ended.
    Laid run_on(std::span<const Index> items, const Laid &first, std::int64_t indent) {
        std::vector<Laid> laid{first};
        auto column = advanced(first, indent);
        for (const auto item : items) {
            laid.push_back(lay(item, column));
            column = advanced(laid.back(), column);
        }
        Laid out;
        std::vector<Size> sizes;
        for (const auto &piece : nonzero(laid)) {
            out.chars += piece.chars;
            sizes.push_back(piece.size);
        }
        out.size = line_size(sizes);
        return out;
    }

    // lay_seq/6: on one line when Before, the parts and their separators fit; else the parts stepped and separated
    // by newlines where needed.
    Laid lay_seq(const Item &item, std::int64_t indent) {
        const auto before = item.before ? lay(*item.before, indent) : Laid{};
        const auto inner = advanced(before, indent);
        const auto parts = lay_parts(item, inner);
        const auto laid = pieces(parts);
        const auto prefix = before.size.empty() ? std::u32string{} : before.chars;
        const auto separators = item.spaced ? std::max<std::int64_t>(0, std::ssize(laid) - 1) : 0;
        if (const auto width = fits(indent, before, laid, separators)) {
            return {.chars = prefix + separate(laid, separators > 0 ? U" " : U""), .size = flat(*width)};
        }
        const auto broken = break_lines(stepped(parts, inner), item.parts, {.indent = inner, .spaced = separators > 0});
        return {.chars = prefix + broken.chars, .size = at_line(broken.size, indent)};
    }

    // fits/4: the one-line width when no piece spans lines and the line has room.
    std::optional<std::int64_t> fits(std::int64_t indent, const Laid &before, std::span<const Laid> laid,
                                     std::int64_t separators) const {
        if (spans_lines(laid) || before.size.line) {
            return std::nullopt;
        }
        const auto width = width_of(laid) + before.size.width + separators;
        return increased(indent, width) <= writers_.width ? std::optional{width} : std::nullopt;
    }

    // lay_parts/5 and follow/5: each part followed by the separator, the last separator before the last part and
    // After after it.
    std::vector<Part> lay_parts(const Item &item, std::int64_t indent) {
        const auto &parts = item.parts;
        if (parts.empty()) {
            return {Part{.head = item.after ? lay(*item.after, indent) : Laid{}, .tail = {}}};
        }
        std::vector<Part> laid;
        laid.reserve(parts.size());
        for (std::size_t at = 0; at < parts.size(); ++at) {
            laid.push_back(lay_part(parts[at], follower_of(item, at), indent));
        }
        return laid;
    }

    static std::optional<Index> follower_of(const Item &item, std::size_t at) {
        if (at + 1 == item.parts.size()) {
            return item.after;
        }
        return at + 2 == item.parts.size() ? item.last_separator : item.separator;
    }

    // lay_part/4.
    Part lay_part(Index index, std::optional<Index> follower, std::int64_t indent) {
        const auto &item = items_.items_[static_cast<std::size_t>(index)];
        if (item.kind != Kind::step && item.kind != Kind::cstep) {
            return {.head = lay_followed(index, follower, indent), .tail = {}};
        }
        auto head = lay(item.parts[0], indent);
        if (item.kind == Kind::cstep && !head.size.line && head.size.width < writers_.indent) {
            std::vector<Index> rest{space_, item.parts[1]};
            if (follower) {
                rest.push_back(*follower);
            }
            return {.head = run_on(rest, head, indent), .tail = {}};
        }
        return {.head = std::move(head),
                .tail = lay_followed(item.parts[1], follower, increased(indent, writers_.indent))};
    }

    // lay([Item, Separator]).
    Laid lay_followed(Index index, std::optional<Index> follower, std::int64_t indent) {
        const auto first = lay(index, indent);
        if (!follower) {
            return run_on({}, first, indent);
        }
        const std::array rest{*follower};
        return run_on(rest, first, indent);
    }

    // stepped/3: each laid part as one piece, a tail on the next line one indentation in.
    std::vector<Laid> stepped(std::span<const Part> parts, std::int64_t indent) const {
        std::vector<Laid> out;
        out.reserve(parts.size());
        for (const auto &part : parts) {
            if (part.tail.size.empty()) {
                out.push_back(part.head.size.empty() ? Laid{} : part.head);
                continue;
            }
            const std::array sizes{part.head.size, part.tail.size};
            out.push_back({.chars = part.head.chars + newline(indent + writers_.indent) + part.tail.chars,
                           .size = line_size(sizes)});
        }
        return out;
    }

    bool is_atomic(Index index) const {
        const auto kind = items_.items_[static_cast<std::size_t>(index)].kind;
        return kind == Kind::atom || kind == Kind::singleton || kind == Kind::word || kind == Kind::leaf;
    }

    // break_lines/5: an atomic part joins the previous line while the line has room, any other part starts a new
    // line.
    Laid break_lines(std::span<const Laid> laid, std::span<const Index> parts, JoinAt at) const {
        if (parts.empty()) {
            return laid.front();
        }
        const auto line = newline(at.indent);
        Joined joined{.column = is_atomic(parts.front()) ? at.indent + laid.front().size.width : writers_.width + 1,
                      .width = laid.front().size,
                      .chars = laid.front().chars};
        for (std::size_t index = 1; index < laid.size() && index < parts.size(); ++index) {
            join_line(joined, laid[index], is_atomic(parts[index]), at, line);
        }
        return {.chars = std::move(joined.chars), .size = joined.width};
    }

    // One step of join_lines/9.
    void join_line(Joined &joined, const Laid &piece, bool atomic, JoinAt at, std::u32string_view line) const {
        if (!atomic || piece.size.line) {
            joined.chars += line;
            joined.chars += piece.chars;
            joined.width = flat(0);
            joined.column = writers_.width + 1;
            return;
        }
        const auto next = joined.column + piece.size.width + 1;
        if (next <= writers_.width) {
            if (at.spaced) {
                joined.chars += U' ';
            }
            joined.chars += piece.chars;
            joined.width.width += piece.size.width + 1;
            joined.column = next;
            return;
        }
        joined.chars += line;
        joined.chars += piece.chars;
        joined.width = flat(piece.size.width + 1);
        joined.column = at.indent + piece.size.width;
    }

    // A prefer_nl group at a column: its parts one per line.
    Laid lay_prefer_nl(std::span<const Index> parts, std::optional<Index> separator, std::int64_t indent) {
        Item group;
        group.parts.assign(parts.begin(), parts.end());
        group.separator = separator;
        group.last_separator = separator;
        const auto laid = lay_parts(group, indent);
        const auto sizes = pieces(laid);
        if (sizes.empty()) {
            return {};
        }
        return {.chars = separate(nonzero(stepped(laid, indent)), newline(indent)),
                .size = at_line(sizes.back().size, indent)};
    }

    // hooked/2: the text of the next hook answer and the column after it.
    Laid lay_hook(const Item &item, std::int64_t indent) {
        if (next_hook_ >= answers_.size()) {
            throw HookNeeded{.expr = item.term, .column = indent, .precedence = item.precedence};
        }
        auto &context = items_.build_.context();
        const auto &answer = answers_[next_hook_++];
        auto text = chardata_text(context, answer);
        if (indent < 0) {
            const auto length = text_length(context, text);
            return {.chars = std::move(text), .size = flat(length)};
        }
        const auto column = io_lib::term_indentation(context, answer, indent);
        const auto line = text.contains(U'\n');
        return {.chars = std::move(text), .size = {.width = column, .line = line}};
    }

    Laid leaf_of(std::u32string text) const {
        const auto length = text_length(items_.build_.context(), text);
        return {.chars = std::move(text), .size = flat(length)};
    }

    io_lib::Escape escape() const {
        return writers_.unicode ? io_lib::Escape::unicode : io_lib::Escape::unicode_as_latin1;
    }

    // The text of an atom, singleton atom type, char or value as the writers write it.
    std::u32string written(Kind kind, const Term &term) const {
        if (kind == Kind::char_value) {
            return char_text(term);
        }
        if (kind == Kind::singleton && writers_.quote_singleton) {
            return quoted(code_points(term.atom_spelling_unchecked()), U'\'');
        }
        return value_text(term);
    }

    // io_lib:write_char/1 or write_char_as_latin1/1.
    std::u32string char_text(const Term &code) const {
        if (writers_.unicode && string::is_char(code, ' ')) {
            return U"$\\s";
        }
        io_lib::Deep deep;
        return deep.flat(deep.cons(deep.code('$'), io_lib::string_char(deep, code, escape(), -1, io_lib::Deep::NIL)));
    }

    // io_lib:write_string/2 or write_string_as_latin1/2 of code points.
    std::u32string quoted(std::u32string_view codes, char32_t quote) const {
        const auto &build = items_.build_;
        std::vector<Term> list;
        list.reserve(codes.size());
        for (const auto code : codes) {
            list.push_back(build.integer(code));
        }
        io_lib::Deep deep;
        return deep.flat(io_lib::string_text(deep, build.list(list), escape(), build.integer(quote)));
    }

    std::u32string string_text(const Term &string) const {
        io_lib::Deep deep;
        return deep.flat(io_lib::string_text(deep, string, escape(), items_.build_.integer('"')));
    }

    // io_lib:format("~p" or "~tp", [Term]).
    std::u32string value_text(const Term &term) const {
        auto &context = items_.build_.context();
        const auto &build = items_.build_;
        std::vector<Term> format{build.integer('~')};
        if (writers_.unicode) {
            format.push_back(build.integer('t'));
        }
        format.push_back(build.integer('p'));
        const std::array args{term};
        io_lib::Deep deep;
        const auto elements = io_lib::scan(context, build.list(format), build.list(args));
        return deep.flat(io_lib::build(context, deep, elements, build.integer(-1)));
    }

    // string_item/3: a string cut in pieces that fit what is left of the line, laid out as a list.
    Laid lay_string(const Term &string, std::int64_t indent) {
        if (indent < 0 || string.is_nil()) {
            return leaf_of(string_text(string));
        }
        Item group;
        group.kind = Kind::list;
        group.spaced = true;
        group.parts = string_pieces(string, std::max(writers_.width - indent, MIN_PIECE));
        return lay_seq(group, indent);
    }

    // string_pieces/4: the longest prefix of at most `width` clusters whose written text fits, down to MIN_PIECE.
    std::vector<Index> string_pieces(const Term &string, std::int64_t width) {
        auto &context = items_.build_.context();
        const auto clusters = clusters_of(context, string);
        std::vector<Index> out;
        for (std::size_t from = 0; from < clusters.size();) {
            auto count = static_cast<std::size_t>(width);
            auto taken = std::min(count, clusters.size() - from);
            auto written = quoted(joined(clusters, from, taken), U'"');
            while (text_length(context, written) > width && std::cmp_greater(count, MIN_PIECE)) {
                --count;
                taken = std::min(count, clusters.size() - from);
                written = quoted(joined(clusters, from, taken), U'"');
            }
            out.push_back(items_.leaf(written));
            from += taken;
        }
        return out;
    }

    static std::u32string joined(std::span<const std::u32string> clusters, std::size_t from, std::size_t count) {
        std::u32string out;
        for (const auto &cluster : clusters.subspan(from, count)) {
            out += cluster;
        }
        return out;
    }

    Items &items_;
    const Writers &writers_;
    std::span<const Term> answers_;
    // A space leaf, for cstep heads that leave the tail on their line.
    Index space_;
    // The hook answers used so far.
    std::size_t next_hook_ = 0;
};
} // namespace

std::u32string layout(Items &items, Index item, std::int64_t indent, const Writers &writers,
                      std::span<const Term> answers) {
    return Layout(items, writers, answers).lay(item, indent).chars;
}
} // namespace clause::runtime::builtins::front_end::pp
