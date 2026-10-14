#include "../../process/identities.hpp"
#include "../../terms/identities.hpp"
#include "../../terms/integers.hpp"
#include "../text.hpp"
#include "../typed.hpp"
#include "terms.hpp"
#include <algorithm>
#include <array>
#include <clause/abi/external_term.hpp>
#include <clause/runtime/atoms.hpp>
#include <clause/runtime/code_server.hpp>
#include <cmath>
#include <string>
#include <utility>
#include <vector>
#include <zlib.h>

// binary_to_term/1,2 (plan step 41E, docs/builtins.md#external-term-format): decodes iteratively, checking every
// length against the input before allocating; malformed input, identities of another node and pids this program
// never created are badarg, compressed input is inflated first (plan step 41Q). Bytes after the term are ignored, as in
// OTP.
namespace clause::runtime::builtins {
namespace {
namespace external = abi::external;

// The node every identity of a non-distributed program names.
constexpr std::string_view LOCAL_NODE = "nonode@nohost";

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
        const auto name = option.is_atom() ? need(option.atom_spelling()) : std::string_view{};
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

// A container being decoded: its kind, how many children it still needs and the children so far.
struct Open {
    external::ItemKind kind_;
    std::size_t remaining_;
    std::vector<Word> children_;
};

class Decoder {
  public:
    Decoder(ProcessContext &context, std::string_view bytes, DecodeOptions options)
        : context_(context), reader_(bytes), options_(options) {}

    // The decoded term; malformed input throws external::FormatError.
    Term decode() {
        reader_.version();
        while (true) {
            auto value = item(reader_.next());
            while (value) {
                if (open_.empty()) {
                    return *value;
                }
                value = add(*value);
            }
        }
    }

    std::size_t used() const { return reader_.offset(); }

  private:
    // A finished term, or none when the item opened a container.
    std::optional<Term> item(const external::Item &item) {
        using Kind = external::ItemKind;
        if (item.kind_ == Kind::tuple || item.kind_ == Kind::list || item.kind_ == Kind::map) {
            return open(item);
        }
        if (item.kind_ == Kind::pid || item.kind_ == Kind::port || item.kind_ == Kind::reference) {
            return identity(item);
        }
        if (item.kind_ == Kind::export_fun) {
            return export_fun();
        }
        return leaf(item);
    }

    // Start a container; an empty tuple or map is finished at once. A list also needs its tail.
    std::optional<Term> open(const external::Item &item) {
        const auto children = item.kind_ == external::ItemKind::map    ? item.count_ * 2
                              : item.kind_ == external::ItemKind::list ? item.count_ + 1
                                                                       : item.count_;
        if (children == 0) {
            return item.kind_ == external::ItemKind::tuple ? need(TermFactory(context_).tuple({}))
                                                           : need(TermFactory(context_).map({}));
        }
        open_.push_back({item.kind_, children, {}});
        open_.back().children_.reserve(std::min<std::size_t>(children, 4096));
        return std::nullopt;
    }

    // Give a finished term to the innermost container; returns that container once it is finished.
    std::optional<Term> add(const Term &value) {
        auto &top = open_.back();
        top.children_.push_back(value.word());
        if (--top.remaining_ != 0) {
            return std::nullopt;
        }
        auto finished = close(top);
        open_.pop_back();
        return finished;
    }

    // Build a finished container.
    Term close(const Open &open) {
        TermFactory factory(context_);
        if (open.kind_ == external::ItemKind::tuple) {
            return need(factory.tuple_words(open.children_));
        }
        if (open.kind_ == external::ItemKind::list) {
            const auto tail = need(Term::from_word(open.children_.back(), context_));
            return need(factory.list_words(std::span(open.children_).first(open.children_.size() - 1), tail));
        }
        return map(open.children_);
    }

    // A map of alternating keys and values; a repeated key is malformed.
    Term map(const std::vector<Word> &words) {
        std::vector<std::pair<Term, Term>> entries;
        for (std::size_t i = 0; i < words.size(); i += 2) {
            entries.emplace_back(need(Term::from_word(words[i], context_)),
                                 need(Term::from_word(words[i + 1], context_)));
        }
        const auto result = need(TermFactory(context_).map(entries));
        if (need(result.map_size()) != entries.size()) {
            reader_.fail("repeated map key");
        }
        return result;
    }

    Term leaf(const external::Item &item) {
        TermFactory factory(context_);
        switch (item.kind_) {
        case external::ItemKind::atom:
            return atom(item);
        case external::ItemKind::integer:
            return need(factory.integer(item.integer_));
        case external::ItemKind::big_integer:
            return big(item);
        case external::ItemKind::floating:
            return floating(item.float_);
        case external::ItemKind::string:
            return string(item.bytes_);
        case external::ItemKind::binary:
            return need(factory.bitstring(std::as_bytes(std::span(item.bytes_)), item.count_));
        default:
            return need(factory.nil());
        }
    }

    // A float; NaN and the infinities are malformed, as in OTP.
    Term floating(double value) {
        if (!std::isfinite(value)) {
            reader_.fail("non-finite float");
        }
        return need(TermFactory(context_).floating(value));
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

    Term string(std::string_view bytes) {
        std::vector<Word> words(bytes.size());
        std::ranges::transform(bytes, words.begin(), [](char byte) {
            return *abi::v1::NativeIntegerEncoding::encode(static_cast<unsigned char>(byte));
        });
        TermFactory factory(context_);
        return need(factory.list_words(words, need(factory.nil())));
    }

    // A pid, port or reference of the local node; another node's is badarg.
    Term identity(external::Item item) {
        const auto node = reader_.atom();
        reader_.identity(item);
        if (node.bytes_ != LOCAL_NODE) {
            bad_argument();
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

    // A reference of up to 3 words: the low 18 bits, the next 32 and the rest.
    Term reference(const external::Item &item) {
        if (item.count_ > 3 || item.identity_[0] >= (std::uint64_t{1} << 18)) {
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
        return need(TermFactory(context_).fun(definition, {}));
    }

    ProcessContext &context_;
    external::Reader reader_;
    DecodeOptions options_;
    // Containers still collecting children, the innermost last.
    std::vector<Open> open_;
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
} // namespace clause::runtime::builtins

namespace clause::runtime {
std::span<const BuiltinEntry> term_decoding_builtins() noexcept { return builtins::TERM_DECODING_BUILTINS; }
} // namespace clause::runtime
