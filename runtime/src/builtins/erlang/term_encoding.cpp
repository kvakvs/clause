#include "../../memory/heap_walk.hpp"
#include "../../process/identities.hpp"
#include "../../terms/atom_table.hpp"
#include "../../terms/funs.hpp"
#include "../../terms/identities.hpp"
#include "../../terms/integers.hpp"
#include "../../terms/structural_order.hpp"
#include "../../terms/term_layout.hpp"
#include "../portions.hpp"
#include "../text.hpp"
#include "../typed.hpp"
#include "terms.hpp"
#include <algorithm>
#include <array>
#include <clause/abi/external_term.hpp>
#include <clause/runtime/atoms.hpp>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <zlib.h>

// term_to_binary/1,2, term_to_iovec/1,2 and external_size/1,2 (plan step 41E, docs/builtins.md#external-term-format):
// OTP's encoding of a local, non-distributed node, walked iteratively with an explicit stack. `compressed` zlib-
// compresses the encoding as OTP does when the result is smaller (plan step 41Q); a local fun or a native record
// raises notsup.
namespace clause::runtime::builtins {
namespace {
namespace external = abi::external;

// The node every identity of a non-distributed program names.
constexpr std::string_view LOCAL_NODE = "nonode@nohost";

// The zlib level of the plain `compressed` option, as OTP's.
constexpr int Z_DEFAULT_LEVEL = 6;

// The term_to_binary/2 options that change the bytes.
struct EncodeOptions {
    // 2 writes UTF-8 atoms; 1 and 0 Latin-1 atoms where possible; 0 also writes floats as text.
    int minor = 2;
    // The zlib level of `compressed` (6) or {compressed, Level}; none leaves the encoding as it is.
    std::optional<int> compression = std::nullopt;
};

// One {Name, Value} option of term_to_binary/2: {minor_version, 0..2} or {compressed, 0..9}.
bool pair_option(std::string_view name, std::int64_t value, EncodeOptions &options) {
    if (name == "minor_version" && value >= 0 && value <= 2) {
        options.minor = static_cast<int>(value);
        return true;
    }
    if (name == "compressed" && value >= 0 && value <= 9) {
        options.compression = static_cast<int>(value);
        return true;
    }
    return false;
}

// One term_to_binary/2 option, applied to `options`; false for anything OTP rejects.
bool encode_option(const Term &option, EncodeOptions &options) {
    if (option.is_atom()) {
        const auto name = option.atom_spelling_unchecked();
        if (name == "compressed") {
            options.compression = Z_DEFAULT_LEVEL;
        }
        return name == "compressed" || name == "deterministic";
    }
    if (!option.is_tuple() || option.tuple_size_unchecked() != 2) {
        return false;
    }
    const auto name = option.tuple_element_unchecked(0).atom_spelling();
    const auto value = small(option.tuple_element_unchecked(1).word());
    return name && value && pair_option(*name, *value, options);
}

// The options of a proper list; badarg for an improper list or an invalid option.
EncodeOptions encode_options(const ListArgument &list) {
    EncodeOptions options;
    if (!std::ranges::all_of(list.elements, [&](const Term &option) { return encode_option(option, options); })) {
        bad_argument();
    }
    return options;
}

// Raise error:notsup for a term this codec cannot encode.
[[noreturn]] void unsupported(ProcessContext &context) {
    throw BuiltinFailure{.reason = abi::v1::ErrorReason::raised_error,
                         .payload = need(TermFactory(context).atom("notsup")).word()};
}

class Encoder {
  public:
    Encoder(ProcessContext &context, EncodeOptions options, std::string &out)
        : context_(context), options_(options), writer_(out) {}

    // Encode `root` after the version byte.
    void encode(const Term &root) {
        writer_.version();
        pending_.push_back(root.word());
        while (!pending_.empty()) {
            const auto word = pending_.back();
            pending_.pop_back();
            one(word);
        }
    }

  private:
    // Write one term's tag; its children are pushed to be written next, in order. Small integers, atoms, lists and
    // tuples are read as words: a builtin is a critical section, so no collection moves them (word-level encoding).
    void one(Word word) {
        if (const auto value = small(word)) {
            writer_.integer(*value);
            return;
        }
        switch (TermTag{word}.get_kind()) {
        case TermKind::atom:
            atom_word(word);
            return;
        case TermKind::empty_list:
            writer_.nil();
            return;
        case TermKind::empty_tuple:
            writer_.tuple(0);
            return;
        case TermKind::list:
            list(word);
            return;
        default:
            if (!tuple(word)) {
                term(term_of(context_, word));
            }
        }
    }

    // The terms read through Term: big integers, floats, maps, bitstrings and identities.
    void term(const Term &term) {
        if (term.is_integer()) {
            integer(term);
        } else if (term.is_float()) {
            floating(term.float_value_unchecked());
        } else if (term.is_map()) {
            map(term);
        } else if (term.is_bitstring()) {
            const auto bytes = need(term.bitstring_bytes());
            writer_.binary({reinterpret_cast<const char *>(bytes.data()), bytes.size()}, term.bit_size_unchecked());
        } else {
            identity(term);
        }
    }

    // An atom by its word, its spelling read from the atom table (word-level encoding, atom table readers).
    void atom_word(Word word) {
        const auto spelling = detail::atom_table::spelling(word);
        if (!spelling) {
            throw BuiltinFailure{.term = TermError::expired_context};
        }
        atom(*spelling);
    }

    // A tuple's header and its elements, read in place (trusted walks); false when the word names no tuple.
    bool tuple(Word word) {
        if ((word & abi::v1::primary_mask) != abi::v1::boxed_tag) {
            return false;
        }
        const auto *cell = detail::TermAccess::trusted(word);
        if (!detail::is_header(cell[0]) || detail::layout::BoxHeader::kind(cell[0]) != BoxedKind::tuple) {
            return false;
        }
        const std::span words(cell, 1 + detail::layout::BoxHeader::count(cell[0]));
        writer_.tuple(words.size() - 1);
        pending_.insert(pending_.end(), words.rbegin(), std::prev(words.rend()));
        return true;
    }

    void integer(const Term &term) {
        const auto value = need(detail::integer_read(term));
        if (value >= std::numeric_limits<std::int64_t>::min() && value <= std::numeric_limits<std::int64_t>::max()) {
            writer_.integer(value.convert_to<std::int64_t>());
            return;
        }
        std::string magnitude;
        boost::multiprecision::export_bits(abs(value), std::back_inserter(magnitude), 8, false);
        writer_.big_integer(value < 0, magnitude);
    }

    void floating(double value) {
        if (options_.minor == 0) {
            writer_.float_text(value);
        } else {
            writer_.floating(value);
        }
    }

    // UTF-8 atoms in minor version 2; ATOM_EXT for Latin-1 spellings in minor versions 0 and 1.
    void atom(std::string_view spelling) {
        std::string latin1;
        const auto codes = code_points(spelling);
        const bool narrow = std::ranges::all_of(codes, [](char32_t code) { return code <= 255; });
        if (options_.minor == 2 || !narrow) {
            writer_.atom(spelling);
            return;
        }
        std::ranges::transform(codes, std::back_inserter(latin1),
                               [](char32_t code) { return static_cast<char>(code); });
        writer_.atom_latin1(latin1);
    }

    // STRING_EXT for a proper list of at most 65535 bytes, else LIST_EXT and its tail, its cells read as words.
    void list(Word word) {
        elements_.clear();
        auto rest = word;
        while (TermTag{rest}.get_kind() == TermKind::list) {
            const auto *cell = detail::TermAccess::trusted(rest);
            elements_.push_back(cell[0]);
            rest = cell[1];
        }
        const auto byte = [](Word element) {
            const auto value = small(element);
            return value && *value >= 0 && *value <= 255;
        };
        if (TermTag{rest}.get_kind() == TermKind::empty_list && elements_.size() <= external::STRING_LIMIT &&
            std::ranges::all_of(elements_, byte)) {
            std::string bytes;
            std::ranges::transform(elements_, std::back_inserter(bytes),
                                   [](Word element) { return static_cast<char>(*small(element)); });
            writer_.string(bytes);
            return;
        }
        writer_.list(elements_.size());
        pending_.push_back(rest);
        pending_.insert(pending_.end(), elements_.rbegin(), elements_.rend());
    }

    // A map's pairs in key order, as OTP writes maps of up to 32 keys.
    void map(const Term &term) {
        auto entries = need(term.map_entries());
        std::ranges::sort(entries, [](const auto &left, const auto &right) {
            return need(detail::structural_order(left.first, right.first, true)) < 0;
        });
        writer_.map(entries.size());
        for (auto entry = entries.rbegin(); entry != entries.rend(); ++entry) {
            pending_.push_back(entry->second.word());
            pending_.push_back(entry->first.word());
        }
    }

    // Pids, ports, references and external funs; anything else is unsupported.
    void identity(const Term &term) {
        if (term.is_pid()) {
            const auto number = detail::pid_number(term.word());
            writer_.pid();
            atom(LOCAL_NODE);
            writer_.identity_word(static_cast<std::uint32_t>(number & 0x0FFF'FFFFU));
            writer_.identity_word(static_cast<std::uint32_t>(number >> 28U));
            writer_.creation(0);
        } else if (term.is_port()) {
            writer_.port();
            atom(LOCAL_NODE);
            writer_.port_number(detail::port_number(term.word()));
            writer_.creation(0);
        } else if (term.is_reference()) {
            reference(need(detail::reference_number(term)));
        } else {
            fun(term);
        }
    }

    // A reference's 3 words: the low 18 bits, the next 32 and the rest.
    void reference(std::uint64_t number) {
        writer_.reference(3);
        atom(LOCAL_NODE);
        writer_.creation(0);
        writer_.identity_word(static_cast<std::uint32_t>(number & 0x3FFFFU));
        writer_.identity_word(static_cast<std::uint32_t>((number >> 18U) & 0xFFFF'FFFFU));
        writer_.identity_word(static_cast<std::uint32_t>(number >> 50U));
    }

    // fun M:F/A; a local fun has no portable encoding here.
    void fun(const Term &term) {
        const auto view = term.is_function() ? detail::fun_view(term) : std::unexpected(TermError::wrong_type);
        if (!view || !view->definition->external) {
            unsupported(context_);
        }
        const auto &atoms = context_.atom_storage();
        writer_.export_fun();
        atom(need(need(atoms.lookup(view->definition->module)).atom_spelling()));
        atom(need(need(atoms.lookup(view->definition->function)).atom_spelling()));
        writer_.integer(static_cast<std::int64_t>(view->definition->arity));
    }

    ProcessContext &context_;
    EncodeOptions options_;
    external::Writer writer_;
    // Words still to write, the next one last; this process's terms, unmoved while the builtin runs.
    std::vector<Word> pending_;
    // The elements of the list being written, reused across lists.
    std::vector<Word> elements_;
};

// The compressed form <<131, 80, Size:32, Zlib/binary>> of an encoding when zlib fits it in five bytes fewer than the
// payload, as OTP decides; otherwise the encoding unchanged.
std::string compressed(std::string encoding, int level) {
    const auto payload = std::string_view(encoding).substr(1);
    if (payload.size() < 5) {
        return encoding;
    }
    std::string out(6 + payload.size() - 5, '\0');
    auto size = static_cast<uLongf>(payload.size() - 5);
    if (compress2(reinterpret_cast<Bytef *>(out.data() + 6), &size, reinterpret_cast<const Bytef *>(payload.data()),
                  static_cast<uLong>(payload.size()), level) != Z_OK) {
        return encoding;
    }
    out[0] = static_cast<char>(external::VERSION);
    out[1] = static_cast<char>(external::COMPRESSED);
    for (int shift = 3; shift >= 0; --shift) {
        out[static_cast<std::size_t>(5 - shift)] = static_cast<char>((payload.size() >> (8 * shift)) & 0xFFU);
    }
    out.resize(6 + size);
    return out;
}

// The encoding of `term` with `options`.
std::string encoding(ProcessContext &context, const Term &term, EncodeOptions options = {}) {
    std::string bytes;
    Encoder(context, options, bytes).encode(term);
    return bytes;
}

// A binary of encoded bytes.
TermResult<Term> binary(ProcessContext &context, const std::string &bytes) {
    return TermFactory(context).binary({reinterpret_cast<const std::byte *>(bytes.data()), bytes.size()});
}

TermResult<Term> term_to_binary1(ProcessContext &context, const Term &term) {
    return binary(context, encoding(context, term));
}

TermResult<Term> term_to_binary2(ProcessContext &context, const Term &term, const ListArgument &options) {
    const auto chosen = encode_options(options);
    auto bytes = encoding(context, term, chosen);
    if (chosen.compression) {
        bytes = compressed(std::move(bytes), *chosen.compression);
    }
    return binary(context, bytes);
}

// term_to_iovec/1,2: the encoding as a list of one binary.
TermResult<Term> term_to_iovec1(ProcessContext &context, const Term &term) {
    const std::array parts{need(term_to_binary1(context, term))};
    return TermFactory(context).list(parts);
}

TermResult<Term> term_to_iovec2(ProcessContext &context, const Term &term, const ListArgument &options) {
    const std::array parts{need(term_to_binary2(context, term, options))};
    return TermFactory(context).list(parts);
}

// external_size/1,2: the encoding's exact size.
TermResult<Term> external_size1(ProcessContext &context, const Term &term) {
    return TermFactory(context).integer(static_cast<std::int64_t>(encoding(context, term).size()));
}

TermResult<Term> external_size2(ProcessContext &context, const Term &term, const ListArgument &options) {
    const auto size = encoding(context, term, encode_options(options)).size();
    return TermFactory(context).integer(static_cast<std::int64_t>(size));
}

constexpr std::array TERM_ENCODING_BUILTINS{
    typed_entry<term_to_binary1>("erlang", "term_to_binary"), typed_entry<term_to_binary2>("erlang", "term_to_binary"),
    typed_entry<term_to_iovec1>("erlang", "term_to_iovec"),   typed_entry<term_to_iovec2>("erlang", "term_to_iovec"),
    typed_entry<external_size1>("erlang", "external_size"),   typed_entry<external_size2>("erlang", "external_size"),
};
} // namespace
} // namespace clause::runtime::builtins

namespace clause::runtime {
std::span<const BuiltinEntry> term_encoding_builtins() noexcept { return builtins::TERM_ENCODING_BUILTINS; }
} // namespace clause::runtime
