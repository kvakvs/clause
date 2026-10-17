#pragma once
#include "../typed.hpp"
#include "deep.hpp"
#include <cstdint>
#include <string>
#include <utility>

// io_lib's term writer (plan 21 step LIB4C, docs/builtins.md#io-lib): io_lib:write/1,2,5 and the ~w and ~W controls
// as OTP's write1 builds them, with depth limits, atoms in the selected encoding and maps in the selected order.
namespace clause::runtime::builtins::io_lib {
// How atoms are written: latin1 escapes characters beyond Latin-1 (write_atom_as_latin1), unicode keeps them.
enum class Encoding : std::uint8_t { latin1, unicode };

// The order of map associations: Clause's key order, sorted by term order, reversed, or another term (badarg when a
// map is written; comparison funs are not supported, docs/differences.md).
enum class MapsOrder : std::uint8_t { undefined, ordered, reversed, other };
// The order a maps_order term names.
MapsOrder order_of(const Term &order);

// How string_char escapes a character: latin1 lists, Unicode lists kept, or Unicode written as \x{...}.
enum class Escape : std::uint8_t { latin1, unicode, unicode_as_latin1 };

// Where write_binary/3 stops: at a depth and a number of characters, -1 for neither.
struct Cut {
    std::int64_t depth = -1;
    std::int64_t limit = -1;
};

class Writer final {
  public:
    Writer(ProcessContext &context, Deep &deep, Encoding encoding, MapsOrder order)
        : context_(context), deep_(deep), encoding_(encoding), order_(order) {}

    // write1(Term, Depth): Depth -1 is unlimited.
    Deep::Ref write(const Term &term, std::int64_t depth);
    // write_binary/3: the bitstring's text within `cut.limit` characters (-1 unlimited) and the bits left unwritten.
    std::pair<Deep::Ref, Term> binary(const Term &bits, Cut cut);
    // The size write_bin/5 reports for the last write: the characters with OTP's counting quirks.
    std::int64_t size_of(Deep::Ref ref) const;

  private:
    // Where a term is written: write1's depth left and the containers around it.
    struct Place {
        std::int64_t depth = -1;
        std::size_t level = 0;
    };

    // The text of an atom, number, pid, port, reference or fun.
    Deep::Ref leaf(const Term &term);
    Deep::Ref list(const Term &term, Place at);
    Deep::Ref tuple(const Term &term, Place at);
    Deep::Ref map(const Term &term, Place at);
    Deep::Ref record(const Term &term, Place at);
    Deep::Ref node(const Term &term, Place at);

    // The process the bits left over by write_binary are built in.
    ProcessContext &context_;
    // Where the text is built.
    Deep &deep_;
    Encoding encoding_;
    MapsOrder order_;
    // What write_bin1 counts beyond the characters: UTF-8 bytes of Latin-1 atoms, short "...", record commas.
    std::int64_t size_adjust_ = 0;
};

// The associations of a map in the order `order` gives.
std::vector<std::pair<Term, Term>> map_entries(const Term &map, MapsOrder order);

// io_lib:write_atom/1 and write_atom_as_latin1/1 as a flat list.
Deep::Ref atom_text(Deep &deep, const Term &atom, Encoding encoding);
// io_lib:quote_atom/2 of an atom's spelling (empty for any other term) and its characters.
bool quote_atom(std::string_view atom, const Term &chars);
// string_char/4: `code` (an integer term) escaped in front of `tail`, quoted by `quote` (-1: none).
Deep::Ref string_char(Deep &deep, const Term &code, Escape escape, std::int64_t quote, Deep::Ref tail);
// io_lib:write_string/2 and its encodings: [Q | escaped characters | Q].
Deep::Ref string_text(Deep &deep, const Term &string, Escape escape, const Term &quote);
// The quote and InEnc arguments of the string writers (typed.hpp's Named).
struct QuoteRole;
struct EncodingRole;
using QuoteArg = Named<QuoteRole>;
using EncodingArg = Named<EncodingRole>;

// io_lib:write_string_bin/3: the escaped string or binary (InEnc says how a binary reads) as UTF-8, and its size.
std::pair<Term, std::int64_t> string_bin(ProcessContext &context, const Term &string, const QuoteArg &quote,
                                         const EncodingArg &encoding);
} // namespace clause::runtime::builtins::io_lib
