#include "lowering_state.hpp"
#include <algorithm>
#include <clause/abi/equality.hpp>
#include <clause/abi/term.hpp>
#include <optional>
#include <utility>

// List loops (OPT1.7, docs/funs.md#inlined-funs): lists:map/2 and its kin called with a known anonymous fun become a
// loop in the calling function with the fun inlined (inline funs, OPT1.8), as OTP's inline_list_funcs does. The
// results and error reasons are those of Clause's lists module; only stack traces differ.
namespace clause::codegen {
namespace {
// How a list function treats its input: `checked` ones raise case_clause for a non-list (the real call raises it),
// `reversed` ones walk it from its last element (after a first pass reversing it), `plain` ones walk it as given.
enum class Input : std::uint8_t { checked, plain, reversed };
// What an improper tail raises: function_clause from a lists helper, or what the real call raises from there.
enum class Improper : std::uint8_t { function_clause, call };

struct ListLoop;

struct ListFunction {
    // The lists function, how it reads its input and what its improper tail raises.
    std::u32string_view name;
    std::size_t arity;
    Input input;
    Improper improper;
    // One element's step, which continues at `next` or leaves for the loop's end, and the value once the input is
    // exhausted.
    void (*step)(ExpressionLowering &state, const ListLoop &loop, llvm::Value *element, llvm::BasicBlock *next);
    llvm::Value *(*finish)(ExpressionLowering &state, const ListLoop &loop);
};

struct ListLoop {
    // The lists function, the fun inlined and the resolved call, kept for the paths that make the real call.
    const ListFunction *function;
    const semantic::FunEntry *lambda;
    semantic::FunctionRef callee;
    // lists:reverse/2, which flatmap/2 prepends each element's list with.
    semantic::FunctionRef reverse;
    // The fun argument's value; null for a fun written in the call, which is built only where it is needed.
    llvm::Value *fun = nullptr;
    // Frame slots: the input left, the fold accumulator, whether it is a map only the loop holds (OPT3.15), the
    // reversed list built so far and the result.
    llvm::Value *rest = nullptr;
    llvm::Value *accumulator = nullptr;
    llvm::Value *owned = nullptr;
    llvm::Value *output = nullptr;
    llvm::Value *result = nullptr;
    // Where the loop leaves with the result in its slot.
    llvm::BasicBlock *done = nullptr;
};

llvm::BasicBlock *block(const ExpressionLowering &state, const char *name) {
    return llvm::BasicBlock::Create(state.entry.getContext(), name, &state.entry);
}

// Loop state lives in frame slots, so no SSA value crosses an iteration or an Erlang call.
llvm::Value *load(ExpressionLowering &state, llvm::Value *slot, const char *name) {
    auto *value = state.builder.CreateAlignedLoad(state.word, slot, llvm::Align(state.word->getBitWidth() / 8), name);
    mark_term(state, value);
    return value;
}

void store(const ExpressionLowering &state, llvm::Value *value, llvm::Value *slot) {
    state.builder.CreateAlignedStore(value, slot, llvm::Align(state.word->getBitWidth() / 8));
}

llvm::Value *word_equal(ExpressionLowering &state, llvm::Value *value, std::uint64_t word) {
    return state.builder.Insert(llvm::CmpInst::Create(llvm::Instruction::ICmp, llvm::CmpInst::ICMP_EQ, value,
                                                      llvm::ConstantInt::get(state.word, word)));
}

llvm::Value *atom(ExpressionLowering &state, const char32_t *name) { return lower_atom(state, ast::Atom{name}); }

llvm::Value *is_atom(ExpressionLowering &state, llvm::Value *value, const char32_t *name) {
    return lower_exact(state, value, atom(state, name));
}

// The fun's value: the argument's, or the fun written in the call built here.
llvm::Value *fun_value(ExpressionLowering &state, const ListLoop &loop) {
    return loop.fun ? loop.fun : lower_fun(state, *loop.lambda->expression);
}

// The fun applied to `arguments`, its clauses inlined.
llvm::Value *apply(ExpressionLowering &state, const ListLoop &loop, std::span<llvm::Value *const> arguments) {
    return inline_lambda(state, *loop.lambda, arguments, [&] { return fun_value(state, loop); });
}

// The real lists call on the input left `list` (with the accumulator for a fold), which raises its own error.
llvm::Value *real_call(ExpressionLowering &state, const ListLoop &loop, llvm::Value *list) {
    std::vector<llvm::Value *> values{fun_value(state, loop)};
    if (loop.accumulator) {
        values.push_back(load(state, loop.accumulator, "list.accumulator"));
    }
    values.push_back(list);
    return lower_named_call(state, loop.callee, values);
}

// Leave the loop with the real call's value on `list`.
void leave_by_call(ExpressionLowering &state, const ListLoop &loop, llvm::Value *list) {
    store(state, real_call(state, loop, list), loop.result);
    state.builder.CreateBr(loop.done);
}

// Leave the loop with `value`.
void leave(ExpressionLowering &state, const ListLoop &loop, llvm::Value *value) {
    store(state, value, loop.result);
    state.builder.CreateBr(loop.done);
}

void push(ExpressionLowering &state, const ListLoop &loop, llvm::Value *value) {
    store(state, lower_list(state, std::array{value, load(state, loop.output, "list.output")}), loop.output);
}

// Continue at `next` when `value` is true, at `other` when it is false; anything else raises {case_clause, Value}
// (all/2 and any/2) or {bad_filter, Value} (filter/2, a comprehension in lists).
void boolean(ExpressionLowering &state, llvm::Value *value, std::array<llvm::BasicBlock *, 2> targets,
             abi::v1::ErrorReason reason) {
    auto *not_true = block(state, "list.not_true");
    auto *bad = block(state, "list.not_boolean");
    state.builder.CreateCondBr(is_atom(state, value, U"true"), targets[0], not_true);
    state.builder.SetInsertPoint(not_true);
    state.builder.CreateCondBr(is_atom(state, value, U"false"), targets[1], bad);
    state.builder.SetInsertPoint(bad);
    raise_reason(state, reason, value);
}

void map_step(ExpressionLowering &state, const ListLoop &loop, llvm::Value *element, llvm::BasicBlock *next) {
    push(state, loop, apply(state, loop, std::array{element}));
    state.builder.CreateBr(next);
}

void filter_step(ExpressionLowering &state, const ListLoop &loop, llvm::Value *element, llvm::BasicBlock *next) {
    auto *keep = block(state, "list.keep");
    boolean(state, apply(state, loop, std::array{element}), {keep, next}, abi::v1::ErrorReason::bad_filter);
    state.builder.SetInsertPoint(keep);
    push(state, loop, element);
    state.builder.CreateBr(next);
}

void foreach_step(ExpressionLowering &state, const ListLoop &loop, llvm::Value *element, llvm::BasicBlock *next) {
    apply(state, loop, std::array{element});
    state.builder.CreateBr(next);
}

// all/2 leaves with false at the first false, any/2 with true at the first true.
void all_step(ExpressionLowering &state, const ListLoop &loop, llvm::Value *element, llvm::BasicBlock *next) {
    auto *stop = block(state, "list.stop");
    boolean(state, apply(state, loop, std::array{element}), {next, stop}, abi::v1::ErrorReason::case_clause);
    state.builder.SetInsertPoint(stop);
    leave(state, loop, atom(state, U"false"));
}

void any_step(ExpressionLowering &state, const ListLoop &loop, llvm::Value *element, llvm::BasicBlock *next) {
    auto *stop = block(state, "list.stop");
    boolean(state, apply(state, loop, std::array{element}), {stop, next}, abi::v1::ErrorReason::case_clause);
    state.builder.SetInsertPoint(stop);
    leave(state, loop, atom(state, U"true"));
}

// flatmap/2 prepends each list the fun returns reversed, as lists:flatmap_onto/3 does with lists:reverse/2.
void flatmap_step(ExpressionLowering &state, const ListLoop &loop, llvm::Value *element, llvm::BasicBlock *next) {
    auto *value = apply(state, loop, std::array{element});
    store(state, lower_named_call(state, loop.reverse, std::array{value, load(state, loop.output, "list.output")}),
          loop.output);
    state.builder.CreateBr(next);
}

void fold_step(ExpressionLowering &state, const ListLoop &loop, llvm::Value *element, llvm::BasicBlock *next) {
    const std::array arguments{element, load(state, loop.accumulator, "list.accumulator")};
    store(state, apply_fold(state, *loop.lambda, arguments, loop.owned, [&] { return fun_value(state, loop); }),
          loop.accumulator);
    state.builder.CreateBr(next);
}

// mapfoldl/3 and mapfoldr/3 match {Mapped, Next} = Fun(Element, Acc); anything else raises {badmatch, Value}.
void mapfold_step(ExpressionLowering &state, const ListLoop &loop, llvm::Value *element, llvm::BasicBlock *next) {
    auto *value = apply(state, loop, std::array{element, load(state, loop.accumulator, "list.accumulator")});
    auto *pair = block(state, "list.pair");
    auto *bad = block(state, "list.badmatch");
    inline_tuple_shape(state, value, 2, pair, bad);
    state.builder.SetInsertPoint(bad);
    raise_reason(state, abi::v1::ErrorReason::badmatch, value);
    state.builder.SetInsertPoint(pair);
    auto *mapped = inline_tuple_element(state, value, 0);
    auto *accumulated = inline_tuple_element(state, value, 1);
    mark_term(state, mapped);
    mark_term(state, accumulated);
    push(state, loop, mapped);
    store(state, accumulated, loop.accumulator);
    state.builder.CreateBr(next);
}

llvm::Value *reversed_output(ExpressionLowering &state, const ListLoop &loop) {
    return lower_reverse(state, load(state, loop.output, "list.output"));
}

llvm::Value *finish_ok(ExpressionLowering &state, const ListLoop & /*loop*/) { return atom(state, U"ok"); }

llvm::Value *finish_true(ExpressionLowering &state, const ListLoop & /*loop*/) { return atom(state, U"true"); }

llvm::Value *finish_false(ExpressionLowering &state, const ListLoop & /*loop*/) { return atom(state, U"false"); }

llvm::Value *finish_fold(ExpressionLowering &state, const ListLoop &loop) {
    return load(state, loop.accumulator, "list.accumulator");
}

// mapfoldl/3 built its list reversed; mapfoldr/3 walked the input reversed, so its list is in order.
llvm::Value *finish_mapfoldl(ExpressionLowering &state, const ListLoop &loop) {
    auto *mapped = reversed_output(state, loop);
    return lower_tuple(state, std::array{mapped, finish_fold(state, loop)});
}

llvm::Value *finish_mapfoldr(ExpressionLowering &state, const ListLoop &loop) {
    auto *mapped = load(state, loop.output, "list.output");
    return lower_tuple(state, std::array{mapped, finish_fold(state, loop)});
}

constexpr std::array LIST_FUNCTIONS{
    ListFunction{U"map", 2, Input::checked, Improper::function_clause, map_step, reversed_output},
    ListFunction{U"filter", 2, Input::plain, Improper::call, filter_step, reversed_output},
    ListFunction{U"foreach", 2, Input::plain, Improper::call, foreach_step, finish_ok},
    ListFunction{U"all", 2, Input::checked, Improper::function_clause, all_step, finish_true},
    ListFunction{U"any", 2, Input::checked, Improper::function_clause, any_step, finish_false},
    ListFunction{U"flatmap", 2, Input::plain, Improper::function_clause, flatmap_step, reversed_output},
    ListFunction{U"foldl", 3, Input::checked, Improper::function_clause, fold_step, finish_fold},
    ListFunction{U"foldr", 3, Input::reversed, Improper::call, fold_step, finish_fold},
    ListFunction{U"mapfoldl", 3, Input::plain, Improper::call, mapfold_step, finish_mapfoldl},
    ListFunction{U"mapfoldr", 3, Input::reversed, Improper::call, mapfold_step, finish_mapfoldr},
};

// The exported, live function `name`/`arity` of `module`; a null function when there is none.
semantic::FunctionRef exported(semantic::Module &module, std::u32string_view name, std::size_t arity) {
    for (auto &function : module.functions) {
        if (function.key.name == name && function.key.arity == arity && function.exported && function.live_) {
            return {&module, &function};
        }
    }
    return {&module, nullptr};
}

struct ListCall {
    // The lists function called, the known fun and the fun written in the call (null for a variable).
    const ListFunction *function;
    KnownLambda known;
    semantic::FunctionRef callee;
    semantic::FunctionRef reverse;
};

// The function of Clause's own lists module a call names, when it is one of LIST_FUNCTIONS.
std::optional<std::pair<semantic::FunctionRef, const ListFunction *>> lists_callee(const ExpressionLowering &state,
                                                                                   const ast::Expression &expression) {
    const auto callee = state.inferred.callees.find(&expression);
    if (callee == state.inferred.callees.end() || state.function.services.contains(&expression) ||
        !callee->second.module->library_ || callee->second.module->name != U"lists") {
        return std::nullopt;
    }
    const auto &key = callee->second.function->key;
    const auto found = std::ranges::find_if(
        LIST_FUNCTIONS, [&](const ListFunction &entry) { return entry.name == key.name && entry.arity == key.arity; });
    return found == LIST_FUNCTIONS.end() ? std::nullopt : std::optional{std::pair{callee->second, &*found}};
}

// A call of Clause's own lists module that becomes a loop: its function and the known fun of its first argument.
std::optional<ListCall> list_call(const ExpressionLowering &state, const ast::Expression &expression,
                                  const ast::CallExpression &call) {
    const auto callee = lists_callee(state, expression);
    if (!callee || call.arguments.size() != callee->second->arity) {
        return std::nullopt;
    }
    const auto [ref, function] = *callee;
    const auto known = known_lambda(state, call.arguments.front(), function->arity - 1);
    const auto reverse = exported(*ref.module, U"reverse", 2);
    if (!known.lambda || (function->step == flatmap_step && !reverse.function)) {
        return std::nullopt;
    }
    return ListCall{function, known, ref, reverse};
}

// The loop's slots, filled with the call's arguments.
ListLoop begin(ExpressionLowering &state, const ListCall &found, const ast::CallExpression &call) {
    const auto &syntax = *state.module.syntax;
    const auto value = [&](const ast::ExprId &id) { return state.values.at(&syntax.expression(id)); };
    ListLoop loop{found.function, found.known.lambda, found.callee, found.reverse};
    loop.fun = found.known.literal ? nullptr : value(call.arguments.front());
    loop.rest = root_slot(state);
    store(state, value(call.arguments.back()), loop.rest);
    if (call.arguments.size() == 3) {
        loop.accumulator = root_slot(state);
        store(state, value(call.arguments[1]), loop.accumulator);
        loop.owned = fold_owned_slot(state);
    }
    loop.output = root_slot(state);
    store(state, llvm::ConstantInt::get(state.word, abi::v1::empty_list), loop.output);
    loop.result = root_slot(state);
    loop.done = block(state, "list.done");
    return loop;
}

// A checked input that is no list leaves by the real call, which raises {case_clause, Input}.
void check_input(ExpressionLowering &state, const ListLoop &loop) {
    auto *input = load(state, loop.rest, "list.input");
    auto *start = block(state, "list.start");
    auto *other = block(state, "list.other");
    auto *bad = block(state, "list.bad_input");
    state.builder.CreateCondBr(inline_cons_test(state, input), start, other);
    state.builder.SetInsertPoint(other);
    state.builder.CreateCondBr(word_equal(state, input, abi::v1::empty_list), start, bad);
    state.builder.SetInsertPoint(bad);
    leave_by_call(state, loop, input);
    state.builder.SetInsertPoint(start);
}

struct Iteration {
    // A cons cell's head, which its step reads, and the block reached at anything else, with that word.
    llvm::Value *element;
    llvm::BasicBlock *end;
    llvm::Value *input;
};

// A loop head at `head` over the input slot: a cons cell continues in `step` with the rest stored back.
Iteration iterate(ExpressionLowering &state, const ListLoop &loop, llvm::BasicBlock *head) {
    state.builder.CreateBr(head);
    state.builder.SetInsertPoint(head);
    // Like the body recursion it replaces, which builds its result without a safepoint, a list loop collects only
    // where it yields and in the calls of its fun.
    loop_reduction(state);
    auto *input = load(state, loop.rest, "list.input");
    auto *step = block(state, "list.step");
    auto *end = block(state, "list.end");
    state.builder.CreateCondBr(inline_cons_test(state, input), step, end);
    state.builder.SetInsertPoint(step);
    auto *element = inline_cons_word(state, input, 0);
    mark_term(state, element);
    auto *tail = inline_cons_word(state, input, 1);
    mark_term(state, tail);
    store(state, tail, loop.rest);
    return {element, end, input};
}

// A reversed input is reversed into the input slot first; an improper tail leaves by the real call, which raises
// function_clause before the fun runs, as lists' body recursion does.
void reverse_input(ExpressionLowering &state, const ListLoop &loop) {
    auto *reversed = root_slot(state);
    store(state, llvm::ConstantInt::get(state.word, abi::v1::empty_list), reversed);
    auto *head = block(state, "list.reverse");
    const auto iteration = iterate(state, loop, head);
    store(state, lower_list(state, std::array{iteration.element, load(state, reversed, "list.reversed")}), reversed);
    state.builder.CreateBr(head);
    state.builder.SetInsertPoint(iteration.end);
    auto *proper = block(state, "list.reversed");
    auto *bad = block(state, "list.improper");
    state.builder.CreateCondBr(word_equal(state, iteration.input, abi::v1::empty_list), proper, bad);
    state.builder.SetInsertPoint(bad);
    leave_by_call(state, loop, iteration.input);
    state.builder.SetInsertPoint(proper);
    store(state, load(state, reversed, "list.reversed"), loop.rest);
}

// An improper tail raises function_clause, as the lists helper walking it does, or leaves by the real call on it.
void improper_tail(ExpressionLowering &state, const ListLoop &loop, llvm::Value *input) {
    if (loop.function->improper == Improper::function_clause) {
        raise_function_clause(state);
    } else {
        leave_by_call(state, loop, input);
    }
}

// The loop over the input: each element's step, then the result once the input ends.
void run(ExpressionLowering &state, const ListLoop &loop) {
    auto *head = block(state, "list.next");
    const auto iteration = iterate(state, loop, head);
    loop.function->step(state, loop, iteration.element, head);
    state.builder.SetInsertPoint(iteration.end);
    if (loop.function->input != Input::reversed) {
        auto *proper = block(state, "list.exhausted");
        auto *bad = block(state, "list.improper");
        state.builder.CreateCondBr(word_equal(state, iteration.input, abi::v1::empty_list), proper, bad);
        state.builder.SetInsertPoint(bad);
        improper_tail(state, loop, iteration.input);
        state.builder.SetInsertPoint(proper);
    }
    leave(state, loop, loop.function->finish(state, loop));
}
} // namespace

const ast::Expression *sunk_list_fun(const ExpressionLowering &state, const ast::Expression &expression,
                                     const ast::CallExpression &call) {
    const auto found = list_call(state, expression, call);
    return found ? found->known.literal : nullptr;
}

llvm::Value *lower_list_call(ExpressionLowering &state, const ast::Expression &expression,
                             const ast::CallExpression &call) {
    const auto found = list_call(state, expression, call);
    if (!found) {
        return nullptr;
    }
    const auto loop = begin(state, *found, call);
    if (found->function->input == Input::checked) {
        check_input(state, loop);
    } else if (found->function->input == Input::reversed) {
        reverse_input(state, loop);
    }
    run(state, loop);
    state.builder.SetInsertPoint(loop.done);
    return load(state, loop.result, "list.result");
}
} // namespace clause::codegen
