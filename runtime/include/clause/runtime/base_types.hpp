#pragma once
#include <clause/abi/v1.hpp>
#include <cstdint>

namespace clause::runtime {

// Use the runtime target's pointer width, never the compiler host's width for cross emission.
using Word = abi::v1::TermWord;
static_assert(sizeof(Word) == 4 || sizeof(Word) == 8);
static_assert(alignof(Word) == sizeof(Word));

static constexpr Word ERL_WORD_BITS = 8 * sizeof(Word);

// Keep binaries up to 64 bytes on the process heap; refcounted objects must exceed this word count.
static constexpr Word HEAP_BINARY_THRESHOLD_WORDS = 64 / sizeof(Word);

// Resolved term categories; boxed values need their object header to determine a BoxedKind.
enum class TermKind : std::uint8_t {
    // An Integer in Erlang can be either a smallint, fitting into a machine word, minus the tag bits,
    // or a BigInt which is a boxed integer with a header word and followed by the binary bignum digits.
    smallint,
    // An Atom is a Erlang data type represented by a hidden constant integer value assigned at
    // atom creation, and a constant string, atom's name, also assigned at creation. Since creation
    // atoms can be used in the program and internally their numerical value is passed, tagged
    // as Atom data type. An Atom hidden integer value can never leave an Erlang node, they
    // always are converted to a string first to find the new numerical value on the remote
    // host or the future host which will read and instantiate this atom value.
    atom,
    // A reference is a bit combination of current node id (in the cluster), node generation (starting from 0),
    // and current time and some monotonously increasing counter within that time. Reference inside one
    // Erlang node is guaranteed to be unique, and tries to also remain unique in a Erlang cluster.
    local_reference,
    // Represented as Port<X.Y> where X and Y are internal index in the internal port table, implementation
    // is free to define how these work.
    // A port is a unique handle to a resource open by a runtime driver (such as network socket or a file)
    // A port can be read from, written to via sending messages, and all that communication is handled by the
    // port driver to perform IO operations.
    local_port,
    // A process identifier <A.B.C> is a node-unique combination of sequentially increasing number (values
    // B, C split into 15 bits and remaining bits), and node number A (0 is local).
    local_pid,
    // A tuple is an array of Terms of fixed size, represented on heap as a header with arity, followed by
    // array of values. Tuples are immutable in language, but compiler is free to allow mutations.
    empty_tuple,
    // A map is a key/value structure of {term => term}
    empty_map,
    // A special value representing an empty list, fits in one Term
    empty_list,
    // Identify a heap object header rather than a term value.
    header,
    // Point to a cons cell containing a head and tail.
    list,
    // Point to a heap object whose header supplies its BoxedKind.
    boxed,
    // Identify an internal catch handler.
    catch_object,
    // Mark an invalid or unresolved tag category.
    invalid,
    // An admitted nonempty tuple; kind and extent come from its heap header.
    tuple,
    // A canonical sign/magnitude integer stored as private target-word limbs.
    bignum,
    // Immutable finite binary64 storage.
    floating,
    // A canonical immutable exact-key table.
    map,
    // Immutable packed bits: an inline heap binary or a view of an off-heap buffer.
    bitstring,
    // An OTP 29 native record: a captured definition and its field values.
    native_record,
    // A fun: a local function or anonymous fun with its captured values, or an external fun M:F/A.
    function,
    // A pid, port or reference of another node (plan 30 step DIST1, docs/terms.md#identities-of-other-nodes): a cell
    // holding the node's name and creation and the identity's numbers.
    external_pid,
    external_port,
    external_reference,
};

// Private object kinds distinguish layouts; these numeric IDs are provisional.
// The private BoxHeader reserves five bits for these kinds.
enum class BoxedKind : std::uint8_t {
    tuple = 0, // corresponds to BEAM VM constant ARITYVAL=0
    native_record = 1,
    bignum = 2, // Sign is stored in the implementation
    // Unused heap words skipped by the walker; the all-zero word (tuple, count 0) is a one-word filler.
    filler = 3,
    reference = 4,
    fun_closure = 5, // every fun: a captured definition followed by its captured values
    floating = 6,
    external_function = 7,
    refc_binary = 8, // a shared binary object owning its word vector
    heap_binary = 9, // a locally heap-contained data blob
    sub_binary = 10,
    match_context = 11, // something created by binary matching?
    // Identities of other nodes (plan 30 step DIST1): layout::ExternalIdentityCell.
    ext_pid = 12,
    ext_port = 13,
    ext_ref = 14,
    map = 15,
    // Tuple value of {} is an immediate like NIL is
    empty_tuple = 16,
    empty_list = 17,
    // A map above 32 entries: a stub naming the root node of its tree of entries (on-heap map trees).
    tree_map = 18,
    // A node of a map's tree, never a term of its own (on-heap map trees).
    map_node = 19,
    // This must fit in the five kind bits reserved by BoxHeader.
};

enum class TermKindPrimary : std::uint8_t {
    header = 0,
    // the rest of `Term` value bits are a pointer to a cons cell
    list = 1,
    // the rest of `Term` value bits are a pointer to a `BoxHeader` object, a boxed in memory
    // determined by the kind bits in that header
    boxed = 2,
    // if tag1 == see_termkind2, allows reading tag2
    see_termkind2 = 3,
};

enum class TermKind2 : std::uint8_t {
    pid = 0,           // if tag1 == see_termkind2
    port = 1,          // if tag1 == see_termkind2
    see_termkind3 = 2, // if tag1 == see_termkind2, and tag2 == see_termkind3, allows reading into tag3
    smallint = 3,      // if tag1 == see_termkind2
};

enum class TermKind3 : std::uint8_t {
    atom = 0,
    catch_object = 1,
    empty_tuple = 2, // {}
    empty_list = 3,  // also known as NIL or []
};

// Resolved tag categories; boxed values need their object header to determine a semantic TermKind.
// enum class TermTagKind : std::uint8_t {
//     header,
//     list,
//     boxed,
//     pid,
//     port,
//     smallint,
//     atom,
//     catch_object,
//     nil,
//     invalid,
//     empty_tuple,
// };
} // namespace clause::runtime
