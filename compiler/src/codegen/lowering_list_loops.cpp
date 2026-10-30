#include "lowering_state.hpp"
#include <algorithm>
#include <clause/abi/equality.hpp>
#include <clause/abi/term.hpp>
#include <optional>
#include <span>
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

struct Visit {
    // A cons cell's head, which its step reads, and the cell itself, which dropwhile/2 and splitwith/2 return.
    llvm::Value *element;
    llvm::Value *cell;
    // The heads of every list a zip loop walks in step (zip loops); empty for one list.
    std::span<llvm::Value *const> heads;
};

struct ListFunction {
    // The lists function, how it reads its input and what its improper tail raises.
    std::u32string_view name;
    std::size_t arity;
    Input input;
    Improper improper;
    // One element's step, which continues at `next` or leaves for the loop's end, and the value once the input is
    // exhausted.
    void (*step)(ExpressionLowering &state, const ListLoop &loop, const Visit &visit, llvm::BasicBlock *next);
    llvm::Value *(*finish)(ExpressionLowering &state, const ListLoop &loop);
    // How many lists it walks in step, its last arguments: two for zipwith/3, three for zipwith3/4 (zip loops).
    std::size_t lists = 1;
};

struct ListLoop {
    // The lists function, the fun inlined and the resolved call, kept for the paths that make the real call.
    const ListFunction *function;
    const semantic::FunEntry *lambda;
    semantic::FunctionRef callee;
    // lists:reverse/2, which flatmap/2 prepends each element's list with.
    semantic::FunctionRef reverse;
    // The fun argument's value; null for a fun written in the call or sunk, which is built only where it is needed.
    llvm::Value *fun = nullptr;
    // Frame slots: the input left, the fold accumulator, whether it is a map only the loop holds (OPT3.15), the list
    // built so far and its last cell (built forward, OPT1.12; reversed for flatmap/2 and mapfoldr/3), partition/2's
    // list of the other elements and its last cell, and the result.
    llvm::Value *rest = nullptr;
    // The other lists a zip loop walks in step with `rest` (zip loops).
    std::array<llvm::Value *, 2> zipped{};
    llvm::Value *accumulator = nullptr;
    llvm::Value *owned = nullptr;
    llvm::Value *output = nullptr;
    llvm::Value *output_last = nullptr;
    llvm::Value *others = nullptr;
    llvm::Value *others_last = nullptr;
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

// The fun's value: the argument's, or the fun written in the call (or sunk) built here.
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

// Prepend `value` to the reversed list in `slot`.
void push(ExpressionLowering &state, llvm::Value *slot, llvm::Value *value) {
    store(state, lower_list(state, std::array{value, load(state, slot, "list.output")}), slot);
}

// Append `value` to the list the loop builds, or to partition/2's other list (forward-built lists, OPT1.12).
void append_output(ExpressionLowering &state, const ListLoop &loop, llvm::Value *value) {
    append_cell(state, {loop.output, loop.output_last}, value);
}

void append_other(ExpressionLowering &state, const ListLoop &loop, llvm::Value *value) {
    append_cell(state, {loop.others, loop.others_last}, value);
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

// The predicate applied to the element, branching as boolean/4 does; a value that is no boolean is a case_clause.
void predicate(ExpressionLowering &state, const ListLoop &loop, const Visit &visit,
               std::array<llvm::BasicBlock *, 2> targets) {
    boolean(state, apply(state, loop, std::array{visit.element}), targets, abi::v1::ErrorReason::case_clause);
}

void map_step(ExpressionLowering &state, const ListLoop &loop, const Visit &visit, llvm::BasicBlock *next) {
    append_output(state, loop, apply(state, loop, std::array{visit.element}));
    state.builder.CreateBr(next);
}

void filter_step(ExpressionLowering &state, const ListLoop &loop, const Visit &visit, llvm::BasicBlock *next) {
    auto *keep = block(state, "list.keep");
    boolean(state, apply(state, loop, std::array{visit.element}), {keep, next}, abi::v1::ErrorReason::bad_filter);
    state.builder.SetInsertPoint(keep);
    append_output(state, loop, visit.element);
    state.builder.CreateBr(next);
}

// filtermap/2 (and zf/2, its old name; more list loops) keeps the element for true and Value for {true, Value};
// anything else but false raises {case_clause, Value}, as the case in lists does.
void filtermap_step(ExpressionLowering &state, const ListLoop &loop, const Visit &visit, llvm::BasicBlock *next) {
    auto *value = apply(state, loop, std::array{visit.element});
    auto *keep = block(state, "list.keep");
    auto *not_true = block(state, "list.not_true");
    auto *not_false = block(state, "list.not_false");
    auto *pair = block(state, "list.pair");
    auto *replace = block(state, "list.replace");
    auto *bad = block(state, "list.not_kept");
    state.builder.CreateCondBr(is_atom(state, value, U"true"), keep, not_true);
    state.builder.SetInsertPoint(not_true);
    state.builder.CreateCondBr(is_atom(state, value, U"false"), next, not_false);
    state.builder.SetInsertPoint(not_false);
    inline_tuple_shape(state, value, 2, pair, bad);
    state.builder.SetInsertPoint(pair);
    auto *tag = inline_tuple_element(state, value, 0);
    state.builder.CreateCondBr(is_atom(state, tag, U"true"), replace, bad);
    state.builder.SetInsertPoint(replace);
    auto *replaced = inline_tuple_element(state, value, 1);
    mark_term(state, replaced);
    append_output(state, loop, replaced);
    state.builder.CreateBr(next);
    state.builder.SetInsertPoint(keep);
    append_output(state, loop, visit.element);
    state.builder.CreateBr(next);
    state.builder.SetInsertPoint(bad);
    raise_reason(state, abi::v1::ErrorReason::case_clause, value);
}

// partition/2 prepends each element to the satisfying or the other list (more list loops).
void partition_step(ExpressionLowering &state, const ListLoop &loop, const Visit &visit, llvm::BasicBlock *next) {
    auto *keep = block(state, "list.keep");
    auto *other = block(state, "list.other");
    predicate(state, loop, visit, {keep, other});
    state.builder.SetInsertPoint(keep);
    append_output(state, loop, visit.element);
    state.builder.CreateBr(next);
    state.builder.SetInsertPoint(other);
    append_other(state, loop, visit.element);
    state.builder.CreateBr(next);
}

void foreach_step(ExpressionLowering &state, const ListLoop &loop, const Visit &visit, llvm::BasicBlock *next) {
    apply(state, loop, std::array{visit.element});
    state.builder.CreateBr(next);
}

// all/2 leaves with false at the first false, any/2 with true at the first true.
void all_step(ExpressionLowering &state, const ListLoop &loop, const Visit &visit, llvm::BasicBlock *next) {
    auto *stop = block(state, "list.stop");
    predicate(state, loop, visit, {next, stop});
    state.builder.SetInsertPoint(stop);
    leave(state, loop, atom(state, U"false"));
}

void any_step(ExpressionLowering &state, const ListLoop &loop, const Visit &visit, llvm::BasicBlock *next) {
    auto *stop = block(state, "list.stop");
    predicate(state, loop, visit, {stop, next});
    state.builder.SetInsertPoint(stop);
    leave(state, loop, atom(state, U"true"));
}

// search/2 leaves with {value, Element} at the first element the predicate holds for (more list loops).
void search_step(ExpressionLowering &state, const ListLoop &loop, const Visit &visit, llvm::BasicBlock *next) {
    auto *found = block(state, "list.found");
    predicate(state, loop, visit, {found, next});
    state.builder.SetInsertPoint(found);
    leave(state, loop, lower_tuple(state, std::array{atom(state, U"value"), visit.element}));
}

// The list the loop built forward (forward-built lists, OPT1.12).
llvm::Value *output_list(ExpressionLowering &state, const ListLoop &loop) {
    return load(state, loop.output, "list.output");
}

// flatmap/2 prepends each list reversed, so its list is reversed at the end.
llvm::Value *reversed_output(ExpressionLowering &state, const ListLoop &loop) {
    return lower_reverse(state, load(state, loop.output, "list.output"));
}

// takewhile/2 and splitwith/2 keep the elements while the predicate holds; this leaves the builder where it first
// fails (more list loops).
void keep_while(ExpressionLowering &state, const ListLoop &loop, const Visit &visit, llvm::BasicBlock *next) {
    auto *keep = block(state, "list.keep");
    auto *stop = block(state, "list.stop");
    predicate(state, loop, visit, {keep, stop});
    state.builder.SetInsertPoint(keep);
    append_output(state, loop, visit.element);
    state.builder.CreateBr(next);
    state.builder.SetInsertPoint(stop);
}

void takewhile_step(ExpressionLowering &state, const ListLoop &loop, const Visit &visit, llvm::BasicBlock *next) {
    keep_while(state, loop, visit, next);
    leave(state, loop, output_list(state, loop));
}

// splitwith/2 leaves with {Prefix, Cell} at the first element the predicate fails for.
void splitwith_step(ExpressionLowering &state, const ListLoop &loop, const Visit &visit, llvm::BasicBlock *next) {
    keep_while(state, loop, visit, next);
    auto *prefix = output_list(state, loop);
    leave(state, loop, lower_tuple(state, std::array{prefix, visit.cell}));
}

// dropwhile/2 leaves with the cell of the first element the predicate fails for (more list loops).
void dropwhile_step(ExpressionLowering &state, const ListLoop &loop, const Visit &visit, llvm::BasicBlock *next) {
    auto *stop = block(state, "list.stop");
    predicate(state, loop, visit, {next, stop});
    state.builder.SetInsertPoint(stop);
    leave(state, loop, visit.cell);
}

// zipwith/3 and zipwith3/4 prepend the fun's value for the heads of all their lists (zip loops).
void zip_step(ExpressionLowering &state, const ListLoop &loop, const Visit &visit, llvm::BasicBlock *next) {
    append_output(state, loop, apply(state, loop, visit.heads));
    state.builder.CreateBr(next);
}

// flatmap/2 prepends each list the fun returns reversed, as lists:flatmap_onto/3 does with lists:reverse/2.
void flatmap_step(ExpressionLowering &state, const ListLoop &loop, const Visit &visit, llvm::BasicBlock *next) {
    auto *value = apply(state, loop, std::array{visit.element});
    store(state, lower_named_call(state, loop.reverse, std::array{value, load(state, loop.output, "list.output")}),
          loop.output);
    state.builder.CreateBr(next);
}

void fold_step(ExpressionLowering &state, const ListLoop &loop, const Visit &visit, llvm::BasicBlock *next) {
    const std::array arguments{visit.element, load(state, loop.accumulator, "list.accumulator")};
    store(state, apply_fold(state, *loop.lambda, arguments, loop.owned, [&] { return fun_value(state, loop); }),
          loop.accumulator);
    state.builder.CreateBr(next);
}

// mapfoldl/3 and mapfoldr/3 match {Mapped, Next} = Fun(Element, Acc); anything else raises {badmatch, Value}.
void mapfold_step(ExpressionLowering &state, const ListLoop &loop, const Visit &visit, llvm::BasicBlock *next) {
    auto *value = apply(state, loop, std::array{visit.element, load(state, loop.accumulator, "list.accumulator")});
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
    // mapfoldr/3 walks the input from its last element, so it prepends.
    if (loop.function->input == Input::reversed) {
        push(state, loop.output, mapped);
    } else {
        append_output(state, loop, mapped);
    }
    store(state, accumulated, loop.accumulator);
    state.builder.CreateBr(next);
}

llvm::Value *finish_ok(ExpressionLowering &state, const ListLoop & /*loop*/) { return atom(state, U"ok"); }

llvm::Value *finish_true(ExpressionLowering &state, const ListLoop & /*loop*/) { return atom(state, U"true"); }

llvm::Value *finish_false(ExpressionLowering &state, const ListLoop & /*loop*/) { return atom(state, U"false"); }

llvm::Value *finish_empty(ExpressionLowering &state, const ListLoop & /*loop*/) {
    return llvm::ConstantInt::get(state.word, abi::v1::empty_list);
}

llvm::Value *finish_fold(ExpressionLowering &state, const ListLoop &loop) {
    return load(state, loop.accumulator, "list.accumulator");
}

// partition/2 returns {Satisfying, Others}, splitwith/2 the whole input as its prefix with an empty rest.
llvm::Value *finish_partition(ExpressionLowering &state, const ListLoop &loop) {
    auto *satisfying = output_list(state, loop);
    auto *others = load(state, loop.others, "list.others");
    return lower_tuple(state, std::array{satisfying, others});
}

llvm::Value *finish_splitwith(ExpressionLowering &state, const ListLoop &loop) {
    auto *prefix = output_list(state, loop);
    return lower_tuple(state, std::array{prefix, finish_empty(state, loop)});
}

// mapfoldl/3 built its list forward; mapfoldr/3 walked the input reversed, so its list is in order.
llvm::Value *finish_mapfoldl(ExpressionLowering &state, const ListLoop &loop) {
    auto *mapped = output_list(state, loop);
    return lower_tuple(state, std::array{mapped, finish_fold(state, loop)});
}

llvm::Value *finish_mapfoldr(ExpressionLowering &state, const ListLoop &loop) {
    auto *mapped = load(state, loop.output, "list.output");
    return lower_tuple(state, std::array{mapped, finish_fold(state, loop)});
}

constexpr std::array LIST_FUNCTIONS{
    ListFunction{U"map", 2, Input::checked, Improper::function_clause, map_step, output_list},
    ListFunction{U"filter", 2, Input::plain, Improper::call, filter_step, output_list},
    ListFunction{U"filtermap", 2, Input::plain, Improper::function_clause, filtermap_step, output_list},
    ListFunction{U"zf", 2, Input::plain, Improper::function_clause, filtermap_step, output_list},
    ListFunction{U"partition", 2, Input::plain, Improper::function_clause, partition_step, finish_partition},
    ListFunction{U"foreach", 2, Input::plain, Improper::call, foreach_step, finish_ok},
    ListFunction{U"all", 2, Input::checked, Improper::function_clause, all_step, finish_true},
    ListFunction{U"any", 2, Input::checked, Improper::function_clause, any_step, finish_false},
    ListFunction{U"search", 2, Input::plain, Improper::function_clause, search_step, finish_false},
    ListFunction{U"takewhile", 2, Input::plain, Improper::function_clause, takewhile_step, output_list},
    ListFunction{U"dropwhile", 2, Input::plain, Improper::function_clause, dropwhile_step, finish_empty},
    ListFunction{U"splitwith", 2, Input::plain, Improper::function_clause, splitwith_step, finish_splitwith},
    ListFunction{U"flatmap", 2, Input::plain, Improper::function_clause, flatmap_step, reversed_output},
    ListFunction{U"foldl", 3, Input::checked, Improper::function_clause, fold_step, finish_fold},
    ListFunction{U"foldr", 3, Input::reversed, Improper::call, fold_step, finish_fold},
    ListFunction{U"mapfoldl", 3, Input::plain, Improper::call, mapfold_step, finish_mapfoldl},
    ListFunction{U"mapfoldr", 3, Input::reversed, Improper::call, mapfold_step, finish_mapfoldr},
    ListFunction{U"zipwith", 3, Input::plain, Improper::function_clause, zip_step, output_list, 2},
    ListFunction{U"zipwith3", 4, Input::plain, Improper::function_clause, zip_step, output_list, 3},
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
    // The lists function called and the known fun with its argument the walk does not lower.
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

// A new frame slot holding `value`.
llvm::Value *slot_of(ExpressionLowering &state, llvm::Value *value) {
    auto *slot = root_slot(state);
    store(state, value, slot);
    return slot;
}

// The loop's slots, filled with the call's arguments: the fun, a fold's accumulator, then the lists.
ListLoop begin(ExpressionLowering &state, const ListCall &found, const ast::CallExpression &call) {
    const auto &syntax = *state.module.syntax;
    const auto value = [&](std::size_t index) { return state.values.at(&syntax.expression(call.arguments[index])); };
    ListLoop loop{found.function, found.known.lambda, found.callee, found.reverse};
    loop.fun = found.known.unlowered ? nullptr : value(0);
    const auto first = call.arguments.size() - found.function->lists;
    loop.rest = slot_of(state, value(first));
    for (std::size_t index = 1; index < found.function->lists; ++index) {
        loop.zipped.at(index - 1) = slot_of(state, value(first + index));
    }
    if (first == 2) {
        loop.accumulator = slot_of(state, value(1));
        loop.owned = fold_owned_slot(state);
    }
    auto *empty = llvm::ConstantInt::get(state.word, abi::v1::empty_list);
    loop.output = root_slot(state);
    store(state, empty, loop.output);
    loop.output_last = root_slot(state);
    store(state, empty, loop.output_last);
    if (found.function->step == partition_step) {
        loop.others = root_slot(state);
        store(state, empty, loop.others);
        loop.others_last = root_slot(state);
        store(state, empty, loop.others_last);
    }
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
    loop.function->step(state, loop, Visit{.element = iteration.element, .cell = iteration.input, .heads = {}}, head);
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

// The loop over one list, reversed or checked first when the lists function does that.
void run_single(ExpressionLowering &state, const ListLoop &loop) {
    if (loop.function->input == Input::checked) {
        check_input(state, loop);
    } else if (loop.function->input == Input::reversed) {
        reverse_input(state, loop);
    }
    run(state, loop);
}

// The heads of a zip loop's lists, each tail stored back, once every list is a cons cell; any other word continues
// at `end` with the slots unchanged (zip loops).
std::array<llvm::Value *, 3> zip_heads(ExpressionLowering &state, std::span<llvm::Value *const> slots,
                                       llvm::BasicBlock *end) {
    std::array<llvm::Value *, 3> cells{};
    for (std::size_t index = 0; index < slots.size(); ++index) {
        cells.at(index) = load(state, slots[index], "list.input");
        auto *more = block(state, "list.zip_more");
        state.builder.CreateCondBr(inline_cons_test(state, cells.at(index)), more, end);
        state.builder.SetInsertPoint(more);
    }
    std::array<llvm::Value *, 3> heads{};
    for (std::size_t index = 0; index < slots.size(); ++index) {
        heads.at(index) = inline_cons_word(state, cells.at(index), 0);
        mark_term(state, heads.at(index));
        auto *tail = inline_cons_word(state, cells.at(index), 1);
        mark_term(state, tail);
        store(state, tail, slots[index]);
    }
    return heads;
}

// Lists of the same length end together with the result; any other end is the function_clause of lists' walk.
void zip_end(ExpressionLowering &state, const ListLoop &loop, std::span<llvm::Value *const> slots) {
    llvm::Value *ended = nullptr;
    for (auto *slot : slots) {
        auto *empty = word_equal(state, load(state, slot, "list.input"), abi::v1::empty_list);
        ended = ended ? state.builder.CreateAnd(ended, empty) : empty;
    }
    auto *proper = block(state, "list.exhausted");
    auto *bad = block(state, "list.improper");
    state.builder.CreateCondBr(ended, proper, bad);
    state.builder.SetInsertPoint(bad);
    raise_function_clause(state);
    state.builder.SetInsertPoint(proper);
    leave(state, loop, loop.function->finish(state, loop));
}

// The loop over two or three lists in step, as zipwith/3 and zipwith3/4 walk them (zip loops).
void run_zip(ExpressionLowering &state, const ListLoop &loop) {
    const std::array all{loop.rest, loop.zipped[0], loop.zipped[1]};
    const auto slots = std::span{all}.first(loop.function->lists);
    auto *head = block(state, "list.zip");
    auto *end = block(state, "list.zip_end");
    state.builder.CreateBr(head);
    state.builder.SetInsertPoint(head);
    loop_reduction(state);
    const auto heads = zip_heads(state, slots, end);
    const auto visit = Visit{.element = heads[0], .cell = nullptr, .heads = std::span{heads}.first(slots.size())};
    loop.function->step(state, loop, visit, head);
    state.builder.SetInsertPoint(end);
    zip_end(state, loop, slots);
}
} // namespace

const ast::Expression *sunk_list_fun(const ExpressionLowering &state, const ast::Expression &expression,
                                     const ast::CallExpression &call) {
    const auto found = list_call(state, expression, call);
    return found ? found->known.unlowered : nullptr;
}

llvm::Value *lower_list_call(ExpressionLowering &state, const ast::Expression &expression,
                             const ast::CallExpression &call) {
    const auto found = list_call(state, expression, call);
    if (!found) {
        return nullptr;
    }
    const auto loop = begin(state, *found, call);
    if (found->function->lists > 1) {
        run_zip(state, loop);
    } else {
        run_single(state, loop);
    }
    state.builder.SetInsertPoint(loop.done);
    return load(state, loop.result, "list.result");
}
} // namespace clause::codegen
