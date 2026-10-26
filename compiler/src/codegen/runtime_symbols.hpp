#pragma once
#include <clause/compiler/mangling.hpp>
#include <llvm/TargetParser/Triple.h>
#include <string_view>

// Native C++ declarations of runtime services called by generated code; mirrors abi/include/clause/abi.
namespace clause::codegen::services {
using mangling::Char;
using mangling::Const;
using mangling::Enum;
using mangling::Function;
using mangling::Int32;
using mangling::Pointer;
using mangling::Size;
using mangling::UInt8;
using mangling::Void;

// Opaque runtime/process context passed first to every service.
using Context = Pointer<Void>;
// Target-width term word (abi::v1::TermWord) inputs and output slots.
using Words = Pointer<Const<Size>>;
using Slot = Pointer<Size>;
using Bytes = Pointer<Const<Char>>;
using Descriptor = Pointer<Const<Void>>;
using Reason = Enum<"clause::abi::v1::ErrorReason">;

using Integer = Function<"CLAUSE_integer_v1", UInt8, Context, Bytes, Size, Slot>;
using Float = Function<"CLAUSE_float_v1", UInt8, Context, Bytes, Size, Slot>;
using Immediate = Function<"CLAUSE_immediate_v1", UInt8, Context, UInt8, Size, Size, Slot>;
using Construct = Function<"CLAUSE_construct_v1", UInt8, Context, UInt8, Words, Size, Slot>;
using Inspect = Function<"CLAUSE_inspect_v1", UInt8, Context, UInt8, Size, Size, Slot>;
using Bits = Function<"CLAUSE_bits_v1", UInt8, Context, UInt8, Words, Size, Slot>;
using Map = Function<"CLAUSE_map_v1", UInt8, Context, UInt8, Words, Size, Slot>;
using Record = Function<"CLAUSE_record_v1", UInt8, Context, UInt8, UInt8, Descriptor, Words, Size, Slot>;
using Display = Function<"CLAUSE_display_v1", UInt8, Context, Size, Slot>;
// Bridge builtins (abi/builtins.hpp): the FrameDescriptor of a bridge index, entered like a function.
using BuiltinFrame = Function<"CLAUSE_builtin_frame_v1", Descriptor, Context, Size>;
using BuiltinCall = Function<"CLAUSE_builtin_call_v1", Size, Context, Size, Words>;
using Halt = Function<"CLAUSE_halt_v1", UInt8, Context, Size>;
using Exact = Function<"CLAUSE_exact_v1", UInt8, Context, Size, Size>;
using CallFailed = Function<"CLAUSE_call_failed_v2", UInt8, Context>;
using Raise = Function<"CLAUSE_raise_v2", UInt8, Context, Reason, Size>;
using Catch = Function<"CLAUSE_catch_v1", UInt8, Context, Slot>;
using Exception = Function<"CLAUSE_exception_v2", UInt8, Context, Slot, Slot, Slot>;
using Reraise = Function<"CLAUSE_reraise_v2", UInt8, Context, Size, Size, Size>;
using Error = Function<"CLAUSE_error_v1", UInt8, Context, Size, Size>;
// Function values (abi/funs.hpp): build a fun, and check a called one, returning the FrameDescriptor to enter.
using MakeFun = Function<"CLAUSE_make_fun_v1", UInt8, Context, Descriptor, Words, Size, Slot>;
using Apply = Function<"CLAUSE_apply_v1", Descriptor, Context, Size, Size, Slot>;
// Dynamic calls: M:F(Args), apply/2,3 (arguments unpacked into the registers) and fun M:F/A with variables.
using Call = Function<"CLAUSE_call_v1", Descriptor, Context, Size, Size, Size>;
using ApplyList = Function<"CLAUSE_apply_list_v1", Descriptor, Context, Size, Size, Slot>;
using CallList = Function<"CLAUSE_call_list_v1", Descriptor, Context, Size, Size, Size, Slot>;
using MakeExternalFun = Function<"CLAUSE_make_external_fun_v1", UInt8, Context, Size, Size, Size, Slot>;
// Selective receive (abi/messages.hpp): one mailbox step, and the frame of the builtin a receive waits in.
using Receive = Function<"CLAUSE_receive_v1", UInt8, Context, UInt8, Slot>;
using WaitFrame = Function<"CLAUSE_wait_frame_v1", Descriptor, Context>;
// Frame transfers (abi/frames.hpp): each returns the continuation code generated code tail-calls next.
using Code = Pointer<Void>;
using Enter = Function<"CLAUSE_enter_v1", Code, Context, Descriptor>;
using Tail = Function<"CLAUSE_tail_v1", Code, Context, Descriptor>;
using Return = Function<"CLAUSE_return_v1", Code, Context, Size>;
// Loop-head safepoint: may collect and rewrite term slots (docs/runtime-heap.md#collection-in-generated-code).
using Safepoint = Function<"CLAUSE_safepoint_v1", Void, Context>;
using Frame = Function<"CLAUSE_frame_v1", Slot, Context>;
using Registers = Function<"CLAUSE_registers_v1", Slot, Context>;
using Invoke = Function<"CLAUSE_invoke_v1", Size, Context, Descriptor, Words>;
using RegisterModule = Function<"CLAUSE_register_module_v4", UInt8, Context, Descriptor>;
using Atom = Function<"CLAUSE_atom_v3", Size, Context, Size, Descriptor>;
// A module literal's word in the literal heap of a runtime that did not claim the module (literal heap).
using Literal = Function<"CLAUSE_literal_v1", Size, Context, Size, Descriptor>;
// Program startup called by the generated native `main` (abi/startup.hpp).
using Main = Function<"CLAUSE_main_v1", Int32, Int32, Pointer<Pointer<Char>>, Descriptor>;
// The crypto runtime archive's extension entry, stored in the startup descriptor (abi/startup.hpp). Added for the
// crypto runtime archive.
using CryptoExtension = Function<"CLAUSE_crypto_extension_v1", Descriptor>;
// Its entry for programs pinned to FIPS (--fips). Added for crypto's FIPS mode (plan 21 step LIB79F).
using CryptoFipsExtension = Function<"CLAUSE_crypto_fips_extension_v1", Descriptor>;

// Mangling family and pointer width of the emitted target, independent of the compiler host.
inline mangling::Target target(const llvm::Triple &triple) {
    return {triple.isWindowsMSVCEnvironment() ? mangling::Scheme::microsoft : mangling::Scheme::itanium,
            triple.isArch64Bit()};
}

// Linker spelling of `Service` for the emitted target.
template <typename Service> std::string_view symbol(const llvm::Triple &triple) {
    return Service::symbol(target(triple));
}
} // namespace clause::codegen::services
