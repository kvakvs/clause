#pragma once
#include "../support.hpp"
#include "../typed.hpp"
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// The uri_string builtins (plan 21 step LIB5G, docs/builtins.md#uri-string): RFC 3986 parsing with OTP's error
// fragments, recomposition, normalization, resolution, percent coding and form-urlencoded queries.
namespace clause::runtime::builtins::uri {
using Bytes = std::vector<std::byte>;

// An {error, Reason, Term} uri_string answers (or throws, where OTP lets it escape).
struct Failure {
    std::string_view reason;
    Term term;
};

// The roles of arguments whose types alone would be easily swapped (typed.hpp's Named).
struct UriRole;
struct BaseRole;
struct OptionsRole;
struct SafeRole;
struct PairsRole;
// A URI string or map.
using UriArg = Named<UriRole>;
// resolve's base URI.
using BaseArg = Named<BaseRole>;
// An option list.
using OptionsArg = Named<OptionsRole>;
// quote/2's safe characters.
using SafeArg = Named<SafeRole>;
// compose_query's key-value pairs.
using PairsArg = Named<PairsRole>;

[[noreturn]] void fail(ProcessContext &context, std::string_view reason, const Term &term);
[[noreturn]] void fail(ProcessContext &context, std::string_view reason, std::span<const std::byte> bytes);
// {error, invalid_uri, [C]}.
[[noreturn]] void invalid_char(ProcessContext &context, char32_t code);

// The {error, Reason, Term} tuple of a failure.
Term error_tuple(ProcessContext &context, const Failure &failure);
// Raise a failure as OTP's uncaught throw.
[[noreturn]] void throw_failure(ProcessContext &context, const Failure &failure);

// The character classes of RFC 3986 as uri_string draws them.
bool is_alpha(char32_t c);
bool is_digit(char32_t c);
bool is_hex(char32_t c);
bool is_unreserved(char32_t c);
bool is_sub_delim(char32_t c);
bool is_reserved(char32_t c);
bool is_scheme(char32_t c);
bool is_userinfo(char32_t c);
bool is_reg_name(char32_t c);
bool is_pchar(char32_t c);
bool is_segment_nz_nc(char32_t c);
bool is_query(char32_t c);
bool is_fragment(char32_t c);
bool is_ipv6(char32_t c);
bool is_host(char32_t c);
bool is_path(char32_t c);

// inet:parse_ipv4strict_address/1 and parse_ipv6strict_address/1 succeed for these characters.
bool ipv4_address(std::u32string_view chars);
bool ipv6_address(std::u32string_view chars);

Term binary_of(ProcessContext &context, std::span<const std::byte> bytes);
Term binary_of(ProcessContext &context, std::string_view text);
Bytes bytes_of(const Term &binary);

// The UTF-8 code point at `at` of a binary, advancing `at`; none for an invalid sequence.
std::optional<char32_t> code_at(std::span<const std::byte> bytes, std::size_t &at);

// unicode:characters_to_binary/1 and characters_to_list/1: none where OTP returns an error or incomplete tuple.
std::optional<Bytes> chardata_bytes(ProcessContext &context, const Term &data);
std::optional<std::u32string> chardata_codes(ProcessContext &context, const Term &data);
// The list of code points of UTF-8 bytes, as characters_to_list/1 of a binary; none for invalid UTF-8.
std::optional<Term> utf8_list(ProcessContext &context, std::span<const std::byte> bytes);
// A proper list of code points.
Term codes_list(ProcessContext &context, std::u32string_view codes);
// unicode:characters_to_list/1 and characters_to_binary/1 as values: the result, or the error or incomplete tuple;
// badarg for data that is not chardata.
Term characters_to_list(ProcessContext &context, const Term &data);
Term characters_to_binary(ProcessContext &context, const Term &data);

// "%XX" for each byte, upper-case hex.
void percent_encode(std::string &out, std::span<const std::byte> bytes);
// The value of hex digits H and L; none unless both are hex digits.
std::optional<std::uint8_t> escape_value(std::uint8_t high, std::uint8_t low);

// The text a list or binary component is, as UTF-8 bytes; function_clause (OTP's encode/3 on an error tuple) when it
// is not characters.
Bytes text_bytes(ProcessContext &context, const Term &text);

// uri_string's encode/3: the characters `keep` (and `safe`) reject percent-encoded as UTF-8, a kept one as its low
// byte; {error, invalid_input, Rest} at invalid UTF-8.
std::string encode_bytes(ProcessContext &context, std::span<const std::byte> bytes, bool (*keep)(char32_t),
                         std::u32string_view safe = {});
// Encoded bytes in the text's form: a binary, or for a list characters_to_list/1 of them (an error or incomplete
// tuple when they are not UTF-8).
Term encoded_as(ProcessContext &context, const Term &text, const std::string &bytes);
} // namespace clause::runtime::builtins::uri
