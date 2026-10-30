#include "term_decoding.hpp"
#include "../../memory/heap_object.hpp"
#include "../../process/identities.hpp"
#include "../../terms/identities.hpp"
#include "../../terms/integers.hpp"
#include "../../terms/term_layout.hpp"
#include "../scratch.hpp"
#include "../text.hpp"
#include "../typed.hpp"
#include "terms.hpp"
#include <algorithm>
#include <array>
#include <clause/abi/equality.hpp>
#include <clause/abi/external_term.hpp>
#include <clause/runtime/atoms.hpp>
#include <clause/runtime/code_server.hpp>
#include <cmath>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <zlib.h>

// binary_to_term/1,2 (plan step 41E, docs/builtins.md#external-term-format): decodes iteratively, checking every
// length against the input before allocating; malformed input and pids of this node the program never created are
// badarg, identities of other nodes become their cells (plan 30 step DIST1), compressed input is inflated first (plan
// step 41Q). Bytes after the term are ignored, as in OTP.
namespace clause::runtime::builtins {
namespace {
namespace external = abi::external;

// The binary_to_term/2 options.
struct DecodeOptions {
    // safe: never create an atom; used: also return the bytes read.
    bool safe = false;
    bool used = false;
};

// The options of a proper list; badarg for any other option.
DecodeOptions decode_options(const ListArgument &list) {
    DecodeOptions options;
    for (const auto &option : list.elements) {
        const auto name = option.is_atom() ? option.atom_spelling_unchecked() : std::string_view{};
        if (name == "safe") {
            options.safe = true;
        } else if (name == "used") {
            options.used = true;
        } else {
            bad_argument();
        }
    }
    return options;
}

// A container being decoded: its kind, how many children it still needs and where its children start on the
// decoder's shared stack of children.
struct Open {
    external::ItemKind kind_;
    std::size_t remaining_;
    std::size_t first_;
};

class Decoder {
  public:
    Decoder(ProcessContext &context, std::string_view bytes, DecodeOptions options)
        : context_(context), factory_(context), reader_(bytes), options_(options) {}

    // The decoded term; malformed input throws external::FormatError. Terms pass between items as words: they are
    // this process's own, and nothing collects while the builtin runs (word-level decoding).
    Term decode() {
        reader_.version();
        while (true) {
            auto value = next();
            while (value) {
                if (open_->empty()) {
                    return need(Term::from_word(*value, context_));
                }
                value = add(*value);
            }
        }
    }

    std::size_t used() const { return reader_.offset(); }

  private:
    // The next finished term, or none when its item opened a container; small integers skip the Item (codec integer
    // fast path).
    std::optional<Word> next() {
        if (const auto value = reader_.next_integer()) {
            return integer(*value);
        }
        return item(reader_.next());
    }

    // A finished term, or none when the item opened a container.
    std::optional<Word> item(const external::Item &item) {
        using Kind = external::ItemKind;
        if (item.kind_ == Kind::tuple || item.kind_ == Kind::list || item.kind_ == Kind::map) {
            return open(item);
        }
        if (item.kind_ == Kind::pid || item.kind_ == Kind::port || item.kind_ == Kind::reference) {
            return identity(item).word();
        }
        if (item.kind_ == Kind::export_fun) {
            return export_fun().word();
        }
        return leaf(item);
    }

    // Start a container; an empty tuple or map is finished at once. A list also needs its tail.
    std::optional<Word> open(const external::Item &item) {
        const auto children = item.kind_ == external::ItemKind::map    ? item.count_ * 2
                              : item.kind_ == external::ItemKind::list ? item.count_ + 1
                                                                       : item.count_;
        if (children == 0) {
            return item.kind_ == external::ItemKind::tuple ? need(factory_.tuple({})).word()
                                                           : need(factory_.map({})).word();
        }
        open_->push_back({item.kind_, children, children_->size()});
        return std::nullopt;
    }

    // Give a finished term to the innermost container; returns that container once it is finished.
    std::optional<Word> add(Word value) {
        children_->push_back(value);
        auto &top = open_->back();
        if (--top.remaining_ != 0) {
            return std::nullopt;
        }
        const auto first = top.first_;
        const auto finished = close(top.kind_, std::span(*children_).subspan(first));
        children_->resize(first);
        open_->pop_back();
        return finished;
    }

    // Build a finished container from children this decoder made, so they are not admitted again.
    Word close(external::ItemKind kind, std::span<const Word> children) {
        if (kind == external::ItemKind::tuple) {
            return tuple(children);
        }
        if (kind == external::ItemKind::list) {
            return list(children.first(children.size() - 1), children.back());
        }
        return map(children).word();
    }

    // A tuple of element words, written straight into claimed heap words (decoded containers).
    Word tuple(std::span<const Word> elements) {
        const auto words = detail::TermAccess::claim(context_.heap(), elements.size() + 1);
        if (words.empty()) {
            return need(factory_.tuple_of_words(elements)).word();
        }
        words[0] = detail::layout::BoxHeader::make(BoxedKind::tuple, elements.size());
        std::ranges::copy(elements, words.begin() + 1);
        return reinterpret_cast<Word>(words.data()) | static_cast<Word>(TermKindPrimary::boxed);
    }

    // A list of element words onto `tail`, its cells written straight into claimed heap words (decoded containers).
    Word list(std::span<const Word> elements, Word tail) {
        if (elements.empty()) {
            return tail;
        }
        const auto words = detail::TermAccess::claim(context_.heap(), 2 * elements.size());
        if (words.empty()) {
            return need(factory_.list_of_words(elements, need(Term::from_word(tail, context_)))).word();
        }
        for (std::size_t i = 0; i < elements.size(); ++i) {
            words[2 * i] = elements[i];
            words[2 * i + 1] = i + 1 == elements.size() ? tail
                                                        : reinterpret_cast<Word>(&words[2 * i + 2]) |
                                                              static_cast<Word>(TermKindPrimary::list);
        }
        return reinterpret_cast<Word>(words.data()) | static_cast<Word>(TermKindPrimary::list);
    }

    // A map of alternating keys and values; a repeated key is malformed.
    Term map(std::span<const Word> words) {
        std::vector<std::pair<Term, Term>> entries;
        for (std::size_t i = 0; i < words.size(); i += 2) {
            entries.emplace_back(need(Term::from_word(words[i], context_)),
                                 need(Term::from_word(words[i + 1], context_)));
        }
        const auto result = need(factory_.map(entries));
        if (need(result.map_size()) != entries.size()) {
            reader_.fail("repeated map key");
        }
        return result;
    }

    Word leaf(const external::Item &item) {
        switch (item.kind_) {
        case external::ItemKind::atom:
            return atom_word(item);
        case external::ItemKind::integer:
            return integer(item.integer_);
        case external::ItemKind::big_integer:
            return big(item).word();
        case external::ItemKind::floating:
            return floating(item.float_).word();
        case external::ItemKind::string:
            return string(item.bytes_);
        case external::ItemKind::binary:
            return need(factory_.bitstring(std::as_bytes(std::span(item.bytes_)), item.count_)).word();
        default:
            return need(factory_.nil()).word();
        }
    }

    // A small integer as its word, a larger one on the heap (word-level decoding).
    Word integer(std::int64_t value) {
        if (const auto word = abi::v1::NativeIntegerEncoding::encode(value)) {
            return *word;
        }
        return need(factory_.integer(value)).word();
    }

    // An atom by its encoded name, decoded and interned once per decoding (word-level decoding).
    Word atom_word(const external::Item &item) {
        auto key = std::string(1, item.latin1_ ? 'L' : 'U');
        key += item.bytes_;
        auto found = atoms_.find(key);
        if (found == atoms_.end()) {
            found = atoms_.emplace(std::move(key), atom(item).word()).first;
        }
        return found->second;
    }

    // A float; NaN and the infinities are malformed, as in OTP.
    Term floating(double value) {
        if (!std::isfinite(value)) {
            reader_.fail("non-finite float");
        }
        return need(factory_.floating(value));
    }

    // The UTF-8 name of an atom item: invalid UTF-8 or more than 255 characters is malformed.
    std::string atom_name(const external::Item &item) {
        std::u32string codes;
        if (item.latin1_) {
            std::ranges::transform(item.bytes_, std::back_inserter(codes),
                                   [](char byte) { return static_cast<char32_t>(static_cast<unsigned char>(byte)); });
        } else if (const auto decoded = decode_utf8(std::as_bytes(std::span(item.bytes_)))) {
            codes = *decoded;
        } else {
            reader_.fail("invalid atom text");
        }
        if (codes.size() > 255) {
            reader_.fail("atom too long");
        }
        return utf8(codes);
    }

    // An atom; with safe, only an existing one. A full atom table is system_limit.
    Term atom(const external::Item &item) {
        const auto name = atom_name(item);
        if (options_.safe) {
            const auto existing = context_.atom_storage().existing(name);
            if (!existing) {
                bad_argument();
            }
            return *existing;
        }
        const auto atom = context_.atom_storage().intern(name);
        if (!atom && atom.error() == TermError::resource_limit) {
            throw BuiltinFailure{abi::v1::ErrorReason::system_limit};
        }
        if (!atom) {
            bad_argument();
        }
        return *atom;
    }

    // A big integer from its little-endian magnitude; past the integer limit is malformed.
    Term big(const external::Item &item) {
        detail::Integer value;
        boost::multiprecision::import_bits(value, item.bytes_.begin(), item.bytes_.end(), 8, false);
        if (detail::integer_bits(value) > detail::integer_bit_limit) {
            reader_.fail("integer too large");
        }
        return need(detail::IntegerAccess::make(context_.heap(), item.negative_ ? -value : value));
    }

    // STRING_EXT: a list of byte values, its cells written straight into claimed heap words (decoded containers).
    Word string(std::string_view bytes) {
        const auto byte = [](char value) {
            return *abi::v1::NativeIntegerEncoding::encode(static_cast<unsigned char>(value));
        };
        if (bytes.empty()) {
            return abi::v1::empty_list;
        }
        const auto words = detail::TermAccess::claim(context_.heap(), 2 * bytes.size());
        if (words.empty()) {
            std::vector<Word> elements(bytes.size());
            std::ranges::transform(bytes, elements.begin(), byte);
            return list(elements, abi::v1::empty_list);
        }
        for (std::size_t i = 0; i < bytes.size(); ++i) {
            words[2 * i] = byte(bytes[i]);
            words[2 * i + 1] = i + 1 == bytes.size() ? abi::v1::empty_list
                                                     : reinterpret_cast<Word>(&words[2 * i + 2]) |
                                                           static_cast<Word>(TermKindPrimary::list);
        }
        return reinterpret_cast<Word>(words.data()) | static_cast<Word>(TermKindPrimary::list);
    }

    // A pid, port or reference: this node's when it names this node with its creation or creation 0, as OTP
    // decides, else another node's (plan 30 step DIST1).
    Term identity(external::Item item) {
        const auto node = atom(reader_.atom());
        reader_.identity(item);
        if (node.atom_spelling_unchecked() != detail::this_node_name() ||
            (item.creation_ != 0 && item.creation_ != detail::this_node_creation())) {
            return external_identity(item, node);
        }
        if (item.kind_ == external::ItemKind::reference) {
            return reference(item);
        }
        const auto word = item.kind_ == external::ItemKind::pid
                              ? detail::pid_word(static_cast<Word>(item.identity_[1] << 28U | item.identity_[0]))
                              : detail::port_word(static_cast<Word>(item.identity_[0]));
        const auto term = Term::from_word(word, context_);
        if (!term || item.identity_[0] >= (std::uint64_t{1} << 28)) {
            bad_argument();
        }
        return *term;
    }

    // An identity of another node, its numbers kept as read (plan 30 step DIST1).
    Term external_identity(const external::Item &item, const Term &node) {
        detail::ExternalIdentity identity;
        identity.node = node.word();
        identity.creation = item.creation_;
        const auto number = [&](std::size_t index) { return static_cast<std::uint32_t>(item.identity_.at(index)); };
        if (item.kind_ == external::ItemKind::reference) {
            identity.kind = TermKind::external_reference;
            identity.count = item.count_;
            for (std::size_t index = 0; index < item.count_; ++index) {
                identity.numbers.at(index) = number(index);
            }
        } else {
            identity.kind = item.kind_ == external::ItemKind::pid ? TermKind::external_pid : TermKind::external_port;
            identity.count = 2;
            identity.numbers = {number(0), item.kind_ == external::ItemKind::pid
                                               ? number(1)
                                               : static_cast<std::uint32_t>(item.identity_[0] >> 32U)};
        }
        return need(detail::ExternalAccess::make(context_.heap(), identity));
    }

    // A reference of 1 to 3 words: the low 18 bits, the next 32 and the rest.
    Term reference(const external::Item &item) {
        if (item.count_ == 0 || item.count_ > 3 || item.identity_[0] >= (std::uint64_t{1} << 18)) {
            bad_argument();
        }
        const auto number = item.identity_[2] << 50U | item.identity_[1] << 18U | item.identity_[0];
        return need(detail::HandleAccess::reference(context_, number));
    }

    // fun M:F/A.
    Term export_fun() {
        const auto module = atom(reader_.atom());
        const auto function = atom(reader_.atom());
        const auto arity = reader_.next();
        if (arity.kind_ != external::ItemKind::integer || arity.integer_ < 0 || arity.integer_ > 255) {
            reader_.fail("invalid fun arity");
        }
        const auto &definition =
            context_.code_server().external_fun(module, function, static_cast<std::size_t>(arity.integer_));
        return need(factory_.fun(definition, {}));
    }

    ProcessContext &context_;
    // One factory for the whole decoding: binding one costs a lifetime check (word-level decoding).
    TermFactory factory_;
    external::Reader reader_;
    DecodeOptions options_;
    // Containers still collecting children, the innermost last, and their children so far in one stack.
    // Both are kept by the thread between decodings (codec scratch reuse).
    Scratch<std::vector<Open>, struct OpenSlot> open_;
    Scratch<std::vector<Word>, struct ChildrenSlot> children_;
    // Atom words by encoding ('L' or 'U', then the name bytes), interned once per decoding.
    std::unordered_map<std::string, Word> atoms_;
};

// The size OTP accepts for decompressed term data, against corrupt sizes.
constexpr std::size_t MAX_DECOMPRESSED = std::size_t{32} * 1024 * 1024;

// <<131, Payload/binary>> of <<131, 80, Size:32, Zlib/binary>>, and the input bytes the compressed form used; badarg
// unless zlib gives exactly Size bytes (plan step 41Q).
std::pair<std::string, std::size_t> decompressed(std::string_view bytes) {
    if (bytes.size() < 6) {
        bad_argument();
    }
    std::size_t size = 0;
    for (std::size_t at = 2; at < 6; ++at) {
        size = (size << 8) | static_cast<unsigned char>(bytes[at]);
    }
    if (size > MAX_DECOMPRESSED) {
        bad_argument();
    }
    std::string out(1 + size, static_cast<char>(0));
    out[0] = static_cast<char>(external::VERSION);
    z_stream z{};
    z.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(bytes.data() + 6));
    z.avail_in = static_cast<uInt>(bytes.size() - 6);
    z.next_out = reinterpret_cast<Bytef *>(out.data() + 1);
    z.avail_out = static_cast<uInt>(size);
    const bool done = inflateInit(&z) == Z_OK && inflate(&z, Z_FINISH) == Z_STREAM_END && z.total_out == size;
    const auto used = 6 + static_cast<std::size_t>(z.total_in);
    inflateEnd(&z);
    if (!done) {
        bad_argument();
    }
    return {std::move(out), used};
}

// Decode `binary`, adding {Term, Used} when asked.
TermResult<Term> decode(ProcessContext &context, const BinaryArgument &binary, DecodeOptions options) {
    std::string_view bytes(reinterpret_cast<const char *>(binary.bytes.data()), binary.bytes.size());
    std::pair<std::string, std::size_t> inflated;
    if (bytes.size() > 1 && static_cast<unsigned char>(bytes[1]) == external::COMPRESSED &&
        static_cast<unsigned char>(bytes[0]) == external::VERSION) {
        inflated = decompressed(bytes);
        bytes = inflated.first;
    }
    try {
        Decoder decoder(context, bytes, options);
        auto term = decoder.decode();
        if (!options.used) {
            return term;
        }
        const auto used = inflated.first.empty() ? decoder.used() : inflated.second;
        const std::array parts{term, need(TermFactory(context).integer(static_cast<std::int64_t>(used)))};
        return TermFactory(context).tuple(parts);
    } catch (const external::FormatError &) {
        bad_argument();
    }
}

TermResult<Term> binary_to_term1(ProcessContext &context, const BinaryArgument &binary) {
    return decode(context, binary, {});
}

TermResult<Term> binary_to_term2(ProcessContext &context, const BinaryArgument &binary, const ListArgument &options) {
    return decode(context, binary, decode_options(options));
}

constexpr std::array TERM_DECODING_BUILTINS{
    typed_entry<binary_to_term1>("erlang", "binary_to_term"),
    typed_entry<binary_to_term2>("erlang", "binary_to_term"),
};
} // namespace

Term decode_external(ProcessContext &context, std::string_view bytes) {
    try {
        Decoder decoder(context, bytes, {});
        return decoder.decode();
    } catch (const external::FormatError &) {
        bad_argument();
    }
}
} // namespace clause::runtime::builtins

namespace clause::runtime {
std::span<const BuiltinEntry> term_decoding_builtins() noexcept { return builtins::TERM_DECODING_BUILTINS; }
} // namespace clause::runtime
