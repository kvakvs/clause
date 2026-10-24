#include "literal_heap.hpp"
#include "../memory/persistent_terms.hpp"
#include "../terms/integers.hpp"
#include "literal_areas.hpp"
#include <clause/abi/literals.hpp>
#include <clause/abi/term.hpp>
#include <new>
#include <string_view>
#include <utility>
#include <vector>

// The literal heap: each runtime decodes every registered module's literals once, at registration, on a scratch
// context, then copies them into an area of its persistent term range that is never retired, so terms of every
// process read them in place, the collector leaves them and copies share them, as module literals of BEAM files.
namespace clause::runtime {
namespace {
using abi::literals::Extended;
using abi::literals::FormatError;
using abi::literals::Item;
using abi::literals::Tag;

// A term construction that failed while decoding.
struct ConstructionFailure {
    TermError error;
};

Term need(TermResult<Term> result) {
    if (!result) {
        throw ConstructionFailure{result.error()};
    }
    return *result;
}

// Decodes the literals of one module on a context's heap, its atoms bound to the runtime's atom words.
class LiteralDecoder final {
  public:
    LiteralDecoder(ProcessContext &context, std::span<const Term> atoms, const std::string_view bytes)
        : context_(context), factory_(context), atoms_(atoms), reader_(bytes) {}

    Term next() { return term(reader_.next()); }

    bool done() const { return reader_.done(); }

  private:
    Term term(const Item &item) {
        switch (item.tag) {
        case Tag::integer:
            return integer(item);
        case Tag::atom:
            return atom(item.value);
        case Tag::extended:
            return extended(item.extended);
        default:
            throw FormatError("unexpected literal tag");
        }
    }

    Term integer(const Item &item) {
        if (!item.large) {
            return need(factory_.integer(static_cast<std::int64_t>(item.value)));
        }
        std::vector<std::uint8_t> bytes(item.bytes.begin(), item.bytes.end());
        const bool negative = (bytes.front() & 0x80U) != 0;
        if (bytes.size() <= 8) {
            auto value = negative ? ~std::uint64_t{0} : std::uint64_t{0};
            for (const auto byte : bytes) {
                value = (value << 8U) | byte;
            }
            return need(factory_.integer(static_cast<std::int64_t>(value)));
        }
        detail::Integer value;
        boost::multiprecision::import_bits(value, bytes.begin(), bytes.end(), 8, true);
        if (negative) {
            value -= detail::Integer(1) << (8 * bytes.size());
        }
        return need(detail::IntegerAccess::make(context_.heap(), value));
    }

    Term atom(const std::uint64_t value) {
        if (value == 0) {
            return need(factory_.nil());
        }
        if (value > atoms_.size()) {
            throw FormatError("atom slot out of range");
        }
        return atoms_[value - 1];
    }

    Term extended(const Extended tag) {
        switch (tag) {
        case Extended::list:
            return list();
        case Extended::tuple:
            return need(factory_.tuple(terms(reader_.unsigned_count())));
        case Extended::floating:
            return need(factory_.floating(reader_.floating()));
        case Extended::bitstring:
            return bitstring();
        case Extended::map:
            return map();
        case Extended::string:
            return string();
        default:
            throw FormatError("unexpected extended literal tag");
        }
    }

    // The next `count` terms.
    std::vector<Term> terms(const std::uint64_t count) {
        std::vector<Term> result;
        result.reserve(count);
        for (std::uint64_t i = 0; i < count; ++i) {
            result.push_back(next());
        }
        return result;
    }

    Term list() {
        const auto elements = terms(reader_.unsigned_count());
        return need(factory_.list(elements, next()));
    }

    Term bitstring() {
        const auto bits = reader_.unsigned_count();
        const auto bytes = reader_.bytes((bits + 7) / 8);
        return need(factory_.bitstring(std::as_bytes(std::span(bytes.data(), bytes.size())), bits));
    }

    Term map() {
        const auto pairs = reader_.unsigned_count();
        std::vector<std::pair<Term, Term>> entries;
        entries.reserve(pairs);
        for (std::uint64_t i = 0; i < pairs; ++i) {
            auto key = next();
            entries.emplace_back(key, next());
        }
        return need(factory_.map(entries));
    }

    Term string() {
        std::vector<Term> elements;
        for (const auto byte : reader_.bytes(reader_.unsigned_count())) {
            elements.push_back(need(factory_.integer(static_cast<std::uint8_t>(byte))));
        }
        return need(factory_.list(elements));
    }

    ProcessContext &context_;
    TermFactory factory_;
    // The module's atoms by slot.
    std::span<const Term> atoms_;
    abi::literals::Reader reader_;
};

// Decode every literal into one tuple on the scratch context, copy it into the literal heap and keep its elements.
void decode(Runtime &runtime, ProcessContext &scratch, const abi::v1::ModuleDescriptor &descriptor,
            ModuleAtoms &bindings) {
    LiteralDecoder decoder(scratch, bindings.slots,
                           {reinterpret_cast<const char *>(descriptor.literals), descriptor.literal_bytes});
    std::vector<Term> terms;
    terms.reserve(descriptor.literal_count);
    for (std::size_t i = 0; i < descriptor.literal_count; ++i) {
        terms.push_back(decoder.next());
    }
    if (!decoder.done()) {
        throw FormatError("trailing literal bytes");
    }
    const auto all = need(TermFactory(scratch).tuple(terms));
    auto &region = runtime.literal_areas().persistent();
    auto copied = detail::copy_persistent(scratch, region, all);
    if (!copied) {
        throw ConstructionFailure{copied.error()};
    }
    // The root tuple's elements follow its header in the area.
    const std::span<const Word> area = copied->area;
    const auto header = (copied->root - abi::v1::boxed_tag - reinterpret_cast<Word>(area.data())) / sizeof(Word);
    const auto elements = area.subspan(header + 1, descriptor.literal_count);
    bindings.literals.assign(elements.begin(), elements.end());
    // The area is never freed before the runtime, even when registration fails later: its words stay module literals.
    auto owner = std::shared_ptr<detail::PersistentCopy>(new detail::PersistentCopy(std::move(*copied)),
                                                         [&region](detail::PersistentCopy *literals) {
                                                             detail::free_persistent(region, *literals);
                                                             delete literals;
                                                         });
    runtime.literal_areas().add(area, std::move(owner));
}
} // namespace

CodeResult<void> decode_literals(Runtime &runtime, const abi::v1::ModuleDescriptor &descriptor, ModuleAtoms &bindings) {
    if (descriptor.literal_count == 0) {
        return {};
    }
    auto scratch = runtime.scratch_context();
    if (!scratch) {
        return std::unexpected(CodeError::resource_limit);
    }
    try {
        decode(runtime, **scratch, descriptor, bindings);
    } catch (const FormatError &) {
        return std::unexpected(CodeError::invalid_module);
    } catch (const ConstructionFailure &) {
        return std::unexpected(CodeError::resource_limit);
    } catch (const std::bad_alloc &) {
        return std::unexpected(CodeError::resource_limit);
    }
    if (bindings.claimed_cache && descriptor.literal_cache) {
        std::ranges::copy(bindings.literals, descriptor.literal_cache);
    }
    return {};
}
} // namespace clause::runtime

clause::abi::v1::TermWord CLAUSE_literal_v1(void *context, const std::size_t index, const void *descriptor) noexcept {
    using namespace clause;
    if (!context) {
        return 0;
    }
    auto &owner = *static_cast<runtime::ProcessContext *>(context);
    if (!owner.generated_calls().active() || owner.generated_calls().failure()) {
        return 0;
    }
    const auto word = owner.code_server().literal_word(descriptor, index);
    if (!word) {
        owner.generated_calls().fail_service(abi::v1::Status::wrong_owner);
        return 0;
    }
    return *word;
}
