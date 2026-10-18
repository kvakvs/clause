#include "../semantic/match_plan.hpp"
#include "../semantic/records.hpp"
#include "../semantic/symbols.hpp"
#include "lowering_state.hpp"
#include "runtime_symbols.hpp"
#include <algorithm>
#include <clause/abi/equality.hpp>
#include <clause/abi/records.hpp>
#include <llvm/IR/Module.h>
#include <stdexcept>

// Native records through CLAUSE_record_v1 (docs/native-records.md#operations).
namespace clause::codegen {
namespace {
using Op = abi::v1::RecordOperation;
using Check = abi::v1::RecordCheck;
using Outcome = abi::v1::RecordOutcome;

// Raise badrecord or badfield with the service's payload in a body; a guard or pattern rejects instead.
llvm::BasicBlock *rejection(ExpressionLowering &state, const ServiceOutput result) {
    if (state.rejection) {
        return state.rejection;
    }
    auto &builder = state.builder;
    auto *saved = builder.GetInsertBlock();
    auto &context = state.entry.getContext();
    auto *dispatch = llvm::BasicBlock::Create(context, "record.rejection", &state.entry);
    auto *bad_record = llvm::BasicBlock::Create(context, "record.badrecord", &state.entry);
    auto *bad_field = llvm::BasicBlock::Create(context, "record.badfield", &state.entry);
    builder.SetInsertPoint(dispatch);
    auto *payload = builder.CreateAlignedLoad(state.word, result.slot, llvm::Align(state.word->getBitWidth() / 8));
    auto *test = builder.Insert(llvm::CmpInst::Create(llvm::Instruction::ICmp, llvm::CmpInst::ICMP_EQ, result.outcome,
                                                      builder.getInt8(static_cast<std::uint8_t>(Outcome::bad_record))));
    builder.CreateCondBr(test, bad_record, bad_field);
    builder.SetInsertPoint(bad_record);
    raise_reason(state, abi::v1::ErrorReason::badrecord, payload);
    builder.SetInsertPoint(bad_field);
    raise_reason(state, abi::v1::ErrorReason::badfield, payload);
    builder.SetInsertPoint(saved);
    return dispatch;
}

// Call the service with rooted operands and one rooted output slot.
ServiceOutput call(ExpressionLowering &state, Op operation, Check check, llvm::Constant *descriptor,
                   std::span<llvm::Value *const> values) {
    auto &builder = state.builder;
    auto &output = *state.entry.getParent();
    auto *arguments = builder.CreateGEP(state.word, state.roots->buffer,
                                        llvm::ConstantInt::get(state.word, state.roots->next), "record.arguments");
    for (auto *value : values) {
        root_value(state, value);
    }
    auto *slot = root_slot(state);
    auto *ptr = builder.getPtrTy();
    auto callee = output.getOrInsertFunction(
        services::symbol<services::Record>(output.getTargetTriple()),
        llvm::FunctionType::get(builder.getInt8Ty(),
                                {ptr, builder.getInt8Ty(), builder.getInt8Ty(), ptr, ptr, state.word, ptr}, false));
    auto *outcome = builder.CreateCall(callee,
                                       {state.entry.getArg(0), builder.getInt8(static_cast<std::uint8_t>(operation)),
                                        builder.getInt8(static_cast<std::uint8_t>(check)),
                                        descriptor ? descriptor : llvm::ConstantPointerNull::get(ptr), arguments,
                                        llvm::ConstantInt::get(state.word, values.size()), slot},
                                       "record.outcome");
    return {outcome, slot};
}

// The descriptor of a native record inside its defining module's record table, declared here when that module
// is another one of the batch.
llvm::Constant *descriptor(ExpressionLowering &state, const semantic::Module &owner,
                           const semantic::RecordLayout &layout) {
    const auto layouts = semantic::native_layouts(owner);
    const auto index = static_cast<std::size_t>(std::ranges::find(layouts, &layout) - layouts.begin());
    if (index == layouts.size()) {
        throw std::invalid_argument("lowering: native record descriptor is missing");
    }
    auto &output = *state.entry.getParent();
    const auto name = semantic::encode_symbol({utf8(owner.name), "", 0}) + ".records";
    auto *ptr = state.builder.getPtrTy();
    auto *entry = llvm::StructType::get(ptr, state.word, state.word, state.word, ptr, state.word, ptr, state.word);
    // This module's own table exists already; another module's is declared as an external constant.
    auto *table =
        llvm::cast<llvm::GlobalVariable>(output.getOrInsertGlobal(name, llvm::ArrayType::get(entry, layouts.size())));
    return llvm::ConstantExpr::getInBoundsGetElementPtr(
        table->getValueType(), table,
        llvm::ArrayRef<llvm::Constant *>{llvm::ConstantInt::get(state.word, 0),
                                         llvm::ConstantInt::get(state.word, index)});
}

// The value of an already evaluated child expression.
llvm::Value *value_of(ExpressionLowering &state, const ast::ExprId &id) {
    return state.values.at(&state.module.syntax->expression(id));
}

// The identity an access or update checks: module and name atoms under a check.
struct Identity {
    llvm::Value *module;
    llvm::Value *name;
    Check check;
};

// Local construction: every field value in definition order, explicit or default.
llvm::Value *construct(ExpressionLowering &state, const ast::Expression &expression,
                       const semantic::RecordLayout &layout) {
    const auto &values = state.record_values.at(&expression);
    const auto result = call(state, Op::make, Check::any, descriptor(state, state.module, layout), values);
    return checked_value(state, result, rejection(state, result));
}

// Update: the record, then field/value pairs, under the identity check.
llvm::Value *update(ExpressionLowering &state, const ast::RecordExpression &record, const Identity &identity) {
    std::vector<llvm::Value *> values;
    values.reserve(3 + 2 * record.fields.size());
    values.push_back(value_of(state, analyzed(record.base)));
    values.push_back(identity.module);
    values.push_back(identity.name);
    for (const auto &field : record.fields) {
        values.push_back(lower_atom(state, std::get<ast::Atom>(field.name)));
        values.push_back(value_of(state, field.value));
    }
    const auto result = call(state, Op::update, identity.check, nullptr, values);
    return checked_value(state, result, rejection(state, result));
}

// Field access under the identity check.
llvm::Value *access(ExpressionLowering &state, const ast::RecordAccess &access, const Identity &identity) {
    const std::array values{value_of(state, access.base), identity.module, identity.name,
                            lower_atom(state, access.field)};
    const auto result = call(state, Op::get, identity.check, nullptr, values);
    return checked_value(state, result, rejection(state, result));
}

// Raise reason with payload where the expression stands; code after it is unreachable.
llvm::Value *raise_here(ExpressionLowering &state, abi::v1::ErrorReason reason, llvm::Value *payload) {
    raise_reason(state, reason, payload);
    state.builder.SetInsertPoint(llvm::BasicBlock::Create(state.entry.getContext(), "record.raised", &state.entry));
    return llvm::ConstantInt::get(state.word, abi::v1::empty_list);
}

// The {{Module, Name}, Field} payload of badfield and novalue.
llvm::Value *field_payload(ExpressionLowering &state, const Identity &identity, const ast::Atom &field) {
    auto *owner = lower_tuple(state, std::array{identity.module, identity.name});
    return lower_tuple(state, std::array{owner, lower_atom(state, field)});
}

// Lower a literal default of another batch module here, loading its atoms from this module's table.
llvm::Value *foreign_default(ExpressionLowering &state, const semantic::Module &owner, const ast::ExprId &id) {
    ExpressionLowering foreign{state.builder, state.entry, owner, state.function, state.inferred, state.word, {}};
    foreign.roots = state.roots;
    foreign.failure = state.failure;
    foreign.handler = state.handler;
    foreign.bad_argument = state.bad_argument;
    foreign.bad_arithmetic = state.bad_arithmetic;
    foreign.system_limit = state.system_limit;
    foreign.atom_owner = state.atom_owner ? state.atom_owner : &state.module;
    auto *value = lower_body(foreign, id);
    state.failure = foreign.failure;
    state.bad_argument = foreign.bad_argument;
    state.bad_arithmetic = foreign.bad_arithmetic;
    state.system_limit = foreign.system_limit;
    return value;
}

// The first given field the definition lacks, in source order.
const ast::Atom *unknown_field(const ast::RecordExpression &record, const semantic::RecordLayout &layout) {
    for (const auto &field : record.fields) {
        const auto &name = std::get<ast::Atom>(field.name);
        if (!semantic::record_field(layout, name)) {
            return &name;
        }
    }
    return nullptr;
}

// Every field value in definition order: given, or the definition's default; null names the first field with
// neither.
std::vector<llvm::Value *> external_values(ExpressionLowering &state, const ast::RecordExpression &record,
                                           const semantic::Module &owner, const semantic::RecordLayout &layout,
                                           const ast::Atom *&missing) {
    std::vector<llvm::Value *> values;
    values.reserve(layout.fields.size());
    for (const auto &declared : layout.fields) {
        const auto given = std::ranges::find_if(record.fields, [&](const ast::RecordField &field) {
            return std::get<ast::Atom>(field.name).name == declared.name.name;
        });
        if (given != record.fields.end()) {
            values.push_back(value_of(state, given->value));
        } else if (declared.default_value) {
            values.push_back(foreign_default(state, owner, *declared.default_value));
        } else {
            missing = &declared.name;
            return {};
        }
    }
    return values;
}

// External construction: the record must be an exported native record of a batch module ({badrecord, {M, N}}
// otherwise); an unknown field is badfield before a missing value is novalue.
llvm::Value *construct_external(ExpressionLowering &state, const ast::RecordExpression &record,
                                const semantic::RecordName &name, const Identity &identity) {
    const auto *layout = semantic::external_layout(state.module, name);
    if (!layout) {
        return raise_here(state, abi::v1::ErrorReason::badrecord,
                          lower_tuple(state, std::array{identity.module, identity.name}));
    }
    if (const auto *field = unknown_field(record, *layout)) {
        return raise_here(state, abi::v1::ErrorReason::badfield, field_payload(state, identity, *field));
    }
    const auto &owner = *state.module.peers.at(name.module);
    const ast::Atom *missing = nullptr;
    const auto values = external_values(state, record, owner, *layout, missing);
    if (missing) {
        return raise_here(state, abi::v1::ErrorReason::novalue, field_payload(state, identity, *missing));
    }
    const auto result = call(state, Op::make, Check::any, descriptor(state, owner, *layout), values);
    return checked_value(state, result, rejection(state, result));
}
} // namespace

llvm::Value *lower_native_record(ExpressionLowering &state, const ast::Expression &expression,
                                 const semantic::RecordLayout &layout) {
    // Local forms check this module's record of the name; access checks only the name, as OTP's runtime does.
    const Identity identity{lower_atom(state, ast::Atom{state.module.name}), lower_atom(state, layout.name),
                            Check::module_name};
    if (const auto *record = std::get_if<ast::RecordExpression>(&expression.value)) {
        return record->base ? update(state, *record, identity) : construct(state, expression, layout);
    }
    return access(state, std::get<ast::RecordAccess>(expression.value), {identity.module, identity.name, Check::name});
}

llvm::Value *lower_external_record(ExpressionLowering &state, const ast::Expression &expression,
                                   const semantic::RecordName &name) {
    // Qualified and imported forms need the record exported from the named module.
    const Identity identity{lower_atom(state, ast::Atom{name.module}), lower_atom(state, ast::Atom{name.name}),
                            Check::exported_module_name};
    if (const auto *record = std::get_if<ast::RecordExpression>(&expression.value)) {
        return record->base ? update(state, *record, identity) : construct_external(state, *record, name, identity);
    }
    return access(state, std::get<ast::RecordAccess>(expression.value), identity);
}

llvm::Value *lower_anonymous_record(ExpressionLowering &state, const ast::Expression &expression) {
    // Anonymous access reads any native record (OTP's runtime checks no export); an update needs the record
    // exported or defined here. The name operand is unused, so the module's name fills it.
    auto *module = lower_atom(state, ast::Atom{state.module.name});
    if (const auto *record = std::get_if<ast::RecordExpression>(&expression.value)) {
        return update(state, *record, {module, module, Check::exported_or_module});
    }
    return access(state, std::get<ast::RecordAccess>(expression.value), {module, module, Check::any});
}

llvm::Value *lower_record_pattern(ExpressionLowering &state, const semantic::MatchNode &node, llvm::Value *input,
                                  llvm::BasicBlock *mismatch) {
    const auto &name = std::get<ast::Atom>(analyzed(node.literal));
    if (node.operation == semantic::MatchOperation::record_test) {
        auto *test = lower_native_test(state, static_cast<Check>(node.index), input,
                                       lower_atom(state, analyzed(node.record_module)), lower_atom(state, name));
        auto *matched = llvm::BasicBlock::Create(state.entry.getContext(), "record.matched", &state.entry);
        state.builder.CreateCondBr(test, matched, mismatch);
        state.builder.SetInsertPoint(matched);
        return input;
    }
    // The check ignores module and name, so any atom of this module fills their operands.
    auto *none = lower_atom(state, ast::Atom{state.module.name});
    const std::array values{input, none, none, lower_atom(state, name)};
    return checked_value(state, call(state, Op::match, Check::any, nullptr, values), mismatch);
}

llvm::Value *lower_native_test(ExpressionLowering &state, abi::v1::RecordCheck check, llvm::Value *value,
                               llvm::Value *module, llvm::Value *name) {
    const std::array values{value, module, name};
    const auto result = call(state, Op::test, check, nullptr, values);
    propagate_failure(state);
    return state.builder.Insert(
        llvm::CmpInst::Create(llvm::Instruction::ICmp, llvm::CmpInst::ICMP_EQ, result.outcome,
                              state.builder.getInt8(static_cast<std::uint8_t>(Outcome::success))));
}
} // namespace clause::codegen
