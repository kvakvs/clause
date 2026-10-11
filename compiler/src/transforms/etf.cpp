// Added for parse transforms: compiler terms through the shared External Term Format codec, without recursion.
#include "etf.hpp"
#include "../preprocessor/value.hpp"
#include <clause/abi/external_term.hpp>
#include <clause/compiler/source.hpp>
#include <iterator>

namespace clause::transforms {
namespace {
namespace external = abi::external;

// Encode an integer: 64-bit values directly, others from their magnitude bytes.
void put_integer(external::Writer &writer, const Integer &value) {
    if (const auto small = small_integer(value)) {
        writer.integer(*small);
        return;
    }
    const auto number = decimal_number(value.decimal);
    std::string magnitude;
    boost::multiprecision::export_bits(boost::multiprecision::abs(number), std::back_inserter(magnitude), 8, false);
    writer.big_integer(number < 0, magnitude);
}

// The bytes of a proper list of byte values, so STRING_EXT encodes it as term_to_binary/1 does.
std::optional<std::string> byte_string(const Terms &terms, const TermNode &list) {
    if (list.improper_ || list.children_.empty() || list.children_.size() > external::STRING_LIMIT) {
        return std::nullopt;
    }
    std::string bytes;
    for (const auto child : list.children_) {
        const auto value = terms.small_integer(child);
        if (!value || *value < 0 || *value > static_cast<std::int64_t>(external::SMALL_LIMIT)) {
            return std::nullopt;
        }
        bytes.push_back(static_cast<char>(*value));
    }
    return bytes;
}

// One step of the encoder's work list: a term, or the NIL_EXT ending a proper list.
struct EncodeStep {
    TermId id_;
    bool nil_;
};

class Encoder {
  public:
    // Encode against one arena into the output buffer.
    Encoder(const Terms &terms, std::string &out) : terms_(terms), writer_(out) {}

    // Emit the term and everything below it in prefix order.
    void run(const TermId root) {
        writer_.version();
        pending_.push_back({.id_ = root, .nil_ = false});
        while (!pending_.empty()) {
            const auto step = pending_.back();
            pending_.pop_back();
            if (step.nil_) {
                writer_.nil();
            } else {
                emit(terms_.node(step.id_));
            }
        }
    }

  private:
    // The arena, the shared writer, and the steps still to emit (last one first).
    const Terms &terms_;
    external::Writer writer_;
    std::vector<EncodeStep> pending_;

    // Queue children so that the first one is emitted next.
    void queue(const std::vector<TermId> &children) {
        for (auto child = children.rbegin(); child != children.rend(); ++child) {
            pending_.push_back({.id_ = *child, .nil_ = false});
        }
    }

    // Emit one node's own bytes and queue its children.
    void emit(const TermNode &node) {
        switch (node.kind_) {
        case TermKind::atom:
            writer_.atom(utf8(node.atom_));
            return;
        case TermKind::integer:
            put_integer(writer_, node.integer_);
            return;
        case TermKind::floating:
            writer_.floating(node.float_);
            return;
        case TermKind::bits:
            writer_.binary(node.bytes_, node.bit_count_);
            return;
        case TermKind::list:
            emit_list(node);
            return;
        case TermKind::map:
            writer_.map(node.children_.size() / 2);
            break;
        default:
            writer_.tuple(node.children_.size());
        }
        queue(node.children_);
    }

    // Lists: NIL_EXT when empty, STRING_EXT for byte lists, else LIST_EXT with its tail.
    void emit_list(const TermNode &node) {
        if (node.children_.empty()) {
            writer_.nil();
            return;
        }
        if (const auto bytes = byte_string(terms_, node)) {
            writer_.string(*bytes);
            return;
        }
        writer_.list(node.children_.size() - (node.improper_ ? 1 : 0));
        if (!node.improper_) {
            pending_.push_back({.id_ = 0, .nil_ = true});
        }
        queue(node.children_);
    }
};

// A container whose children are still being decoded.
struct Open {
    external::ItemKind kind_;
    // Children still expected; a list counts its tail too.
    std::size_t remaining_;
    std::vector<TermId> children_;
};

class Decoder {
  public:
    // Decode from the bytes into the arena.
    Decoder(const std::string_view bytes, Terms &terms) : reader_(bytes), terms_(terms) {}

    // Decode the single term after the version byte and require the input to end there.
    TermId run() {
        reader_.version();
        std::optional<TermId> result;
        while (!result) {
            if (const auto leaf = next()) {
                result = deliver(*leaf);
            }
        }
        if (!reader_.done()) {
            reader_.fail("trailing bytes");
        }
        return *result;
    }

  private:
    // The shared reader, the arena and the containers being filled (innermost last).
    external::Reader reader_;
    Terms &terms_;
    std::vector<Open> open_;

    // Hand a finished term to the innermost open container, closing every container it completes.
    std::optional<TermId> deliver(TermId id) {
        while (!open_.empty()) {
            auto &top = open_.back();
            top.children_.push_back(id);
            if (--top.remaining_ != 0) {
                return std::nullopt;
            }
            id = close(top);
            open_.pop_back();
        }
        return id;
    }

    // Build a container whose children are complete.
    TermId close(Open &open) {
        if (open.kind_ == external::ItemKind::tuple) {
            return terms_.tuple(std::move(open.children_));
        }
        if (open.kind_ == external::ItemKind::map) {
            return terms_.map(std::move(open.children_));
        }
        const auto tail = open.children_.back();
        open.children_.pop_back();
        return terms_.list(std::move(open.children_), tail);
    }

    // Decode one item: a finished term, or nothing when it opened a container.
    std::optional<TermId> next() {
        const auto item = reader_.next();
        switch (item.kind_) {
        case external::ItemKind::tuple:
            return start(item.kind_, item.count_);
        case external::ItemKind::map:
            return start(item.kind_, item.count_ * 2);
        case external::ItemKind::list:
            return start(item.kind_, item.count_ + 1);
        default:
            return leaf(item);
        }
    }

    // Start a container of `count` children; an empty one is a finished term.
    std::optional<TermId> start(const external::ItemKind kind, const std::size_t count) {
        if (count == 0) {
            return kind == external::ItemKind::tuple ? terms_.tuple({}) : terms_.map({});
        }
        open_.push_back({.kind_ = kind, .remaining_ = count, .children_ = {}});
        return std::nullopt;
    }

    // Terms without children.
    TermId leaf(const external::Item &item) {
        switch (item.kind_) {
        case external::ItemKind::atom:
            return atom(item);
        case external::ItemKind::integer:
            return terms_.integer(item.integer_);
        case external::ItemKind::big_integer:
            return big(item);
        case external::ItemKind::floating:
            return terms_.floating(item.float_);
        case external::ItemKind::string:
            return string(item.bytes_);
        case external::ItemKind::binary:
            return terms_.bits(std::string(item.bytes_), item.count_);
        case external::ItemKind::nil:
            return terms_.nil();
        default:
            // Identities and funs never occur in abstract forms (they appear in the runtime's codec, step 41E).
            throw external::FormatError("external term format: unexpected identity or fun");
        }
    }

    // An atom spelled in UTF-8 or Latin-1.
    TermId atom(const external::Item &item) {
        if (item.latin1_) {
            std::u32string name;
            for (const auto byte : item.bytes_) {
                name.push_back(static_cast<std::uint8_t>(byte));
            }
            return terms_.atom(name);
        }
        try {
            return terms_.atom(Source(0, "atom", std::string(item.bytes_)).text);
        } catch (const EncodingError &) {
            reader_.fail("invalid UTF-8 atom");
        }
    }

    // A STRING_EXT list of bytes.
    TermId string(const std::string_view bytes) {
        std::vector<TermId> codes;
        codes.reserve(bytes.size());
        for (const auto byte : bytes) {
            codes.push_back(terms_.integer(static_cast<std::int64_t>(static_cast<std::uint8_t>(byte))));
        }
        return terms_.list(std::move(codes));
    }

    // A big integer within the compiler's integer limit.
    TermId big(const external::Item &item) {
        if (item.bytes_.size() * 8 > INTEGER_BIT_LIMIT) {
            reader_.fail("integer too large");
        }
        BigInt number;
        boost::multiprecision::import_bits(number, item.bytes_.begin(), item.bytes_.end(), 8, false);
        return terms_.integer(Integer{decimal_integer(item.negative_ ? BigInt(-number) : number)});
    }
};
} // namespace

std::string encode_external(const Terms &terms, const TermId id) {
    std::string out;
    Encoder(terms, out).run(id);
    return out;
}

TermId decode_external(const std::string_view bytes, Terms &terms) {
    try {
        return Decoder(bytes, terms).run();
    } catch (const abi::external::FormatError &error) {
        throw TermError(error.what());
    }
}
} // namespace clause::transforms
