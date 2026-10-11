#pragma once
#include "declarations.hpp"

// The erlang library module (library/stdlib/erlang.erl, plan step 41B): which unqualified names reach it.
namespace clause::semantic {
// Whether OTP auto-imports erlang:F/A (erl_internal:bif/2), from the owned inventory library/stdlib/erlang.terms.
bool otp_auto_import(const FunctionKey &key);
// Whether the compiler handles erlang:F/A itself, without the library module: a bridge builtin, a guard BIF or a
// body builtin such as apply/3.
bool compiler_handled(const FunctionKey &key);
// Whether an unqualified call or fun of `key` reaches erlang:F/A of the library module: OTP auto-imports it, the
// module neither defines, imports nor suppresses it, and no builtin the compiler handles has the name.
bool library_auto_import(const ast::Module &syntax, const FunctionKey &key);
} // namespace clause::semantic
