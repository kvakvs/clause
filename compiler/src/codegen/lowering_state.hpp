#pragma once
#include "../semantic/match_plan.hpp"
#include "../semantic/records.hpp"
#include "lowering_expressions.hpp"
#include "lowering_roots.hpp"
#include "proofs.hpp"
#include <array>
#include <clause/abi/bits.hpp>
#include <clause/abi/calls.hpp>
#include <clause/abi/containers.hpp>
#include <clause/abi/maps.hpp>
#include <clause/abi/messages.hpp>
#include <clause/abi/records.hpp>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string_view>
#include <utility>

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

// Branch to the shared failure exit before consuming a fallible service result.
void propagate_failure(ExpressionLowering &state);
// Load one runtime-initialized atom slot; no expression evaluation interns spelling.
llvm::Value *lower_atom(ExpressionLowering &state, const ast::Atom &atom);

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
// setelement/3 copying a small tuple inline where the heap has room, else the builtin (OPT0.2C).
llvm::Value *lower_setelement(ExpressionLowering &state, std::size_t builtin, std::span<llvm::Value *const> values);

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

BinaryView inline_binary_view(ExpressionLowering &state, llvm::Value *binary);
// An i1 that holds when a word is a bitstring (an inline or off-heap binary cell) (OPT1.4).
llvm::Value *inline_bitstring_test(ExpressionLowering &state, llvm::Value *value);
// A value and the predecessor block that computed it.
using BlockValue = std::pair<llvm::BasicBlock *, llvm::Value *>;
// At the start of the current block, join the values its two predecessors computed (LLVM's SSA updater, which never
// reads PHI operands; OPT0.2B).
llvm::Value *join_values(ExpressionLowering &state, llvm::Type *type, BlockValue first, BlockValue second);
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
// Reject malformed/truncated segments without publishing tentative bindings or advancing their cursor.
BitLowering lower_bit_pattern(ExpressionLowering &state, const semantic::MatchNode &node,
                              std::span<llvm::Value *> values, llvm::BasicBlock *mismatch);
// Stage source-ordered map construction/update through rooted checked services.
// Construct or update a map; an owned update may change its source in place (OPT0.5B).
llvm::Value *lower_map(ExpressionLowering &state, const ast::MapExpression &map, bool owned);
// Run one map service operation; badmap/badkey raise.
llvm::Value *lower_map_operation(ExpressionLowering &state, abi::v1::MapOperation operation,
                                 std::span<llvm::Value *const> values);
// Run one bitstring service operation (construction or concat); a bad argument raises badarg.
llvm::Value *lower_bits_operation(ExpressionLowering &state, abi::v1::BitOperation operation,
                                  std::span<llvm::Value *const> values);
// Resolve map BIFs separately from the two-operand numeric service; return null for other operations.
llvm::Value *lower_map_query(ExpressionLowering &state, abi::v1::ImmediateOperation operation, llvm::Value *left,
                             llvm::Value *right);
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
} // namespace clause::codegen
