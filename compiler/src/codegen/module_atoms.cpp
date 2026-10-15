#include "module_atoms.hpp"
#include "../semantic/binding_state.hpp"
#include "../semantic/capabilities.hpp"
#include "../semantic/records.hpp"
#include "../semantic/services.hpp"
#include "../semantic/symbols.hpp"
#include "lowering_state.hpp"
#include "runtime_symbols.hpp"
#include <algorithm>
#include <set>

namespace clause::codegen {
namespace {
// Reuse the collision-free symbol encoding for private literal-slot metadata.
std::string slot_name(const std::string &spelling) { return "atom.slot." + semantic::encode_symbol({spelling, "", 0}); }

// Field names a record expression or pattern spells; native operations pass them to the record service.
void field_atoms(const ast::RecordExpression &record, std::set<std::string> &result) {
    for (const auto &field : record.fields) {
        if (const auto *name = std::get_if<ast::Atom>(&field.name)) {
            result.insert(utf8(name->name));
        }
    }
}

// Normalized patterns can contain atoms folded from grouped syntax; collect their runtime spellings too.
void pattern_atoms(const semantic::Module &module, const semantic::Function &function, std::set<std::string> &result) {
    for (const auto &pattern : function.patterns) {
        if (const auto *record =
                std::get_if<ast::RecordExpression>(&module.syntax->expression(pattern.expression).value)) {
            field_atoms(*record, result);
        }
        if (!pattern.literal) {
            continue;
        }
        if (const auto *atom = std::get_if<ast::Atom>(&*pattern.literal)) {
            result.insert(utf8(atom->name));
        }
    }
}

// Guard roots and service outputs require preinitialized true/false slots alongside body literals.
void roots(const semantic::Module &module, const semantic::Function &function, std::vector<ast::ExprId> &pending,
           std::set<std::string> &result) {
    const auto &definition = std::get<ast::Function>(module.syntax->form(function.form).value);
    const auto expressions = semantic::function_roots(definition);
    pending.insert(pending.end(), expressions.begin(), expressions.end());
    const auto keys = semantic::pattern_reads(module, function);
    pending.insert(pending.end(), keys.begin(), keys.end());
    if (!function.services.empty() ||
        std::ranges::any_of(definition.clauses, [](const auto &clause) { return clause.guard.has_value(); })) {
        result.insert("true");
        result.insert("false");
    }
    pattern_atoms(module, function, result);
}

// Boolean syntax needs both canonical slots even when all source operands are incoming variables.
bool booleans(const ast::ExprValue &value) {
    if (const auto *unary = std::get_if<ast::UnaryExpression>(&value)) {
        return unary->operation == ast::UnaryOperator::logical_not;
    }
    const auto *binary = std::get_if<ast::BinaryExpression>(&value);
    return binary &&
           (semantic::immediate_operator(binary->operation) || binary->operation == ast::BinaryOperator::and_also ||
            binary->operation == ast::BinaryOperator::or_else);
}

// Case, if, try and anonymous fun clause guards compare each test with the canonical true atom.
bool guarded(const ast::ExprValue &value) {
    const auto *clauses = semantic::fun_clauses(value);
    if (clauses) {
        return std::ranges::any_of(*clauses, [](const auto &clause) { return clause.guard.has_value(); });
    }
    return std::ranges::any_of(semantic::branch_clauses(value),
                               [](const auto &clause) { return clause.guard != nullptr; });
}

// A catch clause without a class matches the class atom throw.
bool implicit_throw(const ast::ExprValue &value) {
    return std::ranges::any_of(semantic::branch_clauses(value),
                               [](const auto &clause) { return clause.handler && !clause.handler->exception_class; });
}

// Atoms of a literal default of another batch module, which this module lowers itself.
void default_atoms(const ast::Module &syntax, const ast::ExprId &root, std::set<std::string> &result) {
    std::vector<ast::ExprId> pending{root};
    while (!pending.empty()) {
        const auto &value = syntax.expression(pending.back()).value;
        pending.pop_back();
        if (const auto *atom = std::get_if<ast::Atom>(&value)) {
            result.insert(utf8(atom->name));
        }
        const auto children = semantic::binding_children(value);
        pending.insert(pending.end(), children.begin(), children.end());
    }
}

// Module and record names of a qualified or imported form; a construction also lowers the defining module's
// defaults of the fields it omits.
void external_atoms(const semantic::Module &module, const ast::ExprValue &value, const semantic::RecordName &name,
                    std::set<std::string> &result) {
    result.insert(utf8(name.module));
    result.insert(utf8(name.name));
    const auto *record = std::get_if<ast::RecordExpression>(&value);
    const auto *layout = semantic::external_layout(module, name);
    if (!record || record->base || !layout) {
        return;
    }
    const auto &syntax = *module.peers.at(name.module)->syntax;
    for (const auto &field : layout->fields) {
        result.insert(utf8(field.name.name));
        if (field.default_value) {
            default_atoms(syntax, *field.default_value, result);
        }
    }
}

// Record tags, the undefined default and the field names a record_info(fields, R) list holds.
void record_atoms(const semantic::Module &module, const ast::Expression &expression, std::set<std::string> &result) {
    const auto &value = expression.value;
    if (const auto info = semantic::record_info(module, expression); info && info->fields) {
        for (const auto &field : info->layout.fields) {
            result.insert(utf8(field.name.name));
        }
    }
    const ast::RecordIdentity *identity = nullptr;
    if (const auto *record = std::get_if<ast::RecordExpression>(&value)) {
        identity = &record->identity;
        result.insert("undefined");
        field_atoms(*record, result);
    } else if (const auto *access = std::get_if<ast::RecordAccess>(&value)) {
        identity = &access->identity;
        result.insert(utf8(access->field.name));
    }
    if (!identity || semantic::anonymous_record(*identity)) {
        return;
    }
    if (const auto external = semantic::external_record(module, *identity)) {
        external_atoms(module, value, *external, result);
    } else {
        result.insert(utf8(semantic::record_layout(module, *identity)->name.name));
    }
}

// erlang:raise/3 evaluates to badarg when its class or stack is invalid.
bool raises_stack(const ast::Module &syntax, const ast::ExprValue &value) {
    const auto *call = std::get_if<ast::CallExpression>(&value);
    const auto *remote =
        call ? std::get_if<ast::RemoteExpression>(&syntax.expression(semantic::ungroup(syntax, call->target)).value)
             : nullptr;
    if (!remote || call->arguments.size() != 3) {
        return false;
    }
    const auto *owner = std::get_if<ast::Atom>(&syntax.expression(semantic::ungroup(syntax, remote->module)).value);
    const auto *name = std::get_if<ast::Atom>(&syntax.expression(semantic::ungroup(syntax, remote->function)).value);
    return owner && name && owner->name == U"erlang" && name->name == U"raise";
}

// is_record(X, r) with an imported r tests the record's module.
void imported_test_atoms(const semantic::Module &module, const ast::ExprValue &value, std::set<std::string> &result) {
    const auto *call = std::get_if<ast::CallExpression>(&value);
    if (!call || call->arguments.size() != 2) {
        return;
    }
    const auto &syntax = *module.syntax;
    const auto *tag = std::get_if<ast::Atom>(&syntax.expression(semantic::ungroup(syntax, call->arguments[1])).value);
    if (const auto *from = tag ? semantic::imported_module(module, tag->name) : nullptr) {
        result.insert(utf8(*from));
    }
}

// The literal module and function of fun M:F/A built at run time.
void reference_atoms(const ast::RemoteFunReference &reference, std::set<std::string> &result) {
    for (const auto *part : {&reference.module, &reference.name}) {
        if (const auto *atom = std::get_if<ast::Atom>(part)) {
            result.insert(utf8(atom->name));
        }
    }
}

// Collect the atoms one expression needs: its literal, record names and the atoms its lowering produces.
void expression_atoms(const semantic::Module &module, const ast::Expression &expression,
                      std::set<std::string> &result) {
    const auto &value = expression.value;
    record_atoms(module, expression, result);
    if (booleans(value) || guarded(value) || semantic::comprehension_qualifiers(value)) {
        result.insert("true");
        result.insert("false");
    }
    if (const auto *atom = std::get_if<ast::Atom>(&value)) {
        result.insert(utf8(atom->name));
    }
    if (const auto *reference = std::get_if<ast::RemoteFunReference>(&value)) {
        reference_atoms(*reference, result);
    }
    if (implicit_throw(value)) {
        result.insert("throw");
    }
    if (raises_stack(*module.syntax, value)) {
        result.insert("badarg");
    }
    if (std::holds_alternative<ast::ReceiveExpression>(value)) {
        // A receive without an after part waits with the infinity timeout; the wait answers true for a message.
        result.insert("infinity");
        result.insert("true");
    }
    imported_test_atoms(module, value, result);
}

// Walk only admitted executable children; atom call targets are metadata rather than term expressions.
std::set<std::string> spellings(const semantic::Module &module) {
    std::set<std::string> result{utf8(module.name)};
    // Native record descriptors name their record and fields by slot.
    for (const auto *layout : semantic::native_layouts(module)) {
        result.insert(utf8(layout->name.name));
        for (const auto &field : layout->fields) {
            result.insert(utf8(field.name.name));
        }
    }
    // Fun descriptors name their module and function by slot.
    for (const auto &fun : module.funs) {
        result.insert(utf8(fun.module));
        result.insert(utf8(fun.function));
    }
    std::vector<ast::ExprId> pending;
    for (const auto &function : module.functions) {
        result.insert(utf8(function.key.name));
        roots(module, function, pending, result);
    }
    while (!pending.empty()) {
        const auto id = pending.back();
        pending.pop_back();
        const auto &expression = module.syntax->expression(id);
        expression_atoms(module, expression, result);
        const auto children = semantic::expression_children(module, expression);
        pending.insert(pending.end(), children.begin(), children.end());
    }
    return result;
}
} // namespace

llvm::Constant *emit_atom_table(llvm::Module &output, const semantic::Module &module, llvm::IntegerType *word,
                                std::size_t &count) {
    auto &context = output.getContext();
    auto *type = llvm::StructType::get(llvm::PointerType::get(context, 0), word);
    const auto names = spellings(module);
    std::vector<llvm::Constant *> entries;
    entries.reserve(names.size());
    for (const auto &name : names) {
        auto *bytes = llvm::ConstantDataArray::getString(context, name, false);
        auto *text = new llvm::GlobalVariable(output, bytes->getType(), true, llvm::GlobalValue::PrivateLinkage, bytes,
                                              "atom.spelling");
        auto *slot = llvm::cast<llvm::GlobalVariable>(output.getOrInsertGlobal(slot_name(name), word));
        slot->setConstant(true);
        slot->setLinkage(llvm::GlobalValue::PrivateLinkage);
        slot->setInitializer(llvm::ConstantInt::get(word, entries.size()));
        entries.push_back(llvm::ConstantStruct::get(type, text, llvm::ConstantInt::get(word, name.size())));
    }
    count = entries.size();
    auto *array = llvm::ConstantArray::get(llvm::ArrayType::get(type, count), entries);
    return new llvm::GlobalVariable(output, array->getType(), true, llvm::GlobalValue::PrivateLinkage, array,
                                    "module.atoms");
}

llvm::Constant *atom_slot(llvm::Module &output, const std::string &spelling) {
    return output.getNamedGlobal(slot_name(spelling))->getInitializer();
}

llvm::Value *lower_atom(ExpressionLowering &state, const ast::Atom &atom) {
    // false and true have fixed words in every runtime (OPT0.2A).
    if (atom.name == U"false" || atom.name == U"true") {
        return llvm::ConstantInt::get(state.word, atom.name == U"true" ? abi::v1::true_atom : abi::v1::false_atom);
    }
    auto &output = *state.entry.getParent();
    auto &builder = state.builder;
    auto *slot = atom_slot(output, utf8(atom.name));
    const auto &owner = state.atom_owner ? *state.atom_owner : state.module;
    const auto prefix = semantic::encode_symbol({utf8(owner.name), "", 0});
    auto *descriptor = output.getNamedGlobal(prefix + ".descriptor");
    const auto symbol = services::symbol<services::Atom>(output.getTargetTriple());
    auto service = output.getOrInsertFunction(
        symbol, llvm::FunctionType::get(state.word, {builder.getPtrTy(), state.word, builder.getPtrTy()}, false));
    auto *value = builder.CreateCall(service, {state.entry.getArg(0), slot, descriptor}, "atom.value");
    propagate_failure(state);
    return value;
}
} // namespace clause::codegen
