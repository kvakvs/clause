#pragma once
#include "../support.hpp"
#include "../typed.hpp"
#include "../unicode/code_points.hpp"
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

// The string builtins (plan 21 step LIB4, docs/builtins.md#string): OTP's string module over chardata terms, each
// function following string.erl's clauses so results keep OTP's shapes (tails of the input, sub-binaries, flat
// code point lists where OTP builds them) and raise OTP's errors.
namespace clause::runtime::builtins::string {
using unicode::cp_term;
using unicode::gc_term;
using unicode::split;

// The roles of arguments whose types alone would be easily swapped (typed.hpp's Named).
struct TextRole;
struct PatternRole;
struct OtherRole;
struct DirRole;
struct OptionRole;
struct ContRole;
struct TokenRole;
struct LengthRole;
struct CharRole;
using TextArg = Named<TextRole>;
using PatternArg = Named<PatternRole>;
using OtherArg = Named<OtherRole>;
using DirArg = Named<DirRole>;
using OptionArg = Named<OptionRole>;
// The chardata after a binary (OTP's Cont).
using ContArg = Named<ContRole>;
// A token collected in reverse (OTP's Tkn and Acc).
using TokenArg = Named<TokenRole>;
// slice/3's length.
using LengthArg = Named<LengthRole>;
// A character argument of the Latin-1 API.
using CharArg = Named<CharRole>;

// Term building over one process, as string.erl's helpers build.
class Terms final {
  public:
    explicit Terms(ProcessContext &context) : context_(context) {}

    ProcessContext &context() const { return context_; }

    Term nil() const;
    Term cons(const Term &head, const Term &tail) const;
    Term integer(std::int64_t value) const;
    Term list(std::span<const Term> items) const;
    Term list(std::span<const Term> items, const Term &tail) const;
    Term atom(AtomName name) const;
    Term boolean(bool value) const;
    Term tuple(std::span<const Term> items) const;
    Term binary(std::span<const std::byte> bytes) const;
    // A binary of code points in UTF-8.
    Term utf8(std::u32string_view codes) const;
    // A sub-binary from byte `from` for `count` bytes, sharing an off-heap buffer.
    Term slice(const Term &binary, std::size_t from, std::size_t count) const;

    // string.erl's rev/1: [B] to B, a list reversed, a code point as it is.
    Term rev(const Term &term) const;
    // string.erl's append/2: a cluster in front of a string, [Char] or Char alone before <<>>, [Char, Bin] before a
    // binary.
    Term append(const Term &cluster, const Term &str) const;
    // string.erl's stack/2: Bin in front of St, dropped when either is empty.
    Term stack(const Term &bin, const Term &st) const;
    // string.erl's btoken/2: a binary part after the reversed token collected so far (OTP's Tkn list): the part alone
    // for an empty token, a binary for one code point, the reversed token alone for an empty part, else both.
    Term btoken(const Term &part, const TokenArg &token) const;
    // unicode:characters_to_list/1 and characters_to_binary/1 as terms (error and incomplete tuples included).
    Term characters_to_list(const Term &data) const;
    Term characters_to_binary(const Term &data) const;

  private:
    ProcessContext &context_;
};

// The forms string.erl's clauses tell apart at the head of chardata: a list starting with a code point, a list
// starting with a binary, another list, a binary, anything else.
enum class Form : std::uint8_t { point, binary_head, list, binary, other };
Form form_of(const Term &data);
// Whether chardata starts with two Latin-1 code points, the first not CR (string.erl's [CP1 | [CP2 | _]] fast paths).
bool ascii_pair_at(const Term &data);

// A small integer's value.
std::optional<std::int64_t> small_of(const Term &term);
// Whether a term is the integer `code`.
bool is_char(const Term &term, std::int64_t code);
// string.erl's ?ASCII_LIST(CP1, CP2): two Latin-1 code points, the first not CR.
bool ascii_pair(const Term &cp1, const Term &cp2);
// Whether two terms are the same (=:=).
bool same(const Term &left, const Term &right);
// string:is_empty/1.
bool is_empty(const Term &data);
// The code point list of a cluster term (an integer or a list of them).
std::u32string cluster_codes(const Term &cluster);
// The bytes of a binary term, read in place; only valid until the next allocation.
std::span<const std::byte> binary_bytes(const Term &binary);
// The first UTF-8 character of a binary and its byte length; none for an empty or invalid binary.
std::optional<std::pair<char32_t, std::size_t>> utf8_head(const Term &binary);
// The same for a binary known to start with a character; badarg otherwise.
std::pair<char32_t, std::size_t> first_char(const Term &binary);

// string:length/1 and string:slice(Data, 0, Length), for io_lib's field widths (plan 21 step LIB4C).
Term length_of(ProcessContext &context, const Term &data);
Term prefix_of(ProcessContext &context, const Term &data, std::int64_t length);
// string:trim/1, for io_lib:collect_line_no_eol/4.
Term trim_of(ProcessContext &context, const Term &data);

// error({badarg, Rest}).
[[noreturn]] void badarg_rest(ProcessContext &context, const Term &rest);
// error({badarg, Rest}) for a cp or gc result that is {error, Rest}.
[[noreturn]] void badarg_error(ProcessContext &context, const Term &error_tuple);
// error({case_clause, Term}).
[[noreturn]] void case_clause(const Term &term);
} // namespace clause::runtime::builtins::string
