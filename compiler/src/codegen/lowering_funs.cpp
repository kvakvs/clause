#include "../semantic/capabilities.hpp"
#include "../semantic/funs.hpp"
#include "../semantic/symbols.hpp"
#include "lowering_state.hpp"
#include "runtime_symbols.hpp"
#include <algorithm>
#include <clause/abi/equality.hpp>
#include <clause/abi/frames.hpp>
#include <clause/abi/messages.hpp>
#include <clause/abi/term.hpp>
#include <llvm/IR/Module.h>
#include <optional>
#include <stdexcept>
#include <variant>

// Function values through CLAUSE_make_fun_v1 and CLAUSE_apply_v1 (docs/funs.md).
namespace clause::codegen {
namespace {
// The FunDescriptor of a fun expression in this module's `<prefix>.funs` table.
llvm::Constant *descriptor(ExpressionLowering &state, const ast::Expression &expression) {
    auto &output = *state.entry.getParent();
    const auto name = semantic::encode_symbol({utf8(state.module.name), "", 0}) + ".funs";
    auto *table = output.getNamedGlobal(name);
    if (!table) {
        throw std::invalid_argument("lowering: fun table is missing");
    }
    const auto index = state.module.fun_entries.at(&expression);
    return llvm::ConstantExpr::getInBoundsGetElementPtr(
        table->getValueType(), table,
        llvm::ArrayRef<llvm::Constant *>{llvm::ConstantInt::get(state.word, 0),
                                         llvm::ConstantInt::get(state.word, index)});
}

// The evaluated value of an operand expression.
llvm::Value *value_of(ExpressionLowering &state, const ast::ExprId &id) {
    return state.values.at(&state.module.syntax->expression(id));
}

// A word array of `count` words (at least one) that lower_frames makes the process registers.
llvm::Value *registers(ExpressionLowering &state, std::size_t count) {
    auto *array = state.builder.CreateAlloca(
        state.word, llvm::ConstantInt::get(state.word, std::max<std::size_t>(count, 1)), "apply.arguments");
    array->setAlignment(llvm::Align(state.word->getBitWidth() / 8));
    return array;
}

// A register array holding the call's arguments; a fun's captured values follow them.
llvm::Value *arguments(ExpressionLowering &state, const ast::CallExpression &call) {
    auto &builder = state.builder;
    auto *array = registers(state, call.arguments.size());
    const llvm::Align alignment(state.word->getBitWidth() / 8);
    for (std::size_t i = 0; i < call.arguments.size(); ++i) {
        builder.CreateAlignedStore(value_of(state, call.arguments[i]),
                                   builder.CreateGEP(state.word, array, llvm::ConstantInt::get(state.word, i)),
                                   alignment);
    }
    return array;
}

// Whether a call expression is in tail position, so its callee's result is this function's.
bool in_tail(const ExpressionLowering &state, const ast::Expression &expression) {
    return state.tail_calls && state.tail_calls->contains(&expression);
}

// Call a preparation service returning the FrameDescriptor to enter (null after it raised), then transfer there with
// the registers in `array`: a tail transfer when `tail` is set.
llvm::Value *transfer(ExpressionLowering &state, bool tail, llvm::FunctionCallee service,
                      llvm::ArrayRef<llvm::Value *> operands, llvm::Value *array) {
    auto &builder = state.builder;
    auto &output = *state.entry.getParent();
    auto *ptr = builder.getPtrTy();
    auto *frame = builder.CreateCall(service, operands, "apply.frame");
    propagate_failure(state);
    auto marker = output.getOrInsertFunction(APPLY_MARKER, llvm::FunctionType::get(state.word, {ptr, ptr, ptr}, false));
    auto *result = builder.CreateCall(marker, {state.entry.getArg(0), array, frame}, "apply.result");
    if (tail) {
        // The called function's result is this function's: lower_frames turns the return into a tail transfer.
        builder.CreateRet(result);
        builder.SetInsertPoint(llvm::BasicBlock::Create(state.entry.getContext(), "tail.dead", &state.entry));
        return llvm::ConstantInt::get(state.word, abi::v1::empty_list);
    }
    propagate_failure(state);
    return result;
}

// Declare a dynamic call service returning a FrameDescriptor: the context, `words` term words, then the register
// array when `registers` is set.
template <typename Service>
llvm::FunctionCallee frame_service(ExpressionLowering &state, std::size_t words, bool registers) {
    auto &output = *state.entry.getParent();
    auto *ptr = state.builder.getPtrTy();
    std::vector<llvm::Type *> parameters;
    parameters.reserve(words + 2);
    parameters.push_back(ptr);
    parameters.insert(parameters.end(), words, state.word);
    if (registers) {
        parameters.push_back(ptr);
    }
    return output.getOrInsertFunction(services::symbol<Service>(output.getTargetTriple()),
                                      llvm::FunctionType::get(ptr, parameters, false));
}

// The value a variable part of fun M:F/A reads; binding analysis resolved every one.
llvm::Value *read_value(const ExpressionLowering &state, const std::optional<semantic::BindingId> &read) {
    if (!read) {
        throw std::invalid_argument("lowering: unresolved fun operand");
    }
    return state.bindings.at(*read);
}

// One part of fun M:F/A: its atom, or the value of its variable.
llvm::Value *name_operand(ExpressionLowering &state, const std::variant<ast::Atom, ast::Variable> &part,
                          const std::optional<semantic::BindingId> &read) {
    const auto *atom = std::get_if<ast::Atom>(&part);
    return atom ? lower_atom(state, *atom) : read_value(state, read);
}

// The arity of fun M:F/A: a small integer (capability analysis admitted 0..255), or the value of its variable.
llvm::Value *arity_operand(ExpressionLowering &state, const std::variant<Integer, ast::Variable> &part,
                           const std::optional<semantic::BindingId> &read) {
    const auto *number = std::get_if<Integer>(&part);
    const auto count = number ? semantic::arity(*number) : std::nullopt;
    if (!count) {
        return read_value(state, read);
    }
    const auto value = static_cast<std::int64_t>(*count);
    return llvm::ConstantInt::get(state.word, state.word->getBitWidth() == 32
                                                  ? *abi::v1::IntegerEncoding<32>::encode(value)
                                                  : *abi::v1::IntegerEncoding<64>::encode(value));
}

// Build fun M:F/A with variables through CLAUSE_make_external_fun_v1; badarg raises.
llvm::Value *dynamic_fun(ExpressionLowering &state, const ast::Expression &expression,
                         const ast::RemoteFunReference &reference) {
    auto &builder = state.builder;
    auto &output = *state.entry.getParent();
    const auto &reads = state.function.fun_operands.at(&expression);
    auto *module = name_operand(state, reference.module, reads[0]);
    auto *function = name_operand(state, reference.name, reads[1]);
    auto *arity = arity_operand(state, reference.arity, reads[2]);
    auto *ptr = builder.getPtrTy();
    auto service = output.getOrInsertFunction(
        services::symbol<services::MakeExternalFun>(output.getTargetTriple()),
        llvm::FunctionType::get(builder.getInt8Ty(), {ptr, state.word, state.word, state.word, ptr}, false));
    auto *slot = root_slot(state);
    builder.CreateCall(service, {state.entry.getArg(0), module, function, arity, slot});
    propagate_failure(state);
    return builder.CreateAlignedLoad(state.word, slot, llvm::Align(state.word->getBitWidth() / 8), "fun.value");
}
} // namespace

namespace {
// The fun from CLAUSE_make_fun_v1, its captured values in consecutive rooted slots.
llvm::Value *fun_service(ExpressionLowering &state, const ast::Expression &expression) {
    auto &builder = state.builder;
    auto &output = *state.entry.getParent();
    auto *ptr = builder.getPtrTy();
    auto service = output.getOrInsertFunction(
        services::symbol<services::MakeFun>(output.getTargetTriple()),
        llvm::FunctionType::get(builder.getInt8Ty(), {ptr, ptr, ptr, state.word, ptr}, false));
    // An anonymous fun's captured values go to consecutive rooted slots.
    const auto &captures = state.module.funs.at(state.module.fun_entries.at(&expression)).captures;
    llvm::Value *values = llvm::ConstantPointerNull::get(ptr);
    if (!captures.empty()) {
        values = builder.CreateGEP(state.word, state.roots->buffer,
                                   llvm::ConstantInt::get(state.word, state.roots->next), "fun.captures");
        for (const auto &identity : captures) {
            root_value(state, state.bindings.at(identity));
        }
    }
    auto *slot = root_slot(state);
    builder.CreateCall(service, {state.entry.getArg(0), descriptor(state, expression), values,
                                 llvm::ConstantInt::get(state.word, captures.size()), slot});
    propagate_failure(state);
    return builder.CreateAlignedLoad(state.word, slot, llvm::Align(state.word->getBitWidth() / 8), "fun.value");
}

// An i1 that holds when this context's runtime claimed the module's caches (OPT0.8, inline closures).
llvm::Value *claimed(ExpressionLowering &state, llvm::GlobalVariable *atom_cache) {
    auto &builder = state.builder;
    const llvm::Align align(state.word->getBitWidth() / 8);
    auto *claim = builder.CreateAlignedLoad(state.word, atom_cache, align, "fun.claim");
    claim->setAtomic(llvm::AtomicOrdering::Monotonic);
    auto *runtime = builder.CreateAlignedLoad(
        state.word,
        builder.CreateConstInBoundsGEP1_64(state.word, state.entry.getArg(0),
                                           static_cast<unsigned>(abi::v1::StateField::runtime)),
        align, "context.runtime");
    return builder.Insert(llvm::CmpInst::Create(llvm::Instruction::ICmp, llvm::CmpInst::ICMP_EQ, claim, runtime));
}

// Write the fun cell at `top`: header, the definition from the fun cache, then the captured values.
llvm::Value *write_fun(ExpressionLowering &state, const ast::Expression &expression, llvm::Value *definition,
                       llvm::Value *top) {
    auto &builder = state.builder;
    const llvm::Align align(state.word->getBitWidth() / 8);
    const auto &captures = state.module.funs.at(state.module.fun_entries.at(&expression)).captures;
    const auto header = (static_cast<std::uint64_t>(1 + captures.size()) << abi::v1::header_count_shift) |
                        (static_cast<std::uint64_t>(abi::v1::HeaderKind::fun_closure) << abi::v1::header_kind_shift);
    builder.CreateAlignedStore(llvm::ConstantInt::get(state.word, header), top, align);
    builder.CreateAlignedStore(definition, builder.CreateConstGEP1_64(state.word, top, 1), align);
    for (std::size_t i = 0; i < captures.size(); ++i) {
        builder.CreateAlignedStore(state.bindings.at(captures[i]), builder.CreateConstGEP1_64(state.word, top, 2 + i),
                                   align);
    }
    auto *made = builder.CreateOr(builder.CreatePtrToInt(top, state.word),
                                  llvm::ConstantInt::get(state.word, abi::v1::boxed_tag));
    mark_term(state, made);
    return made;
}
} // namespace

llvm::Value *lower_fun(ExpressionLowering &state, const ast::Expression &expression) {
    if (const auto *reference = std::get_if<ast::RemoteFunReference>(&expression.value);
        reference && semantic::dynamic_fun(*reference)) {
        return dynamic_fun(state, expression, *reference);
    }
    auto &output = *state.entry.getParent();
    const auto prefix = semantic::encode_symbol({utf8(state.module.name), "", 0});
    auto *atom_cache = output.getNamedGlobal(prefix + ".atom_cache");
    auto *fun_cache = output.getNamedGlobal(prefix + ".fun_cache");
    if (!atom_cache || !fun_cache) {
        return fun_service(state, expression);
    }
    // Inline closures: the cell is bump-allocated when this runtime filled the fun cache and the heap has room.
    auto &builder = state.builder;
    auto &context = state.entry.getContext();
    const llvm::Align align(state.word->getBitWidth() / 8);
    const auto index = state.module.fun_entries.at(&expression);
    const auto captures = state.module.funs.at(index).captures.size();
    auto *definition = builder.CreateAlignedLoad(state.word, builder.CreateConstGEP1_64(state.word, fun_cache, index),
                                                 align, "fun.definition");
    const auto room = heap_room(state, llvm::ConstantInt::get(state.word, 2 + captures));
    auto *direct = llvm::BasicBlock::Create(context, "fun.inline", &state.entry);
    auto *fallback = llvm::BasicBlock::Create(context, "fun.service", &state.entry);
    auto *join = llvm::BasicBlock::Create(context, "fun.join", &state.entry);
    builder.CreateCondBr(builder.CreateAnd(claimed(state, atom_cache), room.fits), direct, fallback);
    builder.SetInsertPoint(direct);
    auto *made = write_fun(state, expression, definition, room.top);
    heap_commit(state, room);
    builder.CreateBr(join);
    builder.SetInsertPoint(fallback);
    auto *serviced = fun_service(state, expression);
    auto *fallback_end = builder.GetInsertBlock();
    builder.CreateBr(join);
    builder.SetInsertPoint(join);
    return join_values(state, state.word, {direct, made}, {fallback_end, serviced});
}

llvm::Value *lower_fun_call(ExpressionLowering &state, const ast::Expression &expression,
                            const ast::CallExpression &call) {
    auto *fun = value_of(state, call.target);
    auto *array = arguments(state, call);
    auto *count = llvm::ConstantInt::get(state.word, call.arguments.size());
    return transfer(state, in_tail(state, expression), frame_service<services::Apply>(state, 2, true),
                    {state.entry.getArg(0), fun, count, array}, array);
}

llvm::Value *lower_dynamic_call(ExpressionLowering &state, const ast::Expression &expression,
                                const ast::CallExpression &call) {
    const auto &syntax = *state.module.syntax;
    const auto &remote =
        std::get<ast::RemoteExpression>(syntax.expression(semantic::ungroup(syntax, call.target)).value);
    auto *module = value_of(state, remote.module);
    auto *function = value_of(state, remote.function);
    auto *array = arguments(state, call);
    auto *count = llvm::ConstantInt::get(state.word, call.arguments.size());
    return transfer(state, in_tail(state, expression), frame_service<services::Call>(state, 3, false),
                    {state.entry.getArg(0), module, function, count}, array);
}

llvm::Value *lower_builtin(ExpressionLowering &state, std::size_t builtin, std::span<llvm::Value *const> values) {
    auto &builder = state.builder;
    auto *array = registers(state, values.size());
    const llvm::Align alignment(state.word->getBitWidth() / 8);
    for (std::size_t i = 0; i < values.size(); ++i) {
        builder.CreateAlignedStore(
            values[i], builder.CreateGEP(state.word, array, llvm::ConstantInt::get(state.word, i)), alignment);
    }
    auto &output = *state.entry.getParent();
    auto service = output.getOrInsertFunction(
        services::symbol<services::BuiltinFrame>(output.getTargetTriple()),
        llvm::FunctionType::get(builder.getPtrTy(), {builder.getPtrTy(), state.word}, false));
    // The builtin is entered like a function, so it spends a reduction and may yield in portions.
    return transfer(state, false, service, {state.entry.getArg(0), llvm::ConstantInt::get(state.word, builtin)}, array);
}

llvm::Value *lower_receive(ExpressionLowering &state, abi::v1::ReceiveOperation operation, llvm::Value *slot) {
    auto &builder = state.builder;
    auto &output = *state.entry.getParent();
    auto *ptr = builder.getPtrTy();
    auto service = output.getOrInsertFunction(
        services::symbol<services::Receive>(output.getTargetTriple()),
        llvm::FunctionType::get(builder.getInt8Ty(), {ptr, builder.getInt8Ty(), ptr}, false));
    auto *target = slot ? slot : llvm::ConstantPointerNull::get(ptr);
    return builder.CreateCall(service,
                              {state.entry.getArg(0), builder.getInt8(static_cast<std::uint8_t>(operation)), target},
                              "receive.step");
}

llvm::Value *lower_wait(ExpressionLowering &state, llvm::Value *timeout) {
    auto &builder = state.builder;
    auto *array = registers(state, 1);
    builder.CreateAlignedStore(timeout, array, llvm::Align(state.word->getBitWidth() / 8));
    auto &output = *state.entry.getParent();
    auto service = output.getOrInsertFunction(services::symbol<services::WaitFrame>(output.getTargetTriple()),
                                              llvm::FunctionType::get(builder.getPtrTy(), {builder.getPtrTy()}, false));
    // The wait is entered like a function: the process may suspend there until a message arrives.
    return transfer(state, false, service, {state.entry.getArg(0)}, array);
}

llvm::Value *lower_apply(ExpressionLowering &state, const ast::Expression &expression,
                         const ast::CallExpression &call) {
    auto *array = registers(state, abi::v1::register_count);
    std::vector<llvm::Value *> operands;
    operands.reserve(call.arguments.size() + 2);
    operands.push_back(state.entry.getArg(0));
    for (const auto &argument : call.arguments) {
        operands.push_back(value_of(state, argument));
    }
    operands.push_back(array);
    const auto service = call.arguments.size() == 2 ? frame_service<services::ApplyList>(state, 2, true)
                                                    : frame_service<services::CallList>(state, 3, true);
    return transfer(state, in_tail(state, expression), service, operands, array);
}
} // namespace clause::codegen
