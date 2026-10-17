#include "../../terms/records.hpp"
#include "../string/string.hpp"
#include "../text.hpp"
#include "pretty_format.hpp"
#include <algorithm>
#include <clause/runtime/terms.hpp>

// io_lib_pretty's print_length/7 (plan 21 step LIB4D): a term's intermediate format with the characters it takes on
// one line, cut at the depth and chars_limit by dots that remember how to print more (More funs, rerun by expand/3).
namespace clause::runtime::builtins::io_lib::pretty {
namespace {
using string::Terms;

// One step of a printable check over a list: every element a printable Latin-1 character (io_lib's
// printable_latin1_list/1 rules, the printable range being latin1).
bool printable_latin1(const Term &element) {
    const auto c = string::small_of(element);
    if (!c) {
        return false;
    }
    static constexpr std::u32string_view CONTROLS = U"\n\r\t\v\b\f\x1B";
    return (*c >= ' ' && *c <= '~') || (*c >= 0xA0 && *c <= 0xFF) ||
           (*c >= 0 && CONTROLS.contains(static_cast<char32_t>(*c)));
}

// io_lib:printable_list/1 with the latin1 printable range: a flat proper list of printable characters.
bool printable_list(Term list) {
    for (; list.is_cons(); list = list.tail_unchecked()) {
        if (!printable_latin1(list.head_unchecked())) {
            return false;
        }
    }
    return list.is_nil();
}

// printable_char/2 with the latin1 range.
bool printable_byte(std::uint32_t c) {
    static constexpr std::u32string_view CONTROLS = U"\n\r\t\v\b\f\x1B";
    return (c >= ' ' && c <= '~') || (c >= 0xA0 && c <= 0xFF) || CONTROLS.contains(static_cast<char32_t>(c));
}

// The local printable_latin1_list/2's answer: all, 0 (the first N are printable and more follow) or the N left at a
// character that is not printable.
constexpr std::int64_t ALL = -1;

std::int64_t printable_count(Term list, std::int64_t n) {
    for (;; list = list.tail_unchecked(), --n) {
        if (list.is_cons() && n == 0) {
            return 0;
        }
        if (!list.is_cons() || !printable_latin1(list.head_unchecked())) {
            return list.is_nil() ? ALL : n;
        }
    }
}

// The first `n` elements of a list as a new list (lists:split/2's first part).
Term list_prefix(ProcessContext &context, Term list, std::int64_t n) {
    std::vector<Term> items;
    for (; n > 0; --n, list = need(list.tail())) {
        items.push_back(need(list.head()));
    }
    return Terms(context).list(items);
}

// lists:flatlength/1.
std::int64_t flat_length(const Term &list) {
    std::int64_t count = 0;
    for (Term rest = list; rest.is_cons(); rest = rest.tail_unchecked()) {
        const auto head = rest.head_unchecked();
        count += head.is_list() ? flat_length(head) : 1;
    }
    return count;
}

// is_flat/2 and the rest after those elements: the first `n` elements are integers.
std::optional<Term> flat_rest(Term list, std::int64_t n) {
    for (; n > 0; --n, list = list.tail_unchecked()) {
        if (!list.is_cons() || !list.head_unchecked().is_integer()) {
            return std::nullopt;
        }
    }
    return list;
}

// string:is_empty/1: nested empty lists and empty binaries.
bool empty_chardata(const Term &data) {
    if (data.is_binary()) {
        return string::binary_bytes(data).empty();
    }
    for (Term rest = data; rest.is_list(); rest = rest.tail_unchecked()) {
        if (rest.is_nil()) {
            return true;
        }
        if (!empty_chardata(rest.head_unchecked())) {
            return false;
        }
    }
    return false;
}

// The text of an empty list, tuple, map or bitstring; empty for any other term.
std::string_view empty_text(const Term &term) {
    if (term.is_nil()) {
        return "[]";
    }
    if (term.is_tuple()) {
        return term.tuple_size_unchecked() == 0 ? "{}" : "";
    }
    if (term.is_map()) {
        return term.map_size_unchecked() == 0 ? "#{}" : "";
    }
    if (term.is_bitstring()) {
        return term.bit_size_unchecked() == 0 ? "<<>>" : "";
    }
    return "";
}

// printable_list/4's verdict: not printable, the whole list, or a prefix to write before "...".
struct StringChoice {
    bool printable = false;
    std::optional<Term> prefix{};
};

// The latin1 clause with a chars_limit: printable up to T - 2 characters.
StringChoice latin1_choice(ProcessContext &context, const Term &list, std::int64_t limit) {
    const auto n = tsub(limit, 2);
    const auto count = printable_count(list, n);
    if (count > 0) {
        return {};
    }
    return count == 0 ? StringChoice{.printable = true, .prefix = list_prefix(context, list, n)}
                      : StringChoice{.printable = true};
}

// The unicode clause with a chars_limit: string:slice/3 to T - 2 clusters, flat, then printable.
StringChoice unicode_choice(ProcessContext &context, const Term &list, std::int64_t limit) {
    Term sliced;
    try {
        sliced = string::prefix_of(context, list, tsub(limit, 2));
    } catch (const BuiltinFailure &) {
        return {};
    }
    const auto rest = sliced.is_nil() ? std::nullopt : flat_rest(list, flat_length(sliced));
    if (!rest) {
        return {};
    }
    if (empty_chardata(*rest)) {
        return {.printable = printable_list(list)};
    }
    return printable_list(sliced) ? StringChoice{.printable = true, .prefix = sliced} : StringChoice{};
}

// printable_unicode_bin/3 with the latin1 range: the count left and where scanning stopped, or none (not_utf8).
struct UnicodeScan {
    std::int64_t left = 0;
    std::size_t at = 0;
};

std::optional<UnicodeScan> printable_unicode_bin(std::span<const std::byte> bytes, std::int64_t len) {
    std::size_t at = 0;
    for (auto left = len;; --left) {
        if (at == bytes.size()) {
            return UnicodeScan{.left = left, .at = at};
        }
        auto next = at;
        const auto c = next_utf8(bytes, next);
        if (!c) {
            return std::nullopt;
        }
        if (left <= 0 || !printable_byte(*c)) {
            return UnicodeScan{.left = left, .at = at};
        }
        at = next;
    }
}

// printable_latin1_bin/2: ALL, 0 or the count left at a byte that is not printable.
std::int64_t printable_latin1_bin(std::span<const std::byte> bytes, std::int64_t n) {
    for (std::size_t at = 0;; ++at, --n) {
        if (at == bytes.size()) {
            return ALL;
        }
        if (n == 0) {
            return 0;
        }
        if (!printable_byte(std::to_integer<std::uint32_t>(bytes[at]))) {
            return n;
        }
    }
}

// The answer of printable_bin0/5: not printable, or the bytes to write, whether they are Latin-1 and whether they are
// a prefix of the binary.
struct Printable {
    bool latin1 = true;
    bool prefix = false;
    std::size_t bytes = 0;
};

// The bytes and their depth and length counts that printable_bin/5 decides on.
struct BinaryScan {
    std::span<const std::byte> bytes;
    std::int64_t len = 0;
    std::int64_t depth = 0;
};

// printable_bin/5's latin1 clause.
std::optional<Printable> latin1_printable(const BinaryScan &in) {
    const auto size = static_cast<std::int64_t>(in.bytes.size());
    const auto left = printable_latin1_bin(in.bytes, in.len);
    if (left == ALL) {
        return Printable{.prefix = in.len != size, .bytes = static_cast<std::size_t>(in.len)};
    }
    if (in.depth > 0 && in.len - left >= in.depth) {
        return Printable{.prefix = true, .bytes = static_cast<std::size_t>(in.len - left)};
    }
    return std::nullopt;
}

// printable_bin/5's unicode clause after a UTF-8 scan: the whole binary (Latin-1 when it is ASCII) or a prefix.
std::optional<Printable> unicode_printable(const BinaryScan &in, const UnicodeScan &scan) {
    const auto taken = in.len - scan.left;
    if (scan.at == in.bytes.size()) {
        return Printable{.latin1 = taken == static_cast<std::int64_t>(in.bytes.size()), .bytes = in.bytes.size()};
    }
    if (in.depth > 0 && taken >= in.depth) {
        return Printable{.latin1 = static_cast<std::int64_t>(scan.at) == taken, .prefix = true, .bytes = scan.at};
    }
    return std::nullopt;
}

// printable_bin/5: bytes that are not UTF-8 are read as Latin-1.
std::optional<Printable> printable_bin(const BinaryScan &in, bool latin1) {
    if (in.len == 0) {
        return std::nullopt;
    }
    if (!latin1) {
        if (const auto scan = printable_unicode_bin(in.bytes, in.len)) {
            return unicode_printable(in, *scan);
        }
    }
    return latin1_printable(in);
}

// printable_bin0/5's length: four characters per level of depth, within chars_limit.
std::int64_t printable_length(std::int64_t size, const Cut &cut) {
    const auto [depth, limit] = cut;
    if (depth >= 0) {
        const auto chars = std::min(4 * depth, size);
        return limit >= 0 ? std::min(limit, chars) : chars;
    }
    return limit < 0 ? size : limit;
}

// The number of elements of a deep list's top level (length/1).
std::int64_t top_length(const Deep &deep, Deep::Ref ref) {
    std::int64_t count = 0;
    for (; deep.kind(ref) == Deep::Kind::cons; ref = deep.tail(ref)) {
        ++count;
    }
    return count;
}

// list_length/3: the elements' lengths plus separators, and their dots.
std::pair<std::int64_t, std::int64_t> list_length(const Arena &arena, const Seq &seq, std::int64_t open) {
    std::int64_t length = open;
    std::int64_t dots = 0;
    for (std::size_t i = 0; i < seq.items.size(); ++i) {
        length += arena.node(seq.items[i]).length + (i > 0 ? 1 : 0);
        dots += arena.node(seq.items[i]).dots;
    }
    if (seq.end != NONE) {
        length += arena.node(seq.end).length + (seq.items.empty() ? 0 : 1);
        dots += arena.node(seq.end).dots;
    }
    return {length, dots};
}
} // namespace

std::int64_t depth_minus(std::int64_t depth, std::int64_t n) {
    if (depth == BAD_DEPTH) {
        throw BuiltinFailure{.reason = abi::v1::ErrorReason::badarith};
    }
    return depth - n;
}

Index Arena::add(Node node) {
    nodes_.push_back(std::move(node));
    return static_cast<Index>(nodes_.size() - 1);
}

Index Arena::leaf(Deep::Ref text, std::int64_t length) {
    Node node;
    node.text = text;
    node.length = length;
    return add(std::move(node));
}

Index Arena::cut(Deep::Ref text, std::int64_t length, More more) {
    Node node;
    node.text = text;
    node.length = length;
    node.dots = 3;
    node.more = std::move(more);
    return add(std::move(node));
}

Index Arena::dots(More more) {
    Node node;
    node.shape = Shape::dots;
    node.length = 3;
    node.dots = 3;
    node.more = std::move(more);
    return add(std::move(node));
}

Index Arena::container(Node node, std::int64_t open) {
    const auto [length, dots] = list_length(*this, node.seq, open);
    node.length = length;
    node.dots = dots;
    return add(std::move(node));
}

Index Arena::length(const Term &term, std::int64_t depth, std::int64_t limit) {
    if (level_ > MAX_NESTING) {
        throw BuiltinFailure{.reason = abi::v1::ErrorReason::system_limit};
    }
    ++level_;

    struct Leave {
        std::size_t &level;

        ~Leave() { --level; }
    } leave{level_};

    if (const auto text = simple(term)) {
        return *text;
    }
    if (term.is_cons()) {
        return list(term, depth, limit);
    }
    if (term.is_tuple()) {
        return tuple(term, depth, limit);
    }
    if (term.is_map()) {
        return map(term, depth, limit);
    }
    if (term.is_bitstring()) {
        return depth == 1 ? cut(deep_.ascii("<<...>>"), 7, More{.term = term, .depth = 1}) : binary(term, depth, limit);
    }
    return term.is_native_record() ? record(term, depth, limit) : other(term);
}

// The leaves print_length/7 writes at any depth: empty containers and atoms.
std::optional<Index> Arena::simple(const Term &term) {
    if (term.is_atom()) {
        const auto text = atom_text(deep_, term, options_.encoding);
        return leaf(text, deep_.chars_length(context_, text));
    }
    const auto text = empty_text(term);
    if (text.empty()) {
        return std::nullopt;
    }
    return leaf(deep_.ascii(text), static_cast<std::int64_t>(text.size()));
}

// io_lib:write/1 of a number, pid, port, reference or fun, sized by chars_length/1.
Index Arena::other(const Term &term) {
    Writer writer(context_, deep_, Encoding::latin1, MapsOrder::undefined);
    const auto text = writer.write(term, -1);
    return leaf(text, deep_.chars_length(context_, text));
}

// A list: a string when printable_list/4 says so, else its elements (rerun as a whole when cut).
Index Arena::list(const Term &list, std::int64_t depth, std::int64_t limit) {
    if (options_.strings) {
        if (const auto text = printable_string(list, {.depth = depth, .limit = limit})) {
            return *text;
        }
    }
    Node node;
    node.shape = Shape::list;
    node.seq = list_rest(list, depth, tsub(limit, 2));
    const auto index = container(std::move(node), 2);
    if (nodes_[index].dots > 0) {
        nodes_[index].more = More{.term = list, .depth = depth};
    }
    return index;
}

std::pair<Deep::Ref, std::int64_t> Arena::string_text(const Term &list) {
    const Terms terms(context_);
    if (options_.utf8) {
        const auto encoding = terms.atom(options_.encoding == Encoding::latin1 ? "latin1" : "unicode");
        const auto [bin, size] = string_bin(context_, list, QuoteArg{terms.integer('"')}, EncodingArg{encoding});
        return {deep_.term(bin), size};
    }
    const auto escape = options_.encoding == Encoding::latin1 ? Escape::latin1 : Escape::unicode;
    const auto text = io_lib::string_text(deep_, list, escape, terms.integer('"'));
    return {text, deep_.chars_length(context_, text)};
}

// printable_list/4 and the string it chooses: the whole list, or a prefix followed by "...".
std::optional<Index> Arena::printable_string(const Term &list, Cut at) {
    const auto [depth, limit] = at;
    if (depth == 1) {
        return std::nullopt;
    }
    const auto choice = limit < 0                               ? StringChoice{.printable = printable_list(list)}
                        : options_.encoding == Encoding::latin1 ? latin1_choice(context_, list, limit)
                                                                : unicode_choice(context_, list, limit);
    if (!choice.printable) {
        return std::nullopt;
    }
    if (!choice.prefix) {
        const auto [text, size] = string_text(list);
        return leaf(text, size);
    }
    const auto [text, size] = string_text(*choice.prefix);
    return leaf(deep_.cons(text, deep_.ascii("...")), 3 + size);
}

Seq Arena::list_rest(const Term &rest_term, std::int64_t depth, std::int64_t limit) {
    Seq seq;
    for (Term rest = rest_term;;) {
        if (rest.is_nil()) {
            return seq;
        }
        if (depth == 1 || limit == 0) {
            seq.end = dots(More{.kind = More::Kind::list_rest, .term = rest, .depth = depth});
            return seq;
        }
        const auto below = depth_minus(depth, 1);
        if (!rest.is_cons()) {
            seq.end = length(rest, below, limit);
            return seq;
        }
        const auto tail = rest.tail_unchecked();
        const auto room = tail.is_nil() ? limit : tsub(limit, 1);
        const auto element = length(rest.head_unchecked(), below, room);
        seq.items.push_back(element);
        limit = tsub(room, nodes_[element].length);
        depth = below;
        rest = tail;
    }
}

// A tuple: "{...}" at depth 1, else its elements, tagged when it starts with an atom (never rerun as a whole).
Index Arena::tuple(const Term &tuple, std::int64_t depth, std::int64_t limit) {
    if (depth == 1) {
        return cut(deep_.ascii("{...}"), 5, More{.term = tuple, .depth = 1});
    }
    Node node;
    node.shape = Shape::tuple;
    node.seq = tuple_rest(tuple, 1, depth, tsub(limit, 2));
    node.tagged = tuple.tuple_element_unchecked(0).is_atom() && tuple.tuple_size_unchecked() > 1;
    return container(std::move(node), 2);
}

Seq Arena::tuple_rest(const Term &tuple, std::size_t at, std::int64_t depth, std::int64_t limit) {
    Seq seq;
    const auto size = tuple.tuple_size_unchecked();
    for (; at <= size; ++at) {
        if (depth == 1 || limit == 0) {
            seq.end = dots(More{.kind = More::Kind::tuple_rest, .term = tuple, .index = at, .depth = depth});
            return seq;
        }
        const auto room = at == size ? limit : tsub(limit, 1);
        depth = depth_minus(depth, 1);
        const auto element = length(tuple.tuple_element_unchecked(at - 1), depth, room);
        seq.items.push_back(element);
        limit = tsub(room, nodes_[element].length);
    }
    return seq;
}

// A map: "#{...}" at depth 1, else its associations in print order.
Index Arena::map(const Term &map, std::int64_t depth, std::int64_t limit) {
    if (depth == 1) {
        return cut(deep_.ascii("#{...}"), 6, More{.term = map, .depth = 1});
    }
    auto entries = std::make_shared<const std::vector<std::pair<Term, Term>>>(map_entries(map, options_.order));
    Node node;
    node.shape = Shape::map;
    const More at{.kind = More::Kind::map_pairs, .depth = depth, .depth0 = depth_minus(depth, 1), .entries = entries};
    node.seq = map_pairs(at, depth, tsub(limit, 3));
    return container(std::move(node), 3);
}

// print_length_map_pairs/8 from association `at.index`; D drops per association while values keep D0.
Seq Arena::map_pairs(const More &at, std::int64_t depth, std::int64_t limit) {
    Seq seq;
    const auto &entries = *at.entries;
    for (auto index = at.index; index < entries.size(); ++index) {
        if (depth == 1 || limit == 0) {
            auto more = at;
            more.index = index;
            more.depth = depth;
            seq.end = dots(std::move(more));
            return seq;
        }
        const auto room = index + 1 == entries.size() ? limit : tsub(limit, 1);
        const auto key = length(entries[index].first, at.depth0, room);
        const auto key_length = nodes_[key].length + 4;
        const auto value = length(entries[index].second, at.depth0, tsub(room, key_length));
        Node pair;
        pair.shape = Shape::map_pair;
        pair.key = key;
        pair.value = value;
        pair.length = key_length + nodes_[value].length;
        pair.dots = nodes_[key].dots + nodes_[value].dots;
        const auto index_of_pair = add(std::move(pair));
        seq.items.push_back(index_of_pair);
        depth = depth_minus(depth, 1);
        limit = tsub(room, nodes_[index_of_pair].length);
    }
    return seq;
}

// A native record: io_lib:write/5 at depth 1, else #module:name{field = Value, ...}.
Index Arena::record(const Term &record, std::int64_t depth, std::int64_t limit) {
    if (depth == 1) {
        Writer writer(context_, deep_, options_.encoding, options_.order);
        const auto text = writer.write(record, 1);
        return cut(text, deep_.chars_length(context_, text), More{.term = record, .depth = 1});
    }
    const auto [module, name] = need(detail::record_identity(record));
    Node node;
    node.shape = Shape::record;
    node.text = deep_.list({deep_.code('#'), atom_text(deep_, module, options_.encoding), deep_.code(':'),
                            atom_text(deep_, name, options_.encoding)});
    node.name_length = deep_.chars_length(context_, node.text);
    node.seq = record_fields(record, 0, depth_minus(depth, 1), tsub(limit, node.name_length + 2));
    const auto open = node.name_length + 2;
    return container(std::move(node), open);
}

// print_length_native_record_fields/8 from field `at`; OTP takes the comma off the last field's limit instead.
Seq Arena::record_fields(const Term &record, std::size_t at, std::int64_t depth, std::int64_t limit) {
    Seq seq;
    const auto fields = need(record.record_fields());
    for (; at < fields.size(); ++at) {
        if (depth == 1 || limit == 0) {
            seq.end = dots(More{.kind = More::Kind::record_fields, .term = record, .index = at, .depth = depth});
            return seq;
        }
        const auto room = at + 1 == fields.size() ? tsub(limit, 1) : limit;
        const auto key = atom_text(deep_, fields[at].first, options_.encoding);
        const auto key_length = deep_.chars_length(context_, key) + 3;
        depth = depth_minus(depth, 1);
        const auto value = length(fields[at].second, depth, tsub(room, key_length));
        Node field;
        field.shape = Shape::field;
        field.text = key;
        field.name_length = key_length;
        field.value = value;
        field.length = key_length + nodes_[value].length;
        field.dots = nodes_[value].dots;
        seq.items.push_back(add(std::move(field)));
        limit = tsub(room, key_length + nodes_[value].length);
    }
    return seq;
}

// print_length_binary/7: a printable binary as a string (perhaps a prefix), else its bytes.
Index Arena::binary(const Term &binary, std::int64_t depth, std::int64_t limit) {
    static_cast<void>(depth_minus(depth, 1));
    if (options_.strings && binary.bit_size_unchecked() % 8 == 0) {
        if (const auto index = printable_binary(binary, {.depth = depth, .limit = limit}); index != NONE) {
            return index;
        }
    }
    return raw_binary(binary, depth, limit);
}

// The quoted string of printable bytes (write_string/2, or write_string_bin/3 between quotes) and its length/1 or
// chars_length/1.
std::pair<Deep::Ref, std::int64_t> Arena::binary_text(std::span<const std::byte> part, const Term &binary,
                                                      bool latin1) {
    const Terms terms(context_);
    if (options_.utf8) {
        const auto bin = part.size() == string::binary_bytes(binary).size() ? binary : terms.binary(part);
        const auto [utf8, length] =
            string_bin(context_, bin, QuoteArg{terms.nil()}, EncodingArg{terms.atom(latin1 ? "latin1" : "unicode")});
        return {deep_.list({deep_.code('"'), deep_.term(utf8), deep_.code('"')}), length + 2};
    }
    std::u32string codes;
    if (latin1) {
        std::ranges::transform(part, std::back_inserter(codes),
                               [](std::byte b) { return std::to_integer<char32_t>(b); });
    } else {
        codes = decode_utf8(part).value_or(std::u32string{});
    }
    const auto quote = terms.integer('"');
    auto text = deep_.cons(deep_.term(quote), Deep::NIL);
    for (auto it = codes.rbegin(); it != codes.rend(); ++it) {
        text = string_char(deep_, terms.integer(static_cast<std::int64_t>(*it)), Escape::unicode, '"', text);
    }
    text = deep_.cons(deep_.term(quote), text);
    return {text, latin1 ? top_length(deep_, text) : deep_.chars_length(context_, text)};
}

// The string forms of a printable binary (printable_bin0/5 at D - 1 within T - 6); NONE when it is not printable.
Index Arena::printable_binary(const Term &binary, Cut at) {
    const auto [depth, limit] = at;
    const auto view = string::binary_bytes(binary);
    const std::vector<std::byte> bytes(view.begin(), view.end());
    const auto below = depth - 1;
    const BinaryScan scan{
        .bytes = bytes,
        .len = printable_length(static_cast<std::int64_t>(bytes.size()), {.depth = below, .limit = tsub(limit, 6)}),
        .depth = below};
    const auto found = printable_bin(scan, options_.encoding == Encoding::latin1);
    if (!found) {
        return NONE;
    }
    const auto [text, length] = binary_text(std::span(bytes).first(found->bytes), binary, found->latin1);
    const auto open = deep_.code('<');
    const auto close = deep_.code('>');
    if (!found->prefix) {
        return found->latin1 ? leaf(deep_.list({open, open, text, close, close}), 4 + length)
                             : leaf(deep_.list({open, open, text, deep_.ascii("/utf8>>")}), 9 + length);
    }
    const More more{.term = binary, .depth = depth};
    return found->latin1 ? cut(deep_.list({open, open, text}, deep_.ascii("...>>")), 7 + length, more)
                         : cut(deep_.list({open, open, text}, deep_.ascii("/utf8...>>")), 12 + length, more);
}

// io_lib:write_binary/3 as {bin, S}, cut short with dots when bytes are left.
Index Arena::raw_binary(const Term &binary, std::int64_t depth, std::int64_t limit) {
    Writer writer(context_, deep_, Encoding::latin1, MapsOrder::undefined);
    const auto [text, rest] = writer.binary(binary, {.depth = depth, .limit = limit});
    Node node;
    node.shape = Shape::bin;
    node.text = text;
    node.length = static_cast<std::int64_t>(deep_.flat(text).size());
    if (need(rest.bit_size()) > 0) {
        node.dots = 3;
        node.more = More{.term = binary, .depth = depth};
    }
    return add(std::move(node));
}

// expand/3: dotted parts printed deeper by `extra` levels within chars_limit `limit`.
Index Arena::expand(Index index, Deeper at) {
    const auto [limit, extra] = at;
    const auto node = nodes_[index];
    if (node.more) {
        return length(node.more->term, node.more->depth + extra, limit);
    }
    if (node.dots == 0) {
        return index;
    }
    switch (node.shape) {
    case Shape::tuple:
    case Shape::map:
        return expand_container(node, node.shape == Shape::tuple ? 2 : 3, at);
    case Shape::record:
        return expand_container(node, node.name_length + 2, at);
    case Shape::map_pair: {
        auto pair = node;
        pair.key = expand(node.key, {tsub(limit, 1), extra});
        const auto key_length = nodes_[pair.key].length + 4;
        pair.value = expand(node.value, {tsub(limit, key_length), extra});
        pair.length = key_length + nodes_[pair.value].length;
        pair.dots = nodes_[pair.key].dots + nodes_[pair.value].dots;
        return add(std::move(pair));
    }
    case Shape::field: {
        auto field = node;
        field.value = expand(node.value, {tsub(limit, node.name_length), extra});
        field.length = node.name_length + nodes_[field.value].length;
        field.dots = nodes_[field.value].dots;
        return add(std::move(field));
    }
    default:
        return index;
    }
}

Index Arena::expand_container(const Node &node, std::int64_t open, Deeper at) {
    auto copy = node;
    copy.seq = expand_seq(node.seq, {tsub(at.limit, open), at.extra});
    return container(std::move(copy), open);
}

// expand_list/3: each element expanded, the dots ending the list rerun deeper.
Seq Arena::expand_seq(const Seq &seq, Deeper at) {
    auto [limit, extra] = at;
    Seq out;
    for (std::size_t i = 0; i < seq.items.size(); ++i) {
        const auto room = i + 1 == seq.items.size() && seq.end == NONE ? limit : tsub(limit, 1);
        const auto element = expand(seq.items[i], {room, extra});
        out.items.push_back(element);
        limit = tsub(room, nodes_[element].length);
    }
    if (seq.end == NONE || !nodes_[seq.end].more) {
        out.end = seq.end;
        return out;
    }
    const auto more = *nodes_[seq.end].more;
    auto rest = rerun(more, {limit, extra});
    out.items.insert(out.items.end(), rest.items.begin(), rest.items.end());
    out.end = rest.end;
    return out;
}

// A dots' More fun: the rest of a list, tuple, map or record printed from its depth plus `extra`.
Seq Arena::rerun(const More &more, Deeper at) {
    const auto [limit, extra] = at;
    const auto depth = more.depth + extra;
    switch (more.kind) {
    case More::Kind::list_rest:
        return list_rest(more.term, depth, limit);
    case More::Kind::tuple_rest:
        return tuple_rest(more.term, more.index, depth, limit);
    case More::Kind::map_pairs: {
        auto at = more;
        at.depth = depth;
        return map_pairs(at, depth, limit);
    }
    case More::Kind::record_fields:
        return record_fields(more.term, more.index, depth, limit);
    default:
        return {.items = {}, .end = length(more.term, depth, limit)};
    }
}

Index Arena::intermediate(const Term &term, std::int64_t depth, std::int64_t limit) {
    const auto root = length(term, 1, limit);
    const auto &node = nodes_[root];
    if (node.dots == 0 || node.length > limit || depth == 1) {
        return root;
    }
    return find_upper(root, limit, depth, {1, 2, node.length});
}

// find_upper/11: double the depth step until the text passes the limit or has no dots left.
Index Arena::find_upper(Index lower, std::int64_t limit, std::int64_t depth, Search at) {
    for (;;) {
        const auto step = at.step * 2;
        const auto upper_depth = depth < 0 ? at.depth + step : std::min(at.depth + step, depth);
        const auto index = expand(lower, {limit, upper_depth - at.depth});
        const auto &node = nodes_[index];
        if (node.dots == 0 || node.length == at.last_length) {
            return index;
        }
        if (node.length > limit || (upper_depth >= depth && depth >= 0)) {
            return search_depth(lower, index, limit, {at.depth, upper_depth});
        }
        lower = index;
        at = {upper_depth, step, node.length};
    }
}

// search_depth/10: bisect the depths between a text within the limit and one past it.
Index Arena::search_depth(Index lower, Index upper, std::int64_t limit, std::pair<std::int64_t, std::int64_t> depths) {
    auto [low, high] = depths;
    while (high - low != 1) {
        const auto middle = (low + high) / 2;
        const auto index = expand(lower, {limit, middle - low});
        if (nodes_[index].length > limit) {
            upper = index;
            high = middle;
        } else {
            lower = index;
            low = middle;
        }
    }
    return nodes_[lower].length == limit ? lower : upper;
}
} // namespace clause::runtime::builtins::io_lib::pretty
