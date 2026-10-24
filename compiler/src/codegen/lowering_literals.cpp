#include "../semantic/capabilities.hpp"
#include "../semantic/symbols.hpp"
#include "literals.hpp"
#include "lowering_state.hpp"
#include "module_atoms.hpp"
#include "runtime_symbols.hpp"
#include <algorithm>
#include <clause/abi/equality.hpp>
#include <clause/abi/literals.hpp>
#include <clause/abi/term.hpp>
#include <cmath>
#include <iterator>
#include <llvm/ADT/APInt.h>

// Module literals (literal heap): compound literals are encoded into the module's literals (abi/literals.hpp), which
// each runtime decodes into its literal heap at registration; their uses read the word from the module's literal
// cache when this context's runtime claimed the module, else from CLAUSE_literal_v1.
namespace clause::codegen {
using abi::literals::Extended;
using abi::literals::Writer;

std::vector<std::uint8_t> literal_integer_bytes(const llvm::APInt &value) {
    const auto count = std::max(2U, (value.getSignificantBits() + 7) / 8);
    const auto wide = value.sextOrTrunc(count * 8);
    std::vector<std::uint8_t> bytes(count);
    for (unsigned i = 0; i < count; ++i) {
        bytes[i] = static_cast<std::uint8_t>(wide.extractBitsAsZExtValue(8, (count - 1 - i) * 8));
    }
    return bytes;
}

namespace {
// Encodes literal expressions of the module being lowered; false for an expression that is no literal.
class Encoder final {
  public:
    Encoder(ExpressionLowering &state, std::string &out)
        : state_(state), syntax_(*state.module.syntax), output_(*state.entry.getParent()), writer_(out) {}

    bool term(const ast::ExprId &id) {
        const auto &value = syntax_.expression(id).value;
        if (const auto small = semantic::integer_literal(syntax_, id, 64)) {
            writer_.integer(*small);
            return true;
        }
        if (const auto *group = std::get_if<ast::Group>(&value)) {
            return term(group->expression);
        }
        return leaf(value) || container(value);
    }

    // An atom of the module's atom table; false for one it lacks.
    bool atom(const std::string &spelling) {
        const auto *slot = find_atom_slot(output_, spelling);
        if (!slot) {
            return false;
        }
        writer_.atom(llvm::cast<llvm::ConstantInt>(slot)->getZExtValue());
        return true;
    }

  private:
    bool leaf(const ast::ExprValue &value) {
        if (const auto *name = std::get_if<ast::Atom>(&value)) {
            return atom(utf8(name->name));
        }
        if (const auto *character = std::get_if<ast::CharacterLiteral>(&value)) {
            writer_.integer(character->value);
            return true;
        }
        if (const auto *unary = std::get_if<ast::UnaryExpression>(&value);
            unary && unary->operation == ast::UnaryOperator::negative) {
            return number(syntax_.expression(semantic::ungroup(syntax_, unary->operand)).value, true);
        }
        return number(value, false);
    }

    // An integer or float literal, negated for a unary minus.
    bool number(const ast::ExprValue &value, const bool negative) {
        if (const auto *integer = std::get_if<ast::IntegerLiteral>(&value)) {
            const auto &decimal = integer->value.decimal;
            const llvm::APInt parsed(static_cast<unsigned>(decimal.size() * 4 + 8), llvm::StringRef(decimal), 10);
            writer_.integer_bytes(literal_integer_bytes(negative ? -parsed : parsed));
            return true;
        }
        if (const auto *real = std::get_if<ast::FloatLiteral>(&value); real && std::isfinite(real->value)) {
            writer_.floating(negative ? -real->value : real->value);
            return true;
        }
        return false;
    }

    bool container(const ast::ExprValue &value) {
        if (const auto *text = std::get_if<ast::StringLiteral>(&value)) {
            return string(text->value);
        }
        if (const auto *tuple = std::get_if<ast::Tuple>(&value)) {
            writer_.extended(Extended::tuple);
            writer_.count(tuple->elements.size());
            return std::ranges::all_of(tuple->elements, [&](const ast::ExprId &element) { return term(element); });
        }
        if (const auto *list = std::get_if<ast::List>(&value)) {
            return elements(list->elements, list->tail);
        }
        if (const auto *map = std::get_if<ast::MapExpression>(&value)) {
            return pairs(*map);
        }
        if (const auto *binary = std::get_if<ast::Bitstring>(&value)) {
            const auto bytes = constant_binary(state_, *binary);
            if (bytes) {
                writer_.bitstring({reinterpret_cast<const char *>(bytes->data()), bytes->size()}, bytes->size() * 8);
            }
            return bytes.has_value();
        }
        return false;
    }

    // A proper list of characters: bytes as a string, wider ones as integer elements.
    bool string(const std::u32string &text) {
        if (text.empty()) {
            writer_.nil();
        } else if (std::ranges::all_of(text, [](const char32_t character) { return character <= 0xFF; })) {
            std::string bytes;
            std::ranges::transform(text, std::back_inserter(bytes),
                                   [](const char32_t character) { return static_cast<char>(character); });
            writer_.string(bytes);
        } else {
            writer_.extended(Extended::list);
            writer_.count(text.size());
            for (const auto character : text) {
                writer_.integer(character);
            }
            writer_.nil();
        }
        return true;
    }

    bool elements(const std::vector<ast::ExprId> &elements, const std::optional<ast::ExprId> &tail) {
        if (elements.empty() && !tail) {
            writer_.nil();
            return true;
        }
        writer_.extended(Extended::list);
        writer_.count(elements.size());
        if (!std::ranges::all_of(elements, [&](const ast::ExprId &element) { return term(element); })) {
            return false;
        }
        if (tail) {
            return term(*tail);
        }
        writer_.nil();
        return true;
    }

    // A map built without a base; a later equal key wins, as in evaluation.
    bool pairs(const ast::MapExpression &map) {
        if (map.base || std::ranges::any_of(map.fields, [](const ast::MapField &field) {
                return field.kind != ast::MapFieldKind::associate;
            })) {
            return false;
        }
        writer_.extended(Extended::map);
        writer_.count(map.fields.size());
        return std::ranges::all_of(map.fields,
                                   [&](const ast::MapField &field) { return term(field.key) && term(field.value); });
    }

    ExpressionLowering &state_;
    const ast::Module &syntax_;
    llvm::Module &output_;
    Writer writer_;
};

// One immediate word as a literal term: a small integer, [], {} or a fixed atom; false for any other.
bool immediate(Encoder &encoder, Writer &writer, const ExpressionLowering &state, const std::uint64_t word) {
    if (word == abi::v1::empty_list) {
        writer.nil();
        return true;
    }
    if (word == abi::v1::empty_tuple) {
        writer.extended(Extended::tuple);
        writer.count(0);
        return true;
    }
    if (const auto number = state.word->getBitWidth() == 32
                                ? abi::v1::IntegerEncoding<32>::decode(static_cast<std::uint32_t>(word))
                                : abi::v1::IntegerEncoding<64>::decode(word)) {
        writer.integer(*number);
        return true;
    }
    const auto atom = word >> abi::v1::atom_tag_bits;
    const bool fixed = (word & ((1U << abi::v1::atom_tag_bits) - 1)) == abi::v1::atom_tag &&
                       atom < abi::v1::fixed_atom_spellings.size();
    return fixed && encoder.atom(std::string(abi::v1::fixed_atom_spellings[atom]));
}

// The descriptor and atom cache of the module this object registers: another module's literal default is lowered
// here with this module's atoms (OPT0.8); null without them.
std::pair<llvm::GlobalVariable *, llvm::GlobalVariable *> object_globals(ExpressionLowering &state) {
    auto &output = *state.entry.getParent();
    const auto &owner = state.atom_owner ? *state.atom_owner : state.module;
    const auto prefix = semantic::encode_symbol({utf8(owner.name), "", 0});
    auto *descriptor = output.getNamedGlobal(prefix + ".descriptor");
    auto *cache = output.getNamedGlobal(prefix + ".atom_cache");
    return descriptor && cache ? std::pair{descriptor, cache} : std::pair{nullptr, nullptr};
}

// A tuple, list, string or map expression that is no immediate when it is a literal.
bool compound(const ast::ExprValue &value) {
    if (const auto *tuple = std::get_if<ast::Tuple>(&value)) {
        return !tuple->elements.empty();
    }
    if (const auto *list = std::get_if<ast::List>(&value)) {
        return !list->elements.empty() || list->tail;
    }
    if (const auto *text = std::get_if<ast::StringLiteral>(&value)) {
        return !text->value.empty();
    }
    const auto *map = std::get_if<ast::MapExpression>(&value);
    return map && !map->base;
}

// A module literal's word through the service: this context's runtime did not claim the module. The service only reads
// and cannot fail for the module's own code, so an unused literal is removed.
llvm::Value *literal_service(ExpressionLowering &state, const std::size_t index, llvm::Value *descriptor) {
    auto &output = *state.entry.getParent();
    auto &builder = state.builder;
    auto service = output.getOrInsertFunction(
        services::symbol<services::Literal>(output.getTargetTriple()),
        llvm::FunctionType::get(state.word, {builder.getPtrTy(), state.word, builder.getPtrTy()}, false));
    auto *call = builder.CreateCall(
        service, {state.entry.getArg(0), llvm::ConstantInt::get(state.word, index), descriptor}, "literal.service");
    call->setOnlyReadsMemory();
    call->setDoesNotThrow();
    call->addFnAttr(llvm::Attribute::WillReturn);
    return call;
}
} // namespace

llvm::Value *lower_literal(ExpressionLowering &state, const std::string &encoded) {
    const auto [descriptor, atom_cache] = object_globals(state);
    if (!descriptor) {
        return nullptr;
    }
    auto &output = *state.entry.getParent();
    auto &builder = state.builder;
    auto &context = output.getContext();
    const auto index = literal_index(output, encoded);
    auto *hit = llvm::BasicBlock::Create(context, "literal.cached", &state.entry);
    auto *miss = llvm::BasicBlock::Create(context, "literal.claim", &state.entry);
    auto *join = llvm::BasicBlock::Create(context, "literal.join", &state.entry);
    builder.CreateCondBr(claimed(state, atom_cache), hit, miss);
    builder.SetInsertPoint(hit);
    auto *cached = builder.CreateAlignedLoad(
        state.word, builder.CreateConstGEP1_64(state.word, literal_cache(output, state.word), index),
        llvm::Align(state.word->getBitWidth() / 8), "literal.word");
    builder.CreateBr(join);
    builder.SetInsertPoint(miss);
    auto *serviced = literal_service(state, index, descriptor);
    builder.CreateBr(join);
    builder.SetInsertPoint(join);
    auto *word = join_values(state, state.word, {hit, cached}, {miss, serviced});
    state.literal_values.insert(word);
    return word;
}

std::optional<std::string> encoded_literal(ExpressionLowering &state, const ast::ExprId &id) {
    if (!compound(state.module.syntax->expression(id).value) || !object_globals(state).first) {
        return std::nullopt;
    }
    std::string encoded;
    Encoder encoder(state, encoded);
    return encoder.term(id) ? std::optional{std::move(encoded)} : std::nullopt;
}

llvm::Value *lower_constant_term(ExpressionLowering &state, const ast::ExprId &id) {
    const auto encoded = encoded_literal(state, id);
    return encoded ? lower_literal(state, *encoded) : nullptr;
}

llvm::Value *immediate_container(ExpressionLowering &state, const bool tuple,
                                 const std::span<llvm::Value *const> values) {
    std::string encoded;
    Encoder encoder(state, encoded);
    Writer writer(encoded);
    writer.extended(tuple ? Extended::tuple : Extended::list);
    writer.count(tuple ? values.size() : values.size() - 1);
    for (auto *value : values) {
        const auto *constant = llvm::dyn_cast<llvm::ConstantInt>(value);
        if (!constant || !immediate(encoder, writer, state, constant->getZExtValue())) {
            return nullptr;
        }
    }
    return lower_literal(state, encoded);
}
} // namespace clause::codegen
