#include "frames.hpp"
#include "code_convention.hpp"
#include "llvm_state.hpp"
#include "lowering_roots.hpp"
#include "runtime_symbols.hpp"
#include "verification.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <clause/abi/frames.hpp>
#include <iterator>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/InstIterator.h>
#include <llvm/Transforms/Utils/Local.h>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>

namespace clause::codegen {
namespace {
using abi::v1::frame_header_words;
using abi::v1::frame_resume_word;
using abi::v1::register_arguments;
using abi::v1::StateField;

// The X registers a transfer passes as parameters after the context (OPT0.1A).
using Parameters = std::array<llvm::Value *, register_arguments>;

// FrameDescriptor fields an inline entry reads (abi/modules.hpp).
constexpr unsigned DESCRIPTOR_ARITY = 3;
constexpr unsigned DESCRIPTOR_BODY = 4;
constexpr unsigned DESCRIPTOR_SLOTS = 5;

// Arity recorded on a native-form Erlang function; empty for every other function.
std::optional<std::size_t> erlang_arity(const llvm::Function *function) {
    if (!function || !function->hasFnAttribute(ARITY_ATTRIBUTE)) {
        return {};
    }
    const auto text = function->getFnAttribute(ARITY_ATTRIBUTE).getValueAsString();
    std::size_t value = 0;
    std::from_chars(text.begin(), text.end(), value);
    return value;
}

// Declare one frame service of abi/frames.hpp with its target spelling.
template <typename Service>
llvm::FunctionCallee declare(llvm::Module &output, llvm::Type *result, llvm::ArrayRef<llvm::Type *> parameters) {
    return output.getOrInsertFunction(services::symbol<Service>(output.getTargetTriple()),
                                      llvm::FunctionType::get(result, parameters, false));
}

struct Services {
    // Target word, the FrameDescriptor layout and the frame services of one output module; the transfer services
    // are the slow paths of the inline transfers (OPT0.1).
    llvm::Module &output;
    llvm::IntegerType *word;
    llvm::StructType *descriptor;
    llvm::FunctionType *code;
    // The calling convention of Code bodies and transfers (preserve_none where the runtime uses it).
    llvm::CallingConv::ID convention;
    llvm::FunctionCallee enter;
    llvm::FunctionCallee tail;
    llvm::FunctionCallee leave;
    llvm::FunctionCallee invoke;

    // Word alignment of slots and registers.
    [[nodiscard]] llvm::Align align() const { return llvm::Align(word->getBitWidth() / 8); }

    // Load one abi::v1::FrameState field of `context` as `type` (a word or a pointer); the state starts every
    // context (OPT0.1).
    llvm::Value *state(llvm::IRBuilder<> &builder, llvm::Value *context, StateField field, llvm::Type *type,
                       const char *name) const {
        return builder.CreateAlignedLoad(type, state_address(builder, context, field), align(), name);
    }

    // Store a word into one FrameState field of `context`.
    void set_state(llvm::IRBuilder<> &builder, llvm::Value *context, StateField field, llvm::Value *value) const {
        builder.CreateAlignedStore(value, state_address(builder, context, field), align());
    }

    // Address of one FrameState field; every field is one target word.
    llvm::Value *state_address(llvm::IRBuilder<> &builder, llvm::Value *context, StateField field) const {
        return builder.CreateConstInBoundsGEP1_64(word, context, static_cast<std::uint64_t>(field));
    }

    // Load field `index` of a FrameDescriptor as `type`.
    llvm::Value *descriptor_field(llvm::IRBuilder<> &builder, llvm::Value *frame, unsigned index, llvm::Type *type,
                                  const char *name) const {
        return builder.CreateAlignedLoad(type, builder.CreateStructGEP(descriptor, frame, index), align(), name);
    }

    // The descriptor a call to `function` enters; remote callees are external declarations.
    [[nodiscard]] llvm::Constant *descriptor_of(const llvm::Function &function) const {
        return output.getOrInsertGlobal((function.getName() + ".frame").str(), descriptor);
    }
};

} // namespace

llvm::CallingConv::ID code_convention(const llvm::Triple &target) {
#if defined(__clang__)
    if (target.getArch() == llvm::Triple::x86_64 || target.getArch() == llvm::Triple::aarch64) {
        return llvm::CallingConv::PreserveNone;
    }
#endif
    static_cast<void>(target);
    return llvm::CallingConv::C;
}

namespace {
// Declare the services and descriptor layout once per output module.
Services frame_services(llvm::Module &output, llvm::IntegerType *word) {
    auto *ptr = llvm::PointerType::get(output.getContext(), 0);
    auto *descriptor = llvm::StructType::get(output.getContext(), {ptr, word, word, word, ptr, word, word});
    // Code takes the context and the leading X registers (OPT0.1A).
    std::vector<llvm::Type *> code{ptr};
    code.insert(code.end(), register_arguments, word);
    return {output,
            word,
            descriptor,
            llvm::FunctionType::get(llvm::Type::getVoidTy(output.getContext()), code, false),
            code_convention(llvm::Triple(output.getTargetTriple())),
            declare<services::Enter>(output, ptr, {ptr, ptr}),
            declare<services::Tail>(output, ptr, {ptr, ptr}),
            declare<services::Return>(output, ptr, {ptr, word}),
            declare<services::Invoke>(output, word, {ptr, ptr, ptr})};
}

struct Prologue {
    // Computed whenever the body is entered, so these dominate every resume point and never spill.
    llvm::BasicBlock *block;
    llvm::Value *registers;
    llvm::Value *slots;
    llvm::Value *resume;
};

struct ErlangCall {
    // A call of a native-form Erlang function, or of a fun through the apply marker, the FrameDescriptor it enters
    // and the arguments to copy into the registers (a fun call's arguments are in the registers already).
    llvm::CallInst *call;
    llvm::Value *descriptor;
    std::size_t arity;
    // The stores of a direct call's leading argument words into its array: their values become the transfer's
    // parameters instead (OPT0.1A). Null for a fun call or past the arity.
    std::array<llvm::StoreInst *, register_arguments> stores{};
};

using ArgumentStores = std::array<llvm::StoreInst *, register_arguments>;

// Record the store into one argument array element when the element is a leading word (OPT0.1A).
void record_store(llvm::GetElementPtrInst &address, ArgumentStores &stores) {
    const auto *index = llvm::dyn_cast<llvm::ConstantInt>(address.getOperand(1));
    if (!index || index->getZExtValue() >= register_arguments) {
        return;
    }
    for (auto *user : address.users()) {
        if (auto *store = llvm::dyn_cast<llvm::StoreInst>(user)) {
            stores.at(index->getZExtValue()) = store;
        }
    }
}

// The stores of the leading words of a native-form argument array, indexed by word (OPT0.1A).
ArgumentStores argument_stores(llvm::Value *array) {
    ArgumentStores stores{};
    if (!llvm::isa<llvm::AllocaInst>(array)) {
        return stores;
    }
    for (auto *user : array->users()) {
        if (auto *address = llvm::dyn_cast<llvm::GetElementPtrInst>(user)) {
            record_store(*address, stores);
        }
    }
    return stores;
}

struct Continuation {
    // A non-tail Erlang call and the block resuming after it, reached through the body's resume switch.
    ErlangCall call;
    llvm::BasicBlock *resume;
};

struct Callee {
    // The FrameDescriptor fields an inline entry needs: arity (words copied from the registers), body and slots.
    llvm::Value *arity;
    llvm::Value *body;
    llvm::Value *slots;
};

// Whether a use reads its value in a `reached` block other than the definition's. A PHI reads at the end of
// an incoming block; any predecessor of its block counts, which can only spill more than needed.
bool reads_in(const llvm::Use &use, const llvm::BasicBlock *definition,
              const std::set<const llvm::BasicBlock *> &reached) {
    const auto *user = llvm::cast<llvm::Instruction>(use.getUser());
    const auto *block = user->getParent();
    if (!llvm::isa<llvm::PHINode>(user)) {
        return block != definition && reached.contains(block);
    }
    return std::ranges::any_of(llvm::predecessors(block), [&](const llvm::BasicBlock *from) {
        return from != definition && reached.contains(from);
    });
}

// Lower one native-form function into the body its descriptor names.
class FrameLowering {
  public:
    // Borrow the module services and the native-form function; its body is created empty.
    FrameLowering(const Services &services, llvm::Function &native)
        : services_(services), native_(native),
          body_(*llvm::Function::Create(services.code, llvm::GlobalValue::InternalLinkage, native.getName() + ".body",
                                        services.output)),
          arity_(erlang_arity(&native).value_or(0)), roots_(arity_) {
        body_.setCallingConv(services.convention);
    }

    // Run every step and return the descriptor contents.
    llvm::Constant *lower() {
        move_blocks();
        build_prologue();
        replace_marker();
        const auto calls = erlang_calls();
        replace_allocas(calls);
        hoist_addresses();
        pass_arguments();
        std::vector<ErlangCall> tails;
        for (const auto &call : calls) {
            const auto *next = llvm::dyn_cast_or_null<llvm::ReturnInst>(call.call->getNextNode());
            if (next && next->getReturnValue() == call.call) {
                tails.push_back(call);
            } else {
                split(call);
            }
        }
        split_safepoints();
        spill_values();
        place_slots();
        // Inline transfers add control flow, so they follow the spill analysis (OPT0.1).
        for (const auto &call : tails) {
            lower_tail(call);
        }
        for (std::size_t i = 0; i < continuations_.size(); ++i) {
            lower_call(continuations_[i], i + 1);
        }
        lower_returns();
        finish_prologue();
        return descriptor_value();
    }

  private:
    // Move the blocks and debug scope into the body; the body takes the context in place of the native one.
    void move_blocks() {
        body_.getArg(0)->setName("context");
        body_.splice(body_.end(), &native_);
        native_.getArg(0)->replaceAllUsesWith(body_.getArg(0));
        body_.setSubprogram(native_.getSubprogram());
        native_.setSubprogram(nullptr);
        llvm::removeUnreachableBlocks(body_);
    }

    // Read the frame header and registers from the context's FrameState on entry; argument reads become reads of
    // the first slots, then of the parameters (pass_arguments).
    void build_prologue() {
        auto *entry = &body_.front();
        auto *block = llvm::BasicBlock::Create(body_.getContext(), "frame", &body_, entry);
        llvm::IRBuilder<> builder(block);
        auto *context = body_.getArg(0);
        auto *stack = services_.state(builder, context, StateField::stack, builder.getPtrTy(), "frame.stack");
        auto *offset = services_.state(builder, context, StateField::frame, services_.word, "frame.offset");
        auto *header = builder.CreateInBoundsGEP(services_.word, stack, offset, "frame.header");
        auto *registers =
            services_.state(builder, context, StateField::registers, builder.getPtrTy(), "frame.registers");
        auto *slots = builder.CreateConstInBoundsGEP1_64(services_.word, header, frame_header_words, "frame.slots");
        auto *resume = builder.CreateConstInBoundsGEP1_64(services_.word, header, frame_resume_word, "frame.resume");
        builder.CreateBr(entry);
        prologue_ = {block, registers, slots, resume};
        native_.getArg(1)->replaceAllUsesWith(slots);
        for (unsigned i = 0; i < register_arguments; ++i) {
            body_.getArg(i + 1)->setName("x" + std::to_string(i));
        }
    }

    // The leading arguments arrive as parameters (OPT0.1A): their slot reads become one value each, defined when the
    // function starts. It stays in its argument slot only when lower_frames spills it across a call or safepoint.
    void pass_arguments() {
        auto *start = prologue_.block->getTerminator()->getSuccessor(0);
        llvm::IRBuilder<> builder(start, start->getFirstInsertionPt());
        // A specialization dispatcher passes its argument slots on as the callee's arguments: fill them first.
        const bool forwarded = std::ranges::any_of(
            prologue_.slots->users(), [](const llvm::User *user) { return llvm::isa<llvm::CallInst>(user); });
        // Every value is created before any load is erased: a load may be the builder's insertion point.
        std::vector<llvm::Instruction *> arguments;
        for (std::size_t index = 0; index < std::min(arity_, register_arguments); ++index) {
            arguments.push_back(
                llvm::cast<llvm::Instruction>(builder.CreateFreeze(body_.getArg(index + 1), "argument")));
            if (forwarded) {
                builder.CreateAlignedStore(arguments.back(), argument_slot(builder, index), services_.align());
            }
        }
        for (std::size_t index = 0; index < arguments.size(); ++index) {
            const auto found =
                hoisted_.find({services_.word, {prologue_.slots, llvm::ConstantInt::get(services_.word, index)}});
            if (found == hoisted_.end()) {
                continue;
            }
            argument_loads_.emplace(arguments[index], found->second.front());
            for (auto *slot : found->second) {
                replace_loads(*slot, *arguments[index]);
            }
        }
    }

    // The address of argument slot `index`, computed where the builder is.
    llvm::Value *argument_slot(llvm::IRBuilder<> &builder, std::size_t index) const {
        return builder.CreateConstInBoundsGEP1_64(services_.word, prologue_.slots, index);
    }

    // Replace every load from one argument slot address by the argument's value.
    void replace_loads(llvm::Value &slot, llvm::Instruction &argument) {
        std::vector<llvm::LoadInst *> loads;
        for (auto *user : slot.users()) {
            if (auto *load = llvm::dyn_cast<llvm::LoadInst>(user)) {
                loads.push_back(load);
            }
        }
        for (auto *load : loads) {
            argument_loads_.erase(load);
            load->replaceAllUsesWith(&argument);
            load->eraseFromParent();
        }
    }

    // The marker names the term slot count and the stack-trace names; its slots are the frame's slots.
    void replace_marker() {
        for (auto *call : calls_to(services_.output.getFunction(FRAME_MARKER))) {
            roots_ = llvm::cast<llvm::ConstantInt>(call->getArgOperand(1))->getZExtValue();
            auto *names = llvm::cast<llvm::GlobalVariable>(call->getArgOperand(2));
            for (unsigned i = 0; i < names_.size(); ++i) {
                names_.at(i) = names->getInitializer()->getAggregateElement(i);
            }
            call->replaceAllUsesWith(prologue_.slots);
            call->eraseFromParent();
            if (names->use_empty()) {
                names->eraseFromParent();
            }
        }
    }

    // Recompute constant slot and register addresses on every entry: the stack may move during a call.
    void hoist_addresses() {
        std::set<llvm::Value *> stable{prologue_.registers, prologue_.slots};
        for (bool moved = true; moved;) {
            moved = false;
            for (auto &instruction : llvm::make_early_inc_range(llvm::instructions(body_))) {
                auto *address = llvm::dyn_cast<llvm::GetElementPtrInst>(&instruction);
                if (address && address->getParent() != prologue_.block &&
                    stable.contains(address->getPointerOperand()) && address->hasAllConstantIndices()) {
                    stable.insert(hoist(*address));
                    moved = true;
                }
            }
        }
        record_slot_stores();
        record_argument_loads();
    }

    // Move one constant address into the prologue. Register addresses (one per argument passed) reuse an identical
    // one; slot addresses stay distinct, one per service output or rooted value, so the IR names each use.
    llvm::Value *hoist(llvm::GetElementPtrInst &address) {
        auto &same = hoisted_[{address.getSourceElementType(), {address.op_begin(), address.op_end()}}];
        if (!same.empty() && address.getPointerOperand() == prologue_.registers) {
            address.replaceAllUsesWith(same.front());
            address.eraseFromParent();
            return same.front();
        }
        if (address.getPointerOperand() == prologue_.registers) {
            address.setName("register");
        }
        address.moveBefore(prologue_.block->getTerminator()->getIterator());
        address.dropLocation();
        same.push_back(&address);
        return &address;
    }

    // Index the loads from argument slots by load.
    void record_argument_loads() {
        for (std::size_t index = 0; index < arity_; ++index) {
            const auto found =
                hoisted_.find({services_.word, {prologue_.slots, llvm::ConstantInt::get(services_.word, index)}});
            for (auto *slot : found == hoisted_.end() ? std::vector<llvm::Value *>{} : found->second) {
                record_loads(*slot);
            }
        }
    }

    // Map every load from one argument slot address to that address.
    void record_loads(llvm::Value &slot) {
        for (auto *user : slot.users()) {
            if (llvm::isa<llvm::LoadInst>(user)) {
                argument_loads_.emplace(user, &slot);
            }
        }
    }

    // Index the stores into term slots by store.
    void record_slot_stores() {
        for (auto &instruction : *prologue_.block) {
            const auto *address = llvm::dyn_cast<llvm::GetElementPtrInst>(&instruction);
            if (!address || address->getPointerOperand() != prologue_.slots || raw_slots_.contains(address)) {
                continue;
            }
            for (auto *user : instruction.users()) {
                if (llvm::isa<llvm::StoreInst>(user)) {
                    slot_stores_.emplace(user, &instruction);
                }
            }
        }
    }

    // The term slot already holding the value: the argument slot it was read from (only ever rewritten with the same
    // value), or a slot a store in its own block put it in (not reused within a candidate). Else a new term slot for
    // a term, which a collection rewrites, or a raw slot for any other value.
    llvm::Value *home(const llvm::Instruction &value) {
        if (const auto argument = argument_loads_.find(&value); argument != argument_loads_.end()) {
            return argument->second;
        }
        for (const auto *user : value.users()) {
            const auto found = slot_stores_.find(user);
            if (found != slot_stores_.end() && llvm::cast<llvm::Instruction>(user)->getParent() == value.getParent()) {
                return found->second;
            }
        }
        if (terms_.contains(&value)) {
            return term_slot();
        }
        return raw_slot(body_.getParent()->getDataLayout().getTypeStoreSize(value.getType()).getFixedValue());
    }

    // Find every term word of the body: loaded from the registers or a term slot, stored into a term slot, marked
    // as a term by lowering (OPT1.2), or a PHI, freeze or select passing one on. Only use lists are walked: operand
    // accessors trip clang-analyzer in LLVM headers.
    void find_terms() {
        const auto accesses = term_accesses();
        std::vector<const llvm::Value *> pending;
        for (const auto &instruction : llvm::instructions(body_)) {
            if (instruction.getType() == services_.word &&
                (accesses.contains(&instruction) || instruction.getMetadata(TERM_METADATA) != nullptr ||
                 std::ranges::any_of(instruction.users(),
                                     [&](const llvm::User *user) { return accesses.contains(user); }))) {
                pending.push_back(&instruction);
            }
        }
        const auto passes_on = [](const llvm::User *user) {
            return llvm::isa<llvm::PHINode>(user) || llvm::isa<llvm::FreezeInst>(user) ||
                   llvm::isa<llvm::SelectInst>(user);
        };
        while (!pending.empty()) {
            const auto *value = pending.back();
            pending.pop_back();
            if (terms_.insert(value).second) {
                std::ranges::copy_if(value->users(), std::back_inserter(pending), passes_on);
            }
        }
    }

    // Loads from and stores into the registers and the term slots (every slot address but the raw ones).
    std::set<const llvm::User *> term_accesses() const {
        std::set<const llvm::User *> accesses;
        const auto record = [&](const llvm::Value &address) {
            std::ranges::copy_if(address.users(), std::inserter(accesses, accesses.end()), [](const llvm::User *user) {
                return llvm::isa<llvm::LoadInst>(user) || llvm::isa<llvm::StoreInst>(user);
            });
        };
        for (const auto *base : {prologue_.slots, prologue_.registers}) {
            record(*base);
            for (const auto *user : base->users()) {
                if (llvm::isa<llvm::GetElementPtrInst>(user) && !raw_slots_.contains(user)) {
                    record(*user);
                }
            }
        }
        return accesses;
    }

    // Native stack memory does not survive a transfer. Native form fills an Erlang call's argument array right
    // before the call, so those arrays become the registers; any other array becomes raw slots.
    void replace_allocas(const std::vector<ErlangCall> &calls) {
        std::set<const llvm::Value *> arguments;
        for (const auto &call : calls) {
            arguments.insert(call.call->getArgOperand(1));
        }
        for (auto &instruction : llvm::make_early_inc_range(llvm::instructions(body_))) {
            auto *array = llvm::dyn_cast<llvm::AllocaInst>(&instruction);
            if (!array) {
                continue;
            }
            const auto size = array->getAllocationSize(body_.getParent()->getDataLayout());
            if (!size || size->isScalable()) {
                throw std::logic_error("lower_frames: unsized native stack array");
            }
            array->replaceAllUsesWith(arguments.contains(array) ? prologue_.registers
                                                                : raw_slot(size->getFixedValue()));
            array->eraseFromParent();
        }
    }

    // Calls to native-form Erlang functions and fun calls; other native calls stay ordinary calls.
    std::vector<ErlangCall> erlang_calls() {
        std::vector<ErlangCall> calls;
        for (auto &callee : services_.output) {
            if (const auto arity = erlang_arity(&callee)) {
                for (auto *call : calls_to(&callee)) {
                    calls.push_back(
                        {call, services_.descriptor_of(callee), *arity, argument_stores(call->getArgOperand(1))});
                }
            }
        }
        for (auto *call : calls_to(services_.output.getFunction(APPLY_MARKER))) {
            calls.push_back({call, call->getArgOperand(2), 0});
        }
        return calls;
    }

    // Calls of `callee` in this body, found through its uses: native form passes functions only as callees.
    std::vector<llvm::CallInst *> calls_to(llvm::Function *callee) const {
        std::vector<llvm::CallInst *> calls;
        if (!callee) {
            return calls;
        }
        for (auto *user : callee->users()) {
            auto *call = llvm::dyn_cast<llvm::CallInst>(user);
            if (call && call->getFunction() == &body_) {
                calls.push_back(call);
            }
        }
        return calls;
    }

    // A tail call releases this frame and enters the callee, whose return reaches this function's caller.
    void lower_tail(const ErlangCall &call) {
        llvm::IRBuilder<> builder(call.call);
        copy_arguments(builder, call);
        enter(builder, call.descriptor, parameters(builder, call), true);
    }

    // The parameters of a call's transfer (OPT0.1A): a direct call's leading argument values, whose stores into the
    // registers go away; else the registers already holding them (a fun call, or arguments copied from slots); zero
    // past the arity.
    Parameters parameters(llvm::IRBuilder<> &builder, const ErlangCall &call) const {
        Parameters values{};
        for (std::size_t i = 0; i < register_arguments; ++i) {
            if (auto *store = call.stores.at(i)) {
                values.at(i) = store->getValueOperand();
                store->eraseFromParent();
            } else if (call.arity == 0 || i < call.arity) {
                values.at(i) = builder.CreateAlignedLoad(services_.word, register_address(builder, i),
                                                         services_.align(), "argument");
            } else {
                values.at(i) = llvm::ConstantInt::get(services_.word, 0);
            }
        }
        return values;
    }

    // The address of register `index`.
    llvm::Value *register_address(llvm::IRBuilder<> &builder, std::size_t index) const {
        return builder.CreateConstInBoundsGEP1_64(services_.word, prologue_.registers, index);
    }

    // Store the parameters into the registers before a transfer service, which reads them there; then load them
    // back, as the service may have rewritten them (a collection, a builtin's result).
    Parameters through_registers(llvm::IRBuilder<> &builder, const Parameters &values, llvm::Value *&code,
                                 llvm::FunctionCallee service, llvm::ArrayRef<llvm::Value *> arguments) const {
        for (std::size_t i = 0; i < register_arguments; ++i) {
            builder.CreateAlignedStore(values.at(i), register_address(builder, i), services_.align());
        }
        code = builder.CreateCall(service, arguments, "transfer.service");
        Parameters loaded{};
        for (std::size_t i = 0; i < register_arguments; ++i) {
            loaded.at(i) =
                builder.CreateAlignedLoad(services_.word, register_address(builder, i), services_.align(), "x");
        }
        return loaded;
    }

    // End the call's block at the call; the result arrives in the first register when the body resumes.
    void split(const ErlangCall &call) {
        auto *resume = call.call->getParent()->splitBasicBlock(std::next(call.call->getIterator()), "resume");
        llvm::IRBuilder<> builder(resume, resume->getFirstInsertionPt());
        builder.SetCurrentDebugLocation(call.call->getDebugLoc());
        // The result arrives as the first parameter when the body resumes here (OPT0.1A).
        // An Erlang call's result is a term (OPT1.2: lowering no longer stores every value into a root slot).
        auto *result = llvm::cast<llvm::Instruction>(builder.CreateFreeze(body_.getArg(1), "call.result"));
        result->setMetadata(TERM_METADATA, llvm::MDNode::get(body_.getContext(), {}));
        call.call->replaceAllUsesWith(result);
        continuations_.push_back({call, resume});
    }

    // A loop-head safepoint may move the heap: values read after it are spilled as after a call, without a transfer.
    void split_safepoints() {
        const auto symbol = services::symbol<services::Safepoint>(services_.output.getTargetTriple());
        for (auto *call : calls_to(services_.output.getFunction(symbol))) {
            safepoints_.push_back(call->getParent()->splitBasicBlock(std::next(call->getIterator()), "safepoint"));
        }
    }

    // Store every value read after a resume point or safepoint it was computed before in a slot.
    void spill_values() {
        if (continuations_.empty() && safepoints_.empty()) {
            return;
        }
        find_terms();
        std::vector<llvm::Instruction *> crossing;
        for (auto &block : body_) {
            for (auto &instruction : block) {
                if (&block != prologue_.block && crosses(instruction)) {
                    crossing.push_back(&instruction);
                }
            }
        }
        for (auto *instruction : crossing) {
            spill(*instruction);
        }
    }

    // A use outside the defining block that some resume point reaches without passing the definition.
    bool crosses(const llvm::Instruction &value) {
        const auto &reached = resumed(value.getParent());
        return std::ranges::any_of(value.uses(),
                                   [&](const llvm::Use &use) { return reads_in(use, value.getParent(), reached); });
    }

    // Blocks reachable from a resume point without entering `definition`, cached per defining block.
    const std::set<const llvm::BasicBlock *> &resumed(const llvm::BasicBlock *definition) {
        auto [found, fresh] = resumed_.try_emplace(definition);
        if (!fresh) {
            return found->second;
        }
        std::vector<const llvm::BasicBlock *> pending;
        for (const auto &continuation : continuations_) {
            if (continuation.resume != definition) {
                pending.push_back(continuation.resume);
            }
        }
        std::ranges::copy_if(safepoints_, std::back_inserter(pending),
                             [&](const llvm::BasicBlock *block) { return block != definition; });
        while (!pending.empty()) {
            const auto *block = pending.back();
            pending.pop_back();
            if (found->second.insert(block).second) {
                std::ranges::copy_if(llvm::successors(block), std::back_inserter(pending),
                                     [&](const llvm::BasicBlock *next) { return next != definition; });
            }
        }
        return found->second;
    }

    // Store the value after its definition and load it before each use (LLVM's register demotion), in a raw slot.
    void spill(llvm::Instruction &value) {
        if (value.getType()->isPointerTy()) {
            throw std::logic_error("lower_frames: a native pointer is live across a call");
        }
        auto *slot = home(value);
        auto *stack = llvm::DemoteRegToStack(value);
        for (auto *user : stack->users()) {
            // A reload sits right before its reader, the store right after the definition: share their lines.
            if (auto *load = llvm::dyn_cast<llvm::LoadInst>(user)) {
                load->setAlignment(services_.align());
                load->setDebugLoc(load->getNextNode()->getDebugLoc());
            } else {
                llvm::cast<llvm::StoreInst>(user)->setAlignment(services_.align());
                llvm::cast<llvm::StoreInst>(user)->setDebugLoc(value.getDebugLoc());
            }
        }
        stack->replaceAllUsesWith(slot);
        stack->eraseFromParent();
    }

    // Record the resume index, pass the arguments and enter the callee; its return resumes this body.
    void lower_call(const Continuation &continuation, std::size_t index) {
        llvm::IRBuilder<> builder(continuation.call.call);
        builder.CreateAlignedStore(llvm::ConstantInt::get(services_.word, index), prologue_.resume, services_.align());
        copy_arguments(builder, continuation.call);
        enter(builder, continuation.call.descriptor, parameters(builder, continuation.call), false);
    }

    // Enter `descriptor` with its arguments in the registers, replacing the call at the builder: pushed inline
    // (OPT0.1) unless the entry needs the service (a wanted collection, profiling, no reductions left, stack growth
    // or a builtin); a tail entry first releases this frame.
    void enter(llvm::IRBuilder<> &builder, llvm::Value *descriptor, const Parameters &values, bool tail) {
        auto *context = body_.getArg(0);
        const auto callee = load_callee(builder, descriptor);
        auto *at =
            services_.state(builder, context, tail ? StateField::frame : StateField::top, services_.word, "enter.at");
        auto *need = builder.CreateAdd(
            at, builder.CreateAdd(callee.slots, llvm::ConstantInt::get(services_.word, frame_header_words)),
            "enter.need");
        auto *reductions = services_.state(builder, context, StateField::reductions, services_.word, "enter.reds");
        auto *limit = services_.state(builder, context, StateField::limit, services_.word, "enter.limit");
        auto *slow = services_.state(builder, context, StateField::slow, services_.word, "enter.slow");
        auto *zero = llvm::ConstantInt::get(services_.word, 0);
        auto *fast = builder.CreateAnd(
            {compare(builder, llvm::CmpInst::ICMP_EQ, slow, zero),
             compare(builder, llvm::CmpInst::ICMP_NE, reductions, zero),
             compare(builder, llvm::CmpInst::ICMP_NE, callee.body, llvm::ConstantPointerNull::get(builder.getPtrTy())),
             compare(builder, llvm::CmpInst::ICMP_ULE, need, limit)});
        auto *rest = diamond(builder, fast, "enter");
        push_inline(builder, descriptor, callee, {at, need, reductions}, tail);
        transfer(builder, callee.body, values);
        builder.SetInsertPoint(rest->getPrevNode());
        llvm::Value *code = nullptr;
        const auto loaded =
            through_registers(builder, values, code, tail ? services_.tail : services_.enter, {context, descriptor});
        transfer(builder, code, loaded);
        discard(*rest);
    }

    // An integer or pointer comparison; built directly because IRBuilder's helpers trip clang-analyzer in LLVM
    // headers.
    static llvm::Value *compare(llvm::IRBuilder<> &builder, llvm::CmpInst::Predicate predicate, llvm::Value *left,
                                llvm::Value *right) {
        return builder.Insert(llvm::CmpInst::Create(llvm::Instruction::ICmp, predicate, left, right));
    }

    // The arity, body and slot count of the descriptor an entry pushes.
    Callee load_callee(llvm::IRBuilder<> &builder, llvm::Value *descriptor) const {
        auto *word = services_.word;
        return {services_.descriptor_field(builder, descriptor, DESCRIPTOR_ARITY, word, "callee.arity"),
                services_.descriptor_field(builder, descriptor, DESCRIPTOR_BODY, builder.getPtrTy(), "callee.body"),
                services_.descriptor_field(builder, descriptor, DESCRIPTOR_SLOTS, word, "callee.slots")};
    }

    struct Push {
        // Header offset of the new frame, the stack top after it and the reductions left before the entry.
        llvm::Value *at;
        llvm::Value *top;
        llvm::Value *reductions;
    };

    // ProcessStack::enter inline: spend a reduction, write the header, zero the slots, copy the arguments past the
    // parameters from the registers and make the frame current. A tail entry reuses this frame's place and its
    // caller link. The callee stores its parameters itself where it keeps them (OPT0.1A).
    void push_inline(llvm::IRBuilder<> &builder, llvm::Value *descriptor, const Callee &callee, const Push &push,
                     bool tail) const {
        auto *context = body_.getArg(0);
        auto *word = services_.word;
        auto *zero = llvm::ConstantInt::get(word, 0);
        services_.set_state(builder, context, StateField::reductions,
                            builder.CreateSub(push.reductions, llvm::ConstantInt::get(word, 1)));
        auto *stack = services_.state(builder, context, StateField::stack, builder.getPtrTy(), "push.stack");
        auto *header = builder.CreateInBoundsGEP(word, stack, push.at, "push.header");
        auto *previous = tail ? builder.CreateAlignedLoad(word, header, services_.align(), "push.previous")
                              : services_.state(builder, context, StateField::frame, word, "push.previous");
        // The descriptor word is stored as a pointer of the same width, so no pointer conversion appears.
        const std::array<llvm::Value *, frame_header_words> words{previous, descriptor, zero, zero};
        for (std::size_t i = 0; i < words.size(); ++i) {
            builder.CreateAlignedStore(words.at(i), builder.CreateConstInBoundsGEP1_64(word, header, i),
                                       services_.align());
        }
        const auto unit = llvm::ConstantInt::get(word, word->getBitWidth() / 8);
        auto *slots = builder.CreateConstInBoundsGEP1_64(word, header, frame_header_words, "push.slots");
        builder.CreateMemSet(slots, builder.getInt8(0), builder.CreateMul(callee.slots, unit), services_.align());
        auto *passed = llvm::ConstantInt::get(word, register_arguments);
        auto *rest =
            builder.CreateSub(builder.CreateBinaryIntrinsic(llvm::Intrinsic::umax, callee.arity, passed), passed);
        builder.CreateMemCpy(builder.CreateConstGEP1_64(word, slots, register_arguments), services_.align(),
                             builder.CreateConstGEP1_64(word, prologue_.registers, register_arguments),
                             services_.align(), builder.CreateMul(rest, unit));
        services_.set_state(builder, context, StateField::frame, push.at);
        services_.set_state(builder, context, StateField::top, push.top);
        services_.set_state(builder, context, StateField::live, zero);
    }

    // Return `result` in the first register to the caller's body, replacing the return at the builder: popped
    // inline (OPT0.1) unless the process profiles.
    void leave(llvm::IRBuilder<> &builder, llvm::Value *result) {
        auto *context = body_.getArg(0);
        auto *word = services_.word;
        auto *zero = llvm::ConstantInt::get(word, 0);
        // The result is the caller body's first parameter (OPT0.1A).
        Parameters values{};
        values.fill(zero);
        values.front() = result;
        auto *slow = services_.state(builder, context, StateField::slow, word, "return.slow");
        auto *fast = compare(builder, llvm::CmpInst::ICMP_EQ, builder.CreateAnd(slow, abi::v1::slow_profile), zero);
        auto *rest = diamond(builder, fast, "return");
        auto *frame = services_.state(builder, context, StateField::frame, word, "return.frame");
        auto *stack = services_.state(builder, context, StateField::stack, builder.getPtrTy(), "return.stack");
        auto *header = builder.CreateInBoundsGEP(word, stack, frame);
        auto *previous = builder.CreateAlignedLoad(word, header, services_.align(), "return.previous");
        services_.set_state(builder, context, StateField::top, frame);
        services_.set_state(builder, context, StateField::frame, previous);
        services_.set_state(builder, context, StateField::live, zero);
        // The caller's descriptor word, loaded as a pointer of the same width.
        auto *caller = builder.CreateAlignedLoad(
            builder.getPtrTy(),
            builder.CreateInBoundsGEP(word, stack, builder.CreateAdd(previous, llvm::ConstantInt::get(word, 1))),
            services_.align(), "return.caller");
        auto *body = services_.descriptor_field(builder, caller, DESCRIPTOR_BODY, builder.getPtrTy(), "return.body");
        transfer(builder, body, values);
        builder.SetInsertPoint(rest->getPrevNode());
        llvm::Value *code = nullptr;
        const auto loaded = through_registers(builder, values, code, services_.leave, {context, result});
        transfer(builder, code, loaded);
        discard(*rest);
    }

    // Split the builder's block at its insertion point and branch on `condition` to an inline and a service block,
    // which each end in their own transfer; the builder moves to the inline block. Returns the rest of the split
    // block, the replaced call or return, for discard().
    llvm::BasicBlock *diamond(llvm::IRBuilder<> &builder, llvm::Value *condition, const llvm::Twine &name) {
        auto *head = builder.GetInsertBlock();
        auto *rest = head->splitBasicBlock(builder.GetInsertPoint(), name + ".rest");
        head->getTerminator()->eraseFromParent();
        auto *fast = llvm::BasicBlock::Create(body_.getContext(), name + ".inline", &body_, rest);
        auto *slow = llvm::BasicBlock::Create(body_.getContext(), name + ".service", &body_, rest);
        builder.SetInsertPoint(head);
        builder.CreateCondBr(condition, fast, slow);
        builder.SetInsertPoint(fast);
        return rest;
    }

    // Delete the unreachable rest of a split block, last instruction first so no value is erased before its users.
    static void discard(llvm::BasicBlock &rest) {
        while (!rest.empty()) {
            rest.back().eraseFromParent();
        }
        rest.eraseFromParent();
    }

    // Every return passes its value to the caller's body, inline or through the return service.
    void lower_returns() {
        std::vector<llvm::ReturnInst *> returns;
        for (auto &block : body_) {
            auto *ret = llvm::dyn_cast<llvm::ReturnInst>(block.getTerminator());
            if (ret && ret->getReturnValue()) {
                returns.push_back(ret);
            }
        }
        for (auto *ret : returns) {
            llvm::IRBuilder<> builder(ret);
            leave(builder, ret->getReturnValue());
        }
    }

    // Dispatch on the resume index: 0 starts the function, index k continues after the k-th call.
    void finish_prologue() {
        if (continuations_.empty()) {
            return;
        }
        auto *terminator = prologue_.block->getTerminator();
        llvm::IRBuilder<> builder(terminator);
        auto *resume = builder.CreateAlignedLoad(services_.word, prologue_.resume, services_.align(), "resume");
        auto *dispatch = builder.CreateSwitch(resume, terminator->getSuccessor(0), continuations_.size());
        for (std::size_t i = 0; i < continuations_.size(); ++i) {
            dispatch->addCase(llvm::ConstantInt::get(services_.word, i + 1), continuations_[i].resume);
        }
        terminator->eraseFromParent();
    }

    // Copy arguments into the registers unless the call's array already is the registers.
    void copy_arguments(llvm::IRBuilder<> &builder, const ErlangCall &call) const {
        auto *arguments = call.call->getArgOperand(1);
        if (call.arity != 0 && arguments != prologue_.registers) {
            builder.CreateMemCpy(prologue_.registers, services_.align(), arguments, services_.align(),
                                 call.arity * (services_.word->getBitWidth() / 8));
        }
    }

    // Leave the body by a guaranteed tail call of `code` with the leading X registers as parameters.
    void transfer(llvm::IRBuilder<> &builder, llvm::Value *code, const Parameters &values) const {
        std::vector<llvm::Value *> arguments{body_.getArg(0)};
        arguments.insert(arguments.end(), values.begin(), values.end());
        auto *call = builder.CreateCall(services_.code, code, arguments);
        call->setCallingConv(services_.convention);
        call->setTailCallKind(llvm::CallInst::TCK_MustTail);
        builder.CreateRetVoid();
    }

    // Reserve raw words after the term slots; place_slots() fixes their index, recomputed on every entry.
    llvm::Value *raw_slot(std::uint64_t bytes) {
        const auto unit = services_.word->getBitWidth() / 8;
        auto *slot = slot_address(roots_ + raw_, "frame.raw");
        raw_addresses_.emplace_back(slot, raw_);
        raw_ += std::max<std::uint64_t>(1, (bytes + unit - 1) / unit);
        raw_slots_.insert(slot);
        return slot;
    }

    // Reserve a term slot for a spilled term after the marker's term slots; a collection rewrites it.
    llvm::Value *term_slot() { return slot_address(roots_ + spilled_terms_++, "frame.term"); }

    // The address of slot `index`, computed in the prologue.
    llvm::GetElementPtrInst *slot_address(std::uint64_t index, const char *name) {
        llvm::IRBuilder<> builder(prologue_.block->getTerminator());
        auto *slot = llvm::dyn_cast<llvm::GetElementPtrInst>(
            builder.CreateConstInBoundsGEP1_64(services_.word, prologue_.slots, index, name));
        if (!slot) {
            throw std::logic_error("lower_frames: slot address folded");
        }
        return slot;
    }

    // Spilled term slots join the leading term slots, which are roots; raw slots move after them.
    void place_slots() {
        for (const auto &[slot, offset] : raw_addresses_) {
            slot->setOperand(1, llvm::ConstantInt::get(services_.word, roots_ + spilled_terms_ + offset));
        }
        roots_ += spilled_terms_;
    }

    // FrameDescriptor contents: stack-trace names, body, all slots and the leading term slots.
    llvm::Constant *descriptor_value() {
        auto *ptr = llvm::PointerType::get(body_.getContext(), 0);
        auto *word = services_.word;
        if (!names_[0]) {
            names_ = {llvm::ConstantPointerNull::get(ptr), llvm::ConstantInt::get(word, 0),
                      llvm::ConstantInt::get(word, 0), llvm::ConstantInt::get(word, arity_)};
        }
        return llvm::ConstantStruct::get(services_.descriptor, {names_[0], names_[1], names_[2], names_[3], &body_,
                                                                llvm::ConstantInt::get(word, roots_ + raw_),
                                                                llvm::ConstantInt::get(word, roots_)});
    }

    // Module services and the function being moved from native form into its body.
    const Services &services_;
    llvm::Function &native_;
    llvm::Function &body_;
    std::size_t arity_;
    Prologue prologue_{};
    // Term slots named by the marker (arguments first) plus spilled terms, then raw words for other spills and
    // native arrays; raw addresses are placed once the spilled term count is known.
    std::size_t roots_;
    std::size_t spilled_terms_ = 0;
    std::size_t raw_ = 0;
    std::vector<std::pair<llvm::GetElementPtrInst *, std::size_t>> raw_addresses_;
    // Stack-trace fields copied from the marker's name descriptor; empty without a marker.
    std::array<llvm::Constant *, 4> names_{};
    // Non-tail calls in resume-index order and the blocks each resume point reaches.
    std::vector<Continuation> continuations_;
    // Blocks continuing after each loop-head safepoint call.
    std::vector<const llvm::BasicBlock *> safepoints_;
    std::map<const llvm::BasicBlock *, std::set<const llvm::BasicBlock *>> resumed_;
    // Constant prologue addresses by element type and operands.
    using Address = std::pair<llvm::Type *, std::vector<llvm::Value *>>;
    std::map<Address, std::vector<llvm::Value *>> hoisted_;
    // Stores into term slots, mapped to the slot, and the raw slot addresses, which are not term slots.
    std::map<const llvm::User *, llvm::Value *> slot_stores_;
    std::map<const llvm::Value *, llvm::Value *> argument_loads_;
    std::set<const llvm::Value *> raw_slots_;
    // Term words of the body, spilled to term slots (found before spilling).
    std::set<const llvm::Value *> terms_;
};

// Exported entries keep their symbol as a host entry that runs the body above a bottom frame.
void finish_native(const Services &services, llvm::Function &native, llvm::Constant *descriptor) {
    native.removeFnAttr(ARITY_ATTRIBUTE);
    if (native.hasLocalLinkage() && native.use_empty()) {
        native.eraseFromParent();
        return;
    }
    llvm::IRBuilder<> builder(llvm::BasicBlock::Create(native.getContext(), "entry", &native));
    builder.CreateRet(builder.CreateCall(services.invoke, {native.getArg(0), descriptor, native.getArg(1)}));
}

// Drop declarations only native-form code referred to.
void erase_unused(llvm::Module &output) {
    for (auto &function : llvm::make_early_inc_range(output)) {
        const bool erlang = function.isDeclaration() && erlang_arity(&function);
        const bool marker =
            function.getName() == llvm::StringRef(FRAME_MARKER) || function.getName() == llvm::StringRef(APPLY_MARKER);
        if ((erlang || marker) && function.use_empty()) {
            function.eraseFromParent();
        }
    }
}

// Create every descriptor first so calls between functions of the module resolve to them.
void lower_module(llvm::Module &output) {
    std::vector<llvm::Function *> natives;
    for (auto &function : output) {
        if (!function.isDeclaration() && erlang_arity(&function)) {
            natives.push_back(&function);
        }
    }
    if (natives.empty()) {
        return;
    }
    const auto services = frame_services(output, llvm::cast<llvm::IntegerType>(natives.front()->getReturnType()));
    std::vector<llvm::GlobalVariable *> descriptors;
    descriptors.reserve(natives.size());
    for (auto *native : natives) {
        // Fun descriptors may have declared this function's descriptor already.
        auto *descriptor = llvm::cast<llvm::GlobalVariable>(
            output.getOrInsertGlobal((native->getName() + ".frame").str(), services.descriptor));
        descriptor->setConstant(true);
        descriptor->setLinkage(native->hasLocalLinkage() ? llvm::GlobalValue::InternalLinkage
                                                         : llvm::GlobalValue::ExternalLinkage);
        descriptors.push_back(descriptor);
    }
    for (std::size_t i = 0; i < natives.size(); ++i) {
        descriptors[i]->setInitializer(FrameLowering(services, *natives[i]).lower());
    }
    for (std::size_t i = 0; i < natives.size(); ++i) {
        finish_native(services, *natives[i], descriptors[i]);
    }
    erase_unused(output);
}
} // namespace

bool lower_frames(Compilation &compilation) {
    try {
        for (auto &output : detail::state(compilation).modules) {
            lower_module(*output);
        }
    } catch (const std::exception &error) {
        compilation.result().report(
            {.level = DiagnosticLevel::error, .message = error.what(), .location = {}, .module_name = {}});
        return false;
    }
    return verify_ir(compilation);
}
} // namespace clause::codegen
