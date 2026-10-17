#pragma once
#include "string.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// string.erl's search machinery (plan 21 step LIB4): separator sets, the binary searches that find separators or a
// needle inside a binary while clusters may continue into the chardata after it, and the prefix test. Shared by
// trim, take, lexemes, split, find and the number parsers.
namespace clause::runtime::builtins::string {
// search_pattern/1 and search_compile/1: the separator clusters as given, their first code points, and the UTF-8 of
// those code points for searching binaries.
struct Seps {
    // The separator list as given.
    Term list;
    std::vector<Term> clusters;
    std::vector<Term> points;
    std::vector<std::string> utf8;
};

// The separators of a list; function_clause for anything else, badmatch for an element without a code point.
Seps seps_of(ProcessContext &context, const Term &list);
// search_compile/1's binary:compile_pattern/1: badarg for an empty separator set, where OTP compiles it.
void require_compiled(const Seps &seps);
// lists:member/2 by =:=.
bool member(const std::vector<Term> &items, const Term &item);
// lists:member/2 over a list term: badarg for anything that is not a proper list, as the BIF raises.
bool member(const PatternArg &list, const Term &item);

// A search's answer: matched with OTP's result list ([Cont1] or [Cont0 | Cont]), or nomatch with the rest.
struct Found {
    bool matched;
    Term rest;
};

// bin_search/3: the first separator cluster inside Bin (clusters may continue into Cont).
Found bin_search(ProcessContext &context, const Term &bin, const ContArg &cont, const Seps &seps);
// bin_search_inv/3: the first cluster inside Bin that is not a separator of the list.
Found bin_search_inv(ProcessContext &context, const Term &bin, const ContArg &cont, const PatternArg &clusters);

// bin_search_str/4's answer: the needle found with the bytes before it, OTP's [Cs0 | Cont] at it and the chardata
// after it, or not found with the bytes searched and the chardata to continue with.
struct Needle {
    bool matched;
    Term before;
    Term at;
    Term after;
    std::size_t searched;
};

Needle bin_search_str(ProcessContext &context, const Term &bin, std::size_t start, const Term &cont,
                      const std::vector<Term> &needle);
// prefix_1/2: the chardata after the needle's code points, or none.
std::optional<Term> after_prefix(ProcessContext &context, Term data, const std::vector<Term> &needle);
// cp_prefix/2: the code points of Orig before Cont.
Term cp_prefix(ProcessContext &context, const Term &orig, const ContArg &cont);
// string:equal/2.
bool equal_terms(ProcessContext &context, const Term &a, const Term &b);

// A {Head, Tail} pair of chardata.
struct Halves {
    Term head;
    Term tail;
};

// take_l/3: the leading run of the separator clusters and the rest.
Halves take_leading(ProcessContext &context, const Term &str, const PatternArg &clusters);
// The elements of a proper list; none for anything else.
std::optional<std::vector<Term>> proper_list(const Term &list);
} // namespace clause::runtime::builtins::string
