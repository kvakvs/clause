#pragma once
#include "../semantic/match_plan.hpp"
#include "../semantic/records.hpp"
#include "alias_tags.hpp"
#include "lowering_expressions.hpp"
#include "lowering_roots.hpp"
#include "proofs.hpp"
#include <array>
#include <bitset>
#include <clause/abi/bits.hpp>
#include <clause/abi/calls.hpp>
#include <clause/abi/containers.hpp>
#include <clause/abi/maps.hpp>
#include <clause/abi/messages.hpp>
#include <clause/abi/records.hpp>
#include <llvm/ADT/APInt.h>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace clause::codegen {
using BindingReads = std::map<const ast::Expression *, semantic::BindingId>;

// The value semantic analysis guarantees `value` holds; a missing one is an internal compiler error.
template <typename Value> const Value &analyzed(const std::optional<Value> &value) {
    if (!value) {
        throw std::logic_error("code generation reached a value semantic analysis did not provide");
    }
    return *value;
}

// The same for a temporary, returned by value so no reference outlives it.
template <typename Value> Value analyzed(std::optional<Value> &&value) {
    if (!value) {
        throw std::logic_error("code generation reached a value semantic analysis did not provide");
    }
    return std::move(*value);
}

// A function whose binary parameter passes the rest of the binary back to itself in tail calls (binary loops,
// OPT1.4C): `variant` loops on one sub-binary cell that its self tail calls update in place.
// The bit length, bit offset and data base address (a word, so no pointer joins blocks) of a word proven to be a
// bitstring (OPT1.4): an inline binary's bytes follow its length word at offset 0; an off-heap view names its
// buffer's first byte and its offset into it.
struct BinaryView {
    llvm::Value *bits;
    llvm::Value *offset;
    llvm::Value *data;
    // The inline or off-heap binary the bits belong to: the word itself, or a sub-binary's original, and that
    // binary's own offset into the data (OPT1.4B).
    llvm::Value *original;
    llvm::Value *base;
};

// A rest of a binary loop's parameter (OPT1.4C) and what a copy of it needs (escaping rests, OPT1.4E).
struct LoopRest {
    // Whether the value is a sub-binary cell only this loop holds (an i1): made here in the function, always in the
    // variant. Only such a cell can enter the variant, and a read that may keep it gets a copy.
    llvm::Value *fresh;
    // The original-relative bit offset and bit length a variant's tail call stores into the parameter's cell, and
    // the original the bits belong to.
    llvm::Value *offset;
    llvm::Value *bits;
    llvm::Value *original;
    // The matched binary and the cursor the rest starts at, from which the bits service makes a copy without room.
    llvm::Value *input;
    llvm::Value *cursor;
};

// How a binary loop's clauses read its parameter (OPT1.4C, escaping rests OPT1.4E).
struct LoopReads {
    // The parameter matched as a binary.
    std::size_t parameter;
    // The reads of a rest that pass it on as that parameter of a self tail call: they keep the cell.
    std::set<const ast::Expression *> passed;
    // Body reads of a rest or of the whole parameter elsewhere: they may keep the value, so they get a copy.
    std::set<const ast::Expression *> copied;
    // The bindings of the whole parameter.
    std::set<semantic::BindingId> wholes;
};

// A byte scan clause of a binary loop (byte scans, OPT1.5): it matches one byte of the parameter whose value is in
// `bytes`, and passes on the rest and every other parameter unchanged or advanced by a constant step.
struct ByteScan {
    // The clause, the bytes it accepts that no earlier clause can match, and each (parameter, step) counter.
    std::size_t clause;
    std::bitset<256> bytes;
    std::vector<std::pair<std::size_t, std::int64_t>> counters;
};

struct BinaryLoop {
    // How the clauses read the parameter, the variant function, and whether the variant is being lowered.
    LoopReads reads;
    llvm::Function *variant;
    bool in_variant;
    // The loop's byte scan clause, whose variant tail call skips the run of accepted bytes after it (OPT1.5).
    std::optional<ByteScan> scan = {};
    // Each rest of the parameter the clause being lowered made.
    std::map<const llvm::Value *, LoopRest> rests = {};
    // In the variant: the parameter's view, read once before the clauses, since the parameter is always its cell,
    // and the parameter as a rest at bit 0, which a read of the whole parameter copies.
    std::optional<BinaryView> view = {};
    std::optional<LoopRest> whole = {};
};

// A function whose float parameters loop through self tail calls (float loops, OPT1.2): its `variant` takes each such
// parameter as the bits of a double in its argument word, so a loop passes new floats on without making cells.
struct FloatLoop {
    // The float parameters, each head binding of one of them, the variant and whether the variant is being lowered.
    std::vector<std::size_t> parameters;
    std::map<semantic::BindingId, std::size_t> bindings;
    llvm::Function *variant;
    bool in_variant;
};

struct ExpressionLowering {
    // Borrow the current generic entry, immutable source/analysis and target-word builder.
    llvm::IRBuilder<> &builder;
    llvm::Function &entry;
    const semantic::Module &module;
    const semantic::Function &function;
    const semantic::types::Inference &inferred;
    llvm::IntegerType *word;
    // Keep source-node results for iterative, left-to-right argument evaluation.
    std::map<const ast::Expression *, llvm::Value *> values;
    // Borrow the function-wide read index; SSA values still belong to each isolated candidate.
    const BindingReads *reads = nullptr;
    // Select the source candidate without allowing bindings from another clause to enter its environment.
    std::size_t clause = 0;
    // Calls whose value is the function's result; they become tail transfers.
    const std::set<const ast::Expression *> *tail_calls = nullptr;
    // Share argument roots and bounded temporary slots across all candidates of the generated function.
    FunctionRoots *roots = nullptr;
    // Match definitions retain the original candidate SSA word; repeated names read this same identity.
    std::map<semantic::BindingId, llvm::Value *> bindings = {};
    // Share a terminal failure exit across calls instead of duplicating return blocks per expression.
    llvm::BasicBlock *failure = nullptr;
    // The innermost enclosing `catch` handler; failures and raises branch there instead of leaving the function.
    llvm::BasicBlock *handler = nullptr;
    // Semantic service errors reject the enclosing guard, while body errors raise badarg.
    llvm::BasicBlock *rejection = nullptr;
    llvm::BasicBlock *bad_argument = nullptr;
    // Arithmetic operand failures share a body error exit while guards retain their rejection edge.
    llvm::BasicBlock *bad_arithmetic = nullptr;
    // Integer results beyond the size limit share a body exit raising system_limit; guards reject instead.
    llvm::BasicBlock *system_limit = nullptr;
    // The anonymous fun lowered as its own function: its clauses, frame name and arity (arguments, then captured
    // values); null while lowering the function itself.
    const semantic::FunEntry *lambda = nullptr;
    // The module whose atom table and descriptor atom loads use, when module is another batch module whose
    // literal record defaults are lowered here; null means module itself.
    const semantic::Module *atom_owner = nullptr;
    // Capture each evaluated record field immediately, including repeated wildcard/default source nodes.
    std::map<const ast::Expression *, std::vector<llvm::Value *>> record_values = {};
    // Inferred facts and dominating shape tests replace checks (O2 with type specialization, step 59); null keeps
    // every check generic.
    const Proofs *proofs = nullptr;
    // The binary loop of the function being lowered (OPT1.4C); null for every other function.
    BinaryLoop *binary_loop = nullptr;
    // The float loop of the function being lowered (float loops, OPT1.2); null for every other function.
    FloatLoop *float_loop = nullptr;
    // The double each float word holds, where lowering has it in a register (raw floats, OPT1.2): float literals,
    // results of float arithmetic and cells of a float loop variant's parameters. Float operations read it instead.
    std::map<const llvm::Value *, llvm::Value *> raw_floats = {};
    // Inline funs (OPT1.8): the variables bound once to an anonymous fun, whose calls lower its clauses in place; null
    // when no fun is inlined (below O2). `inline_depth` counts the inlined fun bodies around the current expression.
    const LambdaBindings *lambdas = nullptr;
    std::size_t inline_depth = 0;
    // Sunk bound funs (OPT1.8): the variables whose binding built no closure; a read not inlined builds its own.
    std::map<semantic::BindingId, const semantic::FunEntry *> sunk_lambdas = {};
    // Fold loops (OPT1.7, OPT3.15): the accumulator updates of the fun inlined in a fold loop, owned when the frame
    // slot each names holds true (from the loop's second step on).
    std::map<const ast::Expression *, llvm::Value *> owned_maps = {};
    // Digit prefixes (OPT3.16): left operands of `++`, recorded by the walk, and the integer_to_list/1 calls among
    // them whose small-integer argument the append writes as digits onto its right operand.
    std::set<const ast::Expression *> digit_prefixes = {};
    std::set<const ast::Expression *> deferred_digits = {};
    // Map-shape inline caches (OPT1.6): words of atom literals read from the module's atoms; key searches cache their
    // index only for such keys and constant words, whose key stays the same at a site.
    std::set<const llvm::Value *> atom_literals = {};
    // Words read from the module's literal cache (literal heap): stable for the program's life, so never rooted, and
    // already boxed.
    std::set<const llvm::Value *> literal_values = {};
};

// The fact inference recorded for an expression, or the fact of argument `index` joined from the function's callers;
// none without proofs.
std::optional<Known> known_expression(const ExpressionLowering &state, const ast::Expression &expression);
std::optional<Known> known_argument(const ExpressionLowering &state, std::size_t index);
// The small integers an expression is proven to be one of.
std::optional<SmallRange> small_expression(const ExpressionLowering &state, const ast::ExprId &id);
// The address a list or boxed word points at, its primary tag removed: an instruction named proven.cell, or the
// constant address of a literal cell (OPT0.4). Used only right after a proof or a tag test, with no call in between,
// so the pointer never lives across a collection.
llvm::Value *cell_address(ExpressionLowering &state, llvm::Value *value, unsigned tag);
// An i1 that holds when a word is a cons cell (its primary tag); any word may be tested.
llvm::Value *inline_cons_test(ExpressionLowering &state, llvm::Value *value);
// Branch to `success` when any word is a tuple of `arity` elements, else to `mismatch`; the header is read only after
// the boxed tag test (OPT0.2A).
void inline_tuple_shape(ExpressionLowering &state, llvm::Value *value, std::size_t arity, llvm::BasicBlock *success,
                        llvm::BasicBlock *mismatch);
// Word `index` (head 0, tail 1) of a proven cons cell, and element `index` (zero-based) of a proven tuple.
llvm::Value *inline_cons_word(ExpressionLowering &state, llvm::Value *value, std::size_t index);
llvm::Value *inline_tuple_element(ExpressionLowering &state, llvm::Value *value, std::size_t index);
// A container inspection tested and read inline (inline inspections): shapes return the value itself, fields their
// word; anything else goes to `mismatch`, as CLAUSE_inspect_v1's bad argument did.
llvm::Value *inline_inspection(ExpressionLowering &state, abi::v1::ContainerInspection operation, llvm::Value *value,
                               std::size_t index, llvm::BasicBlock *mismatch);

// Branch to the shared failure exit before consuming a fallible service result.
void propagate_failure(ExpressionLowering &state);
// Load one runtime-initialized atom slot; no expression evaluation interns spelling.
llvm::Value *lower_atom(ExpressionLowering &state, const ast::Atom &atom);
// An i1 that holds when this context's runtime claimed the module's caches (OPT0.8, inline closures, literal heap).
llvm::Value *claimed(ExpressionLowering &state, llvm::GlobalVariable *atom_cache);
// The word of a module literal, `encoded` as one term of the module's literals (literal heap); null for a module
// without a descriptor, whose literals are built where they are used.
llvm::Value *lower_literal(ExpressionLowering &state, const std::string &encoded);
// A non-empty tuple, list, string or a map without a base whose every element is a literal, encoded; none for any
// other expression. Such an expression is one module literal: its elements are not lowered (literal heap).
std::optional<std::string> encoded_literal(ExpressionLowering &state, const ast::ExprId &id);
// The word of the module literal `id` is, or null when encoded_literal has none.
llvm::Value *lower_constant_term(ExpressionLowering &state, const ast::ExprId &id);
// The module literal of a tuple, or a list whose last value is its tail, of immediate words; null when a value is no
// small integer, [], {} or fixed atom (literal heap).
llvm::Value *immediate_container(ExpressionLowering &state, bool tuple, std::span<llvm::Value *const> values);
// The big-endian two's complement bytes of an integer literal, at least two (literal heap).
std::vector<std::uint8_t> literal_integer_bytes(const llvm::APInt &value);
// The bytes of a binary whose every segment is known at compile time; none otherwise (literal binaries).
std::optional<std::vector<std::uint8_t>> constant_binary(ExpressionLowering &state, const ast::Bitstring &binary);

// Representation-aware exact comparison shares the runtime extension point with guards.
llvm::Value *lower_exact(ExpressionLowering &state, llvm::Value *left, llvm::Value *right);
// A caller supplies selection continuations; mismatch itself never mutates the error channel.
void lower_head(ExpressionLowering &state, llvm::BasicBlock *success, llvm::BasicBlock *mismatch);
// Preserve compact projection/direct-call IR when the normalized plan has no rejection tests.
bool lower_unconditional_head(ExpressionLowering &state);
// Lower the reusable matcher against caller-supplied values and selection continuations; the result holds every
// candidate value, defined where the success continuation can read it. `known` holds proven facts of the inputs.
std::vector<llvm::Value *> lower_match_plan(ExpressionLowering &state, const semantic::MatchPlan &plan,
                                            std::span<llvm::Value *const> values, llvm::BasicBlock *success,
                                            llvm::BasicBlock *mismatch,
                                            std::span<const std::optional<Known>> known = {});
// Plan a one-input body or case pattern; semantic analysis accepted it, so failure is a phase-contract error.
semantic::MatchPlan body_pattern_plan(const ExpressionLowering &state, const ast::ExprId &pattern,
                                      semantic::GeneratorPattern generator = semantic::GeneratorPattern::none);
// Match an already evaluated RHS, publishing new bindings only along the successful continuation.
llvm::Value *lower_body_match(ExpressionLowering &state, const ast::MatchExpression &match);
// Raise clause exhaustion using the existing checked generated-call contract.
void raise_function_clause(ExpressionLowering &state);
// Raise a typed Erlang error; payload ownership remains with the existing checked service.
void raise_reason(ExpressionLowering &state, abi::v1::ErrorReason reason, llvm::Value *payload = nullptr);
// Evaluate only authorized immediate service operations with success-only outputs: inline where an inline form exists
// (OPT0.2B), else through CLAUSE_immediate_v1.
llvm::Value *lower_immediate(ExpressionLowering &state, abi::v1::ImmediateOperation operation, llvm::Value *left,
                             llvm::Value *right = nullptr);
// The CLAUSE_immediate_v1 call alone: the slow tail of the inline forms.
llvm::Value *lower_immediate_service(ExpressionLowering &state, abi::v1::ImmediateOperation operation,
                                     llvm::Value *left, llvm::Value *right);

// The heap block's free words for an inline allocation (OPT0.3): its start, its end after `words` words, and whether
// they fit below FrameState::heap_limit (never while the fields are null).
struct HeapRoom {
    llvm::Value *top;
    llvm::Value *end;
    llvm::Value *fits;
};

HeapRoom heap_room(ExpressionLowering &state, llvm::Value *words);
// Take the words of an inline allocation: FrameState::heap_top becomes its end.
void heap_commit(ExpressionLowering &state, const HeapRoom &room);
// integer_to_list/1 of a small integer and list_to_integer/1 of a short decimal list made inline, else `call` (the
// builtin); null for other builtins (inline integer text, OPT3.8). An integer_to_list/1 call that is the left operand
// of `++` (digit_prefixes) yields a small integer itself, which lower_digit_prefix writes onto the right operand.
llvm::Value *inline_integer_text(ExpressionLowering &state, const ast::Expression &expression, std::size_t builtin,
                                 std::span<llvm::Value *const> values, llvm::function_ref<llvm::Value *()> call);
// `integer_to_list(N) ++ Right` with the call deferred: N's digits written onto Right with no copy; null when `left`
// was not deferred (digit prefixes, OPT3.16).
llvm::Value *lower_digit_prefix(ExpressionLowering &state, const ast::Expression &left, llvm::Value *prefix,
                                llvm::Value *right);
// make_tuple/2 of 1..64 elements bump-allocated and filled inline where the heap has room, else the builtin.
llvm::Value *lower_make_tuple(ExpressionLowering &state, std::size_t builtin, std::span<llvm::Value *const> values);
// setelement/3 copying a small tuple inline where the heap has room, else the builtin (OPT0.2C).
llvm::Value *lower_setelement(ExpressionLowering &state, std::size_t builtin, std::span<llvm::Value *const> values);

BinaryView inline_binary_view(ExpressionLowering &state, llvm::Value *binary);
// The view of a word known to be a sub-binary cell (binary loops); `invariant` when the cell and its original
// exist before the function runs and only the cell's offset and length change (SSA binary cursors, OPT1.4F).
BinaryView sub_view(ExpressionLowering &state, llvm::Value *binary, bool invariant);
// An i1 that holds when a word is a bitstring (an inline or off-heap binary cell) (OPT1.4).
llvm::Value *inline_bitstring_test(ExpressionLowering &state, llvm::Value *value);
// A value and the predecessor block that computed it.
using BlockValue = std::pair<llvm::BasicBlock *, llvm::Value *>;
// At the start of the current block, join the values its two predecessors computed (LLVM's SSA updater, which never
// reads PHI operands; OPT0.2B).
llvm::Value *join_values(ExpressionLowering &state, llvm::Type *type, BlockValue first, BlockValue second);
// A stand-in for a loop-carried word at a loop head, used by the body before the latch value exists (inline appends).
llvm::Instruction *loop_placeholder(ExpressionLowering &state, const char *name);
// Replace a loop head's placeholder by the join of its value on entry and its value from the latch; the join.
llvm::Value *close_loop(llvm::Instruction *value, llvm::BasicBlock *head, BlockValue entry, BlockValue latch);
// The inline form of an operation (lowering_inline, OPT0.2B), or null when it has none.
llvm::Value *lower_inline_immediate(ExpressionLowering &state, abi::v1::ImmediateOperation operation, llvm::Value *left,
                                    llvm::Value *right);
// Print one term through erlang:display/1; the rooted result is the atom true after the channel check.
llvm::Value *lower_display(ExpressionLowering &state, llvm::Value *value);
// Stop the program through erlang:halt/0,1 (null `status` means halt/0); the dead continuation yields [].
llvm::Value *lower_halt(ExpressionLowering &state, llvm::Value *status);
// Raise `reason` with the class of erlang:error/exit/throw (`name`); the dead continuation yields [].
llvm::Value *lower_raise(ExpressionLowering &state, std::u32string_view name, llvm::Value *reason);
// Turn the pending exception into the `catch Expr` value; halts and runtime failures continue to the outer exit.
llvm::Value *lower_catch(ExpressionLowering &state);
// A caught exception's rooted class atom, reason and stack trace.
using Exception = std::array<llvm::Value *, 3>;
// Take the pending exception for try handlers; halts and runtime failures continue to the outer exit.
Exception lower_exception(ExpressionLowering &state);
// Raise a caught exception again, with its stack trace, when no catch clause matched.
void reraise(ExpressionLowering &state, const Exception &exception);
// Raise erlang:error/2,3 with the arguments shown in the top stack frame; the dead continuation yields [].
llvm::Value *lower_error(ExpressionLowering &state, llvm::Value *reason, llvm::Value *arguments);
// Raise erlang:raise/3; an invalid class or stack makes the call evaluate to badarg instead.
llvm::Value *lower_raise_stack(ExpressionLowering &state, std::span<llvm::Value *const> arguments);
// Ordinary service errors reject guards or raise badarg; boolean operand errors additionally retain their value.
llvm::BasicBlock *bad_argument_exit(ExpressionLowering &state, llvm::Value *payload = nullptr);
// Arithmetic errors reject guards and raise badarith in ordinary bodies.
llvm::BasicBlock *bad_arithmetic_exit(ExpressionLowering &state);
// Materialize arbitrary decimal literals with target-specific small encodings or rooted runtime storage.
// Materialize finite IEEE bits through a rooted checked runtime service.
llvm::Value *lower_float(ExpressionLowering &state, double value);
llvm::Value *lower_integer(ExpressionLowering &state, std::string_view decimal);

struct OperandProofs {
    // The small integers each operand is proven to be one of; none keeps its tag check.
    std::optional<SmallRange> left;
    std::optional<SmallRange> right;
};

// + - * / with a float operand (any numbers for /) computed inline into a new float cell on 64-bit targets, with
// `service` for every other case (inline float arithmetic).
llvm::Value *inline_float_operation(ExpressionLowering &state, abi::v1::ImmediateOperation operation, llvm::Value *left,
                                    llvm::Value *right, llvm::function_ref<llvm::Value *()> service);
// A math builtin of numbers computed inline into a float cell on 64-bit targets, with `service` (the builtin) for
// other operands and non-finite results; null for builtins without an inline form (inline math).
llvm::Value *inline_math(ExpressionLowering &state, std::size_t builtin, std::span<llvm::Value *const> values,
                         llvm::function_ref<llvm::Value *()> service);
// A new float cell holding `raw`, whose double stays known as raw (raw floats, OPT1.2). Lowering drops the cell where
// nothing reads the word itself (drop_unused_floats).
llvm::Value *float_box(ExpressionLowering &state, llvm::Value *raw);
// The word a service reads: a raw float gets a cell of its own where the builder is, so the cell float_box made for it
// stays unread on the fast path (raw floats, OPT1.2).
llvm::Value *materialized(ExpressionLowering &state, llvm::Value *word);
// Remove the float cells of float_box whose word nothing reads, once a function is lowered.
void drop_unused_floats(llvm::Function &function);
// A comparison of two words whose doubles lowering holds, as the true or false word (raw floats, OPT1.2); null when
// either has none.
llvm::Value *raw_comparison(ExpressionLowering &state, abi::v1::ImmediateOperation operation, llvm::Value *left,
                            llvm::Value *right);
// Define FLOAT_BOX in the module being lowered once: the slow paths of transfers into a float loop variant make
// cells of their doubles with it (float loops, OPT1.2).
void define_float_box(ExpressionLowering &state);
// Use checked small arithmetic where safe, retaining the common runtime fallback for all other values; proven
// operands skip their tag checks, and a proven result range also skips the overflow check and the fallback.
llvm::Value *lower_operation(ExpressionLowering &state, abi::v1::ImmediateOperation operation, llvm::Value *left,
                             llvm::Value *right = nullptr, OperandProofs proofs = {});

struct ServiceOutput {
    // Pair the status byte with its success-only rooted word slot, without interchangeable positional pointers.
    llvm::Value *outcome;
    llvm::Value *slot;
};

// Consume a checked success-only output after separating infrastructure failure from semantic rejection.
llvm::Value *checked_value(ExpressionLowering &state, ServiceOutput result, llvm::BasicBlock *rejection);
// Consume an arithmetic service output: bad operands raise badarith and an oversized integer system_limit in bodies;
// both reject guards.
llvm::Value *checked_arithmetic(ExpressionLowering &state, ServiceOutput result);

struct BitLowering {
    // Success-only words retain both extracted ownership and the following logical bit cursor.
    llvm::Value *value;
    llvm::Value *cursor;
};

// Construct complete bitstrings and checked byte parts through shared rooted services.
llvm::Value *lower_bits(ExpressionLowering &state, const ast::Bitstring &binary);
// A binary of whole-byte constant-width integer segments written inline, with `service` building it when a value
// is no small integer or the heap has no room; null when the binary has other segments (inline construction).
llvm::Value *inline_binary(ExpressionLowering &state, const ast::Bitstring &binary,
                           llvm::function_ref<llvm::Value *()> service);
llvm::Value *lower_binary_part(ExpressionLowering &state, std::span<llvm::Value *const> values);
// Whether a node extracts the rest of a binary (`Rest/binary` or `Rest/bits`) (OPT1.4B).
bool rest_extraction(const semantic::MatchNode &node);
// A binary loop variant's parameter `cell` with `view` as a rest at bit 0, which reads of the whole parameter copy.
LoopRest whole_rest(ExpressionLowering &state, llvm::Value *cell, const BinaryView &view);
// A copy of a binary loop's rest `value` for a read that may keep it, when the loop holds the cell (escaping rests,
// OPT1.4E); `value` itself otherwise.
llvm::Value *loop_copy(ExpressionLowering &state, const LoopRest &rest, llvm::Value *value);
// The value of a read of `identity` in a binary loop: a copy where the loop's cell may be kept (escaping rests,
// OPT1.4E); `read` is the reading expression, or null for a fun capturing the value.
llvm::Value *binary_loop_read(ExpressionLowering &state, semantic::BindingId identity, llvm::Value *value,
                              const ast::Expression *read);
// Reject malformed/truncated segments without publishing tentative bindings or advancing their cursor.
BitLowering lower_bit_pattern(ExpressionLowering &state, const semantic::MatchNode &node,
                              std::span<llvm::Value *> values, llvm::BasicBlock *mismatch);
// Stage source-ordered map construction/update through rooted checked services.
// Construct or update a map; an update whose i1 `owned` holds may change its source in place (OPT0.5B, OPT3.15).
llvm::Value *lower_map(ExpressionLowering &state, const ast::MapExpression &map, llvm::Value *owned);
// Run one map service operation; badmap/badkey raise.
llvm::Value *lower_map_operation(ExpressionLowering &state, abi::v1::MapOperation operation,
                                 std::span<llvm::Value *const> values);
// Run one bitstring service operation (construction or concat); a bad argument raises badarg.
llvm::Value *lower_bits_operation(ExpressionLowering &state, abi::v1::BitOperation operation,
                                  std::span<llvm::Value *const> values);
// Resolve map BIFs separately from the two-operand numeric service; return null for other operations.
llvm::Value *lower_map_query(ExpressionLowering &state, abi::v1::ImmediateOperation operation, llvm::Value *left,
                             llvm::Value *right);

struct MapKey {
    // A map operand and the key looked up in it (small-map lookups).
    llvm::Value *map;
    llvm::Value *key;
};

// Small-map lookups: the map's value for an atom or small-integer key read from a classic map cell inline; a missing
// key branches to `miss` when given, and every other case runs `service`.
llvm::Value *inline_map_get(ExpressionLowering &state, MapKey operands, llvm::BasicBlock *miss,
                            llvm::function_ref<llvm::Value *()> service);
// is_map_key/2 answered from a classic map cell inline, `service` for every other case (small-map lookups).
llvm::Value *inline_map_contains(ExpressionLowering &state, MapKey operands,
                                 llvm::function_ref<llvm::Value *()> service);
// map_size/1 of a classic map cell inline, `service` for every other case (small-map lookups).
llvm::Value *inline_map_size(ExpressionLowering &state, llvm::Value *map, llvm::function_ref<llvm::Value *()> service);
// Continue where `map` is any map, else branch to `mismatch`; false (nothing emitted) for an immediate constant.
bool inline_map_shape(ExpressionLowering &state, llvm::Value *map, llvm::BasicBlock *mismatch);
// An update's service values (the map, then key, value and exact flag per field) applied inline to a classic map
// cell that holds every key: the cell copied with the new values; `service` for every other case (small-map lookups).
llvm::Value *inline_map_update(ExpressionLowering &state, std::span<llvm::Value *const> values,
                               llvm::function_ref<llvm::Value *()> service);
// Evaluate scoped keys with semantic errors routed to the enclosing pattern mismatch.
llvm::Value *lower_map_pattern(ExpressionLowering &state, const semantic::MatchNode &node, llvm::Value *input,
                               llvm::BasicBlock *mismatch);
// Construct tuple/list/string expression values after their source-ordered children have completed.
llvm::Value *lower_container(ExpressionLowering &state, const ast::ExprValue &value);
// Share rooted tuple construction with ordinary record expansion.
llvm::Value *lower_tuple(ExpressionLowering &state, std::span<llvm::Value *const> values);
// Construct a list of `values` whose last value is the tail.
llvm::Value *lower_list(ExpressionLowering &state, std::span<llvm::Value *const> values);
// Reverse a proper list built by a comprehension.
llvm::Value *lower_reverse(ExpressionLowering &state, llvm::Value *list);
// `Left ++ Right`: a short proper Left copied inline, anything else through the builtin (inline appends).
llvm::Value *lower_append(ExpressionLowering &state, llvm::Value *left, llvm::Value *right);

struct Comprehension {
    // The list, binary or map comprehension being lowered.
    const ast::ExprValue *syntax;
    // The bindings before the comprehension come back after it: nothing it binds is visible outside.
    std::map<semantic::BindingId, llvm::Value *> bindings;
    // The reversed result lives in a term slot, so the loops carry no SSA value across iterations or calls.
    llvm::Value *accumulator;
    // Where the next qualifier continues once an element is done: the innermost generator's next element, or the
    // end when no generator encloses it.
    llvm::BasicBlock *next;
    llvm::BasicBlock *end;
};

// Start a comprehension with an empty accumulator.
Comprehension begin_comprehension(ExpressionLowering &state, const ast::ExprValue &syntax);
// Loop over the generators of one qualifier (a zip group runs its generators in step), whose inputs are lowered;
// the following qualifiers lower into the loop body.
void lower_generators(ExpressionLowering &state, Comprehension &comprehension,
                      const ast::ComprehensionQualifier &qualifier);
// Continue when a filter holds; otherwise take the next element. A filter that is not a guard test and returns
// neither true nor false raises {bad_filter, Value}.
void lower_filter(ExpressionLowering &state, const Comprehension &comprehension, const ast::ExprId &filter);
// Push the lowered template values onto the accumulator, then take the next element; a binary comprehension's
// template must be a bitstring (else badarg).
void lower_templates(ExpressionLowering &state, const Comprehension &comprehension);
// Once every generator is exhausted, restore the bindings and return the list, bitstring or map (later keys win).
llvm::Value *finish_comprehension(ExpressionLowering &state, Comprehension &comprehension);
// Lower record values/updates/access/indices using tuple shape and checked element services.
llvm::Value *lower_record(ExpressionLowering &state, const ast::ExprId &id);
// Construct, update or read a field of a local native record through the record service.
llvm::Value *lower_native_record(ExpressionLowering &state, const ast::Expression &expression,
                                 const semantic::RecordLayout &layout);
// Construct, update or read a field of a qualified or imported native record.
llvm::Value *lower_external_record(ExpressionLowering &state, const ast::Expression &expression,
                                   const semantic::RecordName &name);
// Update or read a field of a native record through the anonymous #_ form.
llvm::Value *lower_anonymous_record(ExpressionLowering &state, const ast::Expression &expression);
// Test a native record pattern's identity or extract one of its fields; failures branch to mismatch.
llvm::Value *lower_record_pattern(ExpressionLowering &state, const semantic::MatchNode &node, llvm::Value *input,
                                  llvm::BasicBlock *mismatch);
// An i1 that is true when value is a native record passing check against the module and name atoms.
llvm::Value *lower_native_test(ExpressionLowering &state, abi::v1::RecordCheck check, llvm::Value *value,
                               llvm::Value *module, llvm::Value *name);
// Expand a validated record_info/2 call to its constant field-name list or tuple size.
llvm::Value *lower_record_info(ExpressionLowering &state, const ast::Expression &expression);
// Compose the tuple-record BIF with context-appropriate argument rejection and literal declaration sizes.
llvm::Value *lower_record_test(ExpressionLowering &state, const ast::Expression &expression,
                               const ast::CallExpression &call);
// Validate both arbitrary-integer bounds first, then test the candidate on a separate successful edge.
llvm::Value *lower_integer_range(ExpressionLowering &state, const ast::CallExpression &call);
// Check candidate ownership/shape before extracting a rooted child; mismatch belongs to the pattern caller.
llvm::Value *lower_inspection(ExpressionLowering &state, abi::v1::ContainerInspection operation, llvm::Value *value,
                              std::size_t index, llvm::BasicBlock *mismatch);
// Evaluate the existing bounded body walk using the candidate's tentative bindings.
llvm::Value *lower_body(ExpressionLowering &state, const ast::ExprId &root);
// Emit an already visited ordinary value node; lazy operands are scheduled by the iterative walker.
llvm::Value *lower_value(ExpressionLowering &state, const ast::ExprId &id);

struct GuardEdges {
    // Named candidate continuations prevent accidental interchange of acceptance and rejection blocks.
    llvm::BasicBlock *success;
    llvm::BasicBlock *rejection;
};

// Preserve comma conjunctions and semicolon alternatives using canonical-true boundaries.
void lower_guard(ExpressionLowering &state, const ast::GuardSyntax &guard, GuardEdges edges);

// Build the value of fun F/A, fun M:F/A or an anonymous fun (capturing its values) through the fun service.
llvm::Value *lower_fun(ExpressionLowering &state, const ast::Expression &expression);
// Call the value of the call's target with its arguments (F(Args)); badfun, badarity and undef raise.
llvm::Value *lower_fun_call(ExpressionLowering &state, const ast::Expression &expression,
                            const ast::CallExpression &call);
// Call M:F(Args) with a runtime module or function; badarg and undef raise.
llvm::Value *lower_dynamic_call(ExpressionLowering &state, const ast::Expression &expression,
                                const ast::CallExpression &call);
// Call apply(Fun, Args) or apply(M, F, Args) with the list unpacked into the registers at run time.
llvm::Value *lower_apply(ExpressionLowering &state, const ast::Expression &expression, const ast::CallExpression &call);
// One receive step over the mailbox (abi/messages.hpp); peek writes the message to `slot` and yields nonzero when
// there was one.
llvm::Value *lower_receive(ExpressionLowering &state, abi::v1::ReceiveOperation operation, llvm::Value *slot);
// Enter the wait builtin with `timeout`: the process waits until a message arrives; yields the builtin's result.
llvm::Value *lower_wait(ExpressionLowering &state, llvm::Value *timeout);
// Enter bridge builtin `builtin` (abi::v1::bridge_builtins index) with `values` in the registers, like a call.
llvm::Value *lower_builtin(ExpressionLowering &state, std::size_t builtin, std::span<llvm::Value *const> values);
// Emit one resolved call after its arguments have been evaluated in source order.
llvm::Value *lower_call(ExpressionLowering &state, const ast::Expression &expression, const ast::CallExpression &call);
// Call the exported function `callee` with `values` as an ordinary (non-tail) call (list loops, OPT1.7).
llvm::Value *lower_named_call(ExpressionLowering &state, semantic::FunctionRef callee,
                              std::span<llvm::Value *const> values);
// Call the fun value `fun` with `arguments` as an ordinary (non-tail) call; badfun and badarity raise (inline funs).
llvm::Value *lower_apply_values(ExpressionLowering &state, llvm::Value *fun, std::span<llvm::Value *const> arguments);
// The head of every comprehension iteration: collect when the heap wants it, then loop_reduction.
void loop_safepoint(ExpressionLowering &state);
// Spend a reduction at a loop head and yield when none are left; the yield is a safepoint, where a list loop
// collects (list loops, OPT1.7).
void loop_reduction(ExpressionLowering &state);

// Inline funs (OPT1.8): an anonymous fun whose clauses lowering can put in place of a call, and the argument the walk
// does not lower (the fun written in the call, or a read of a sunk binding), so that the fun is built only on the
// paths that need its value.
struct KnownLambda {
    const semantic::FunEntry *lambda = nullptr;
    const ast::Expression *unlowered = nullptr;
};

// The anonymous fun of arity `arity` that `value` is (written there, or read from a variable bound once to it) when
// funs may be inlined here; none otherwise.
KnownLambda known_lambda(const ExpressionLowering &state, const ast::ExprId &value, std::size_t arity);
// The fun written in a call that lowering inlines (or read from a sunk binding), and the fun of a sunk binding's
// match, which the walk must not lower; null for every other expression.
const ast::Expression *sunk_fun(const ExpressionLowering &state, const ast::Expression &expression);
// A sunk binding's match `F = fun ... end`: F bound to a placeholder, no closure built (sunk bound funs, OPT1.8);
// null for every other match.
llvm::Value *lower_sunk_binding(ExpressionLowering &state, const ast::Expression &expression);
// The closure of a sunk binding a read needs, built where it is read; null for every other read.
llvm::Value *sunk_read(ExpressionLowering &state, semantic::BindingId identity);
// The fun's clauses matched against `arguments` and their body lowered in place: its result. No clause matching
// calls the fun from `fun()`, which raises function_clause as the fun itself would (inline funs, OPT1.8).
llvm::Value *inline_lambda(ExpressionLowering &state, const semantic::FunEntry &lambda,
                           std::span<llvm::Value *const> arguments, llvm::function_ref<llvm::Value *()> fun);
// F(Args) of a known anonymous fun with its clauses inlined; null when the target is no known fun (inline funs).
llvm::Value *lower_known_fun_call(ExpressionLowering &state, const ast::CallExpression &call);
// The unlowered fun argument (written there or a sunk read) of a lists call that lowering turns into a loop, null for
// other calls (list loops, OPT1.7).
const ast::Expression *sunk_list_fun(const ExpressionLowering &state, const ast::Expression &expression,
                                     const ast::CallExpression &call);
// lists:map/2, filter/2, foreach/2, all/2, any/2, flatmap/2, foldl/3, foldr/3, mapfoldl/3 and mapfoldr/3 of a known
// anonymous fun as a loop with the fun inlined; null for every other call (list loops, OPT1.7).
llvm::Value *lower_list_call(ExpressionLowering &state, const ast::Expression &expression,
                             const ast::CallExpression &call);
// The unlowered fun argument of a maps:fold/3 call that lowering turns into a loop, null for other calls (map fold
// loops, OPT1.7).
const ast::Expression *sunk_map_fun(const ExpressionLowering &state, const ast::Expression &expression,
                                    const ast::CallExpression &call);
// maps:fold/3 of a known anonymous fun as a loop over the map's entries with the fun inlined; null for every other
// call (map fold loops, OPT1.7).
llvm::Value *lower_map_fold(ExpressionLowering &state, const ast::Expression &expression,
                            const ast::CallExpression &call);
// A frame slot holding false: whether a fold loop's accumulator is a map only the loop holds (OPT3.15).
llvm::Value *fold_owned_slot(ExpressionLowering &state);
// One step of a fold loop (list loops, map fold loops): the fun inlined on `arguments`, the last its accumulator. When
// every clause returns its accumulator parameter updated, `Acc#{...}` with Acc read nowhere else, those updates are
// owned once the `owned` slot holds true, which the step stores after them (OPT3.15).
llvm::Value *apply_fold(ExpressionLowering &state, const semantic::FunEntry &lambda,
                        std::span<llvm::Value *const> arguments, llvm::Value *owned,
                        llvm::function_ref<llvm::Value *()> fun);
// Whether a map update may change its source in place (OPT0.5B, OPT3.15): true, an i1 decided at run time in a fold
// loop, or null for a copying update.
llvm::Value *owned_map_update(ExpressionLowering &state, const ast::Expression &update);
} // namespace clause::codegen
