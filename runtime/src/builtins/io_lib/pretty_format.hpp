#pragma once
#include "pretty.hpp"
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

// io_lib_pretty's intermediate format (plan 21 step LIB4D): what print_length/7 computes for a term, kept in an
// arena so expand/3 can deepen dotted parts and pp/8 and write/1 can lay the rest out as OTP does.
namespace clause::runtime::builtins::io_lib::pretty {
// A node of the arena; NONE stands for [] where an if_list() ends.
using Index = std::uint32_t;
inline constexpr Index NONE = std::numeric_limits<Index>::max();

// A depth that is not an integer: comparisons fail and arithmetic raises badarith, as in OTP.
inline constexpr std::int64_t BAD_DEPTH = NON_INTEGER_DEPTH;

// The What of an intermediate value: chars, {bin, S}, the dots ending an if_list(), or a container.
enum class Shape : std::uint8_t { chars, bin, dots, field, list, map, map_pair, record, tuple };

// An if_list(): elements, then [] (NONE), a dots node or an improper tail.
struct Seq {
    std::vector<Index> items{};
    Index end = NONE;
};

// The More fun of a value cut short: which print_length function to rerun, on what, from which depth.
struct More {
    enum class Kind : std::uint8_t { term, list_rest, tuple_rest, map_pairs, record_fields };
    Kind kind = Kind::term;
    // The term, list rest, tuple or record.
    Term term{};
    // The tuple element (from 1), map association or record field the rerun starts at.
    std::size_t index = 0;
    // D of the call that left the dots, and print_length_map_pairs' D0.
    std::int64_t depth = 0;
    std::int64_t depth0 = 0;
    // The associations of a map in print order, shared by every rerun.
    std::shared_ptr<const std::vector<std::pair<Term, Term>>> entries{};
};

// One intermediate value {What, Len, NumOfDots, More}.
struct Node {
    Shape shape = Shape::chars;
    // A tuple of two or more elements starting with an atom.
    bool tagged = false;
    // S of chars and {bin, S}; Name of a record or field.
    Deep::Ref text = Deep::NIL;
    // NameLen of a record or field.
    std::int64_t name_length = 0;
    // The elements of a list, tuple or map, a record's fields.
    Seq seq{};
    // A map pair's key and value; a field's value.
    Index key = NONE;
    Index value = NONE;
    std::int64_t length = 0;
    std::int64_t dots = 0;
    std::optional<More> more{};
};

// expand/3's T and Dd: the chars_limit and how many levels deeper the dots are printed.
struct Deeper {
    std::int64_t limit = -1;
    std::int64_t extra = 0;
};

// The options print_length/7 passes down unchanged.
struct LengthOptions {
    Encoding encoding = Encoding::latin1;
    // Leaves as UTF-8 binaries (print_bin/2, write_bin/5): OTP's {InEnc, utf8} encoding.
    bool utf8 = false;
    bool strings = true;
    MapsOrder order = MapsOrder::undefined;
};

class Arena final {
  public:
    Arena(ProcessContext &context, Deep &deep, LengthOptions options)
        : context_(context), deep_(deep), options_(options) {}

    const Node &node(Index index) const { return nodes_[index]; }

    Deep &deep() const { return deep_; }

    ProcessContext &context() const { return context_; }

    // print_length/7 of `term` at depth `depth` within chars_limit `limit`.
    Index length(const Term &term, std::int64_t depth, std::int64_t limit);
    // intermediate/7 (limit above 0).
    Index intermediate(const Term &term, std::int64_t depth, std::int64_t limit);

  private:
    Index add(Node node);
    Index leaf(Deep::Ref text, std::int64_t length);
    Index cut(Deep::Ref text, std::int64_t length, More more);
    Index dots(More more);

    std::optional<Index> simple(const Term &term);
    Index list(const Term &list, std::int64_t depth, std::int64_t limit);
    Seq list_rest(const Term &rest, std::int64_t depth, std::int64_t limit);
    Index tuple(const Term &tuple, std::int64_t depth, std::int64_t limit);
    Seq tuple_rest(const Term &tuple, std::size_t at, std::int64_t depth, std::int64_t limit);
    Index map(const Term &map, std::int64_t depth, std::int64_t limit);
    Seq map_pairs(const More &at, std::int64_t depth, std::int64_t limit);
    Index record(const Term &record, std::int64_t depth, std::int64_t limit);
    Seq record_fields(const Term &record, std::size_t at, std::int64_t depth, std::int64_t limit);
    Index binary(const Term &binary, std::int64_t depth, std::int64_t limit);
    Index printable_binary(const Term &binary, Cut at);
    std::pair<Deep::Ref, std::int64_t> binary_text(std::span<const std::byte> part, const Term &binary, bool latin1);
    Index raw_binary(const Term &binary, std::int64_t depth, std::int64_t limit);
    Index other(const Term &term);
    std::optional<Index> printable_string(const Term &list, Cut at);

    // A container's node with list_length/3 of its seq from `open` characters.
    Index container(Node node, std::int64_t open);
    // The written string of a list and its length (write_string/2).
    std::pair<Deep::Ref, std::int64_t> string_text(const Term &list);
    // A More fun's rerun with chars_limit `limit`, `extra` levels deeper.
    Seq rerun(const More &more, Deeper at);
    // expand/3 and expand_list/3,4.
    Index expand(Index index, Deeper at);
    Seq expand_seq(const Seq &seq, Deeper at);
    Index expand_container(const Node &node, std::int64_t open, Deeper at);

    // find_upper/11's state: the depth reached, the step to double and the last length seen.
    struct Search {
        std::int64_t depth = 1;
        std::int64_t step = 2;
        std::int64_t last_length = 0;
    };

    // find_upper/11 and search_depth/10.
    Index find_upper(Index lower, std::int64_t limit, std::int64_t depth, Search at);
    Index search_depth(Index lower, Index upper, std::int64_t limit, std::pair<std::int64_t, std::int64_t> depths);

    ProcessContext &context_;
    Deep &deep_;
    LengthOptions options_;
    std::vector<Node> nodes_;
    // The containers print_length is inside, bounded by MAX_NESTING.
    std::size_t level_ = 0;
};

// What a layout keeps for the whole term: the line length Ll, M (the most characters on one line) and TInd (the tag
// indentation tag_indent/4 chose).
struct Bounds {
    std::int64_t line_length = 80;
    std::int64_t most = 0;
    std::int64_t tag = 1;
};

// Where pp/8, cind/7 and their helpers place a value: column, the closing characters after it (LD) and the
// characters on the line before it (W).
struct Place {
    std::int64_t column = 1;
    std::int64_t last = 0;
    std::int64_t width = 0;
};

// write/1: the value on one line, in OTP's deep shape.
Deep::Ref write(const Arena &arena, Index index);
// while_fail([-1, 4], cind/7, 1): the tag indentation the layout uses.
std::int64_t tag_indent(const Arena &arena, Index root, const Bounds &bounds, std::int64_t column);
// pp/8 of the root from `column`, indented by indent(Col).
Deep::Ref layout(const Arena &arena, Index root, const Bounds &bounds, std::int64_t column);

// pp_bin/11 of the root: the bytes, the newlines written and the final position.
struct BinaryLayout {
    std::string bytes{};
    std::int64_t newlines = 0;
    std::int64_t position = 0;
};

BinaryLayout layout_bin(const Arena &arena, Index root, const Bounds &bounds, std::int64_t column);

// ?ATM(If): the value is chars (a list or a binary), so it is written whole.
inline bool atomic(const Node &node) { return node.shape == Shape::chars; }

// tsub/2: never changes the limit's sign.
inline std::int64_t tsub(std::int64_t limit, std::int64_t used) {
    return limit < 0 ? limit : limit >= used ? limit - used : 0;
}

// last_depth/2: one more closing character after the last element of an if_list().
inline std::int64_t last_depth(const Seq &seq, std::size_t next, std::int64_t last) {
    return next < seq.items.size() ? 0 : last + 1;
}

// D - N, raising badarith for a depth that is not an integer.
std::int64_t depth_minus(std::int64_t depth, std::int64_t n);
} // namespace clause::runtime::builtins::io_lib::pretty
