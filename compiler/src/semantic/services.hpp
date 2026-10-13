#pragma once
#include "binding_state.hpp"

namespace clause::semantic {
// Keep the semantic catalog available to metadata validation without consulting runtime registration.
bool guard_signature(const FunctionKey &key);
// Whether OTP auto-imports the erlang function unqualified: a guard BIF or an auto-imported body BIF Clause knows.
bool auto_imported_bif(const FunctionKey &key);
// A local call of a function -import takes from a module other than erlang that is a bridge builtin there
// (io:format/2): its name and bridge index.
std::optional<std::pair<FunctionKey, std::size_t>> imported_builtin(const Module &module,
                                                                    const ast::CallExpression &call);
// Classify supported immediate operators without widening guard legality or numeric representation support.
std::optional<abi::v1::ImmediateOperation> immediate_operator(ast::BinaryOperator operation);
// Map every authorized unary operator to the shared checked numeric/boolean service boundary.
std::optional<abi::v1::ImmediateOperation> immediate_unary(ast::UnaryOperator operation);
// Map only resolved erlang name/arity identities to executable service operations.
std::optional<abi::v1::ImmediateOperation> immediate_service(const FunctionKey &key);
// Identify erlang body-only builtins, never guard-legal: explicit display/1, raise/3 and other bridge builtins, and
// halt/0,1, the raising error/1,2,3, exit/1 and throw/1 and the dynamic apply/2,3, also unqualified through
// auto-import.
std::optional<FunctionKey> body_builtin(BindingAnalysis &state, const ast::ExprId &id, const ast::CallExpression &call);
// The bridge index (abi::v1::bridge_builtins) of erlang:Name/Arity, when the runtime provides it as a builtin.
std::optional<std::size_t> bridge_builtin(const FunctionKey &key);
// The bridge index of Module:Name/Arity for any module, such as io:format/2.
std::optional<std::size_t> bridge_builtin(std::u32string_view module, const FunctionKey &key);
// A qualified call of a bridge builtin of a module other than erlang (io:format/2): its name and bridge index.
// Such builtins take precedence over a batch module of the same name, as OTP's sticky modules cannot be replaced.
std::optional<std::pair<FunctionKey, std::size_t>> module_builtin(const ast::Module &syntax,
                                                                  const ast::CallExpression &call);
// The erlang builtin a local fun F/A names: an auto-imported bridge builtin the module neither defines nor
// suppresses (OTP makes fun abs/1 the external fun erlang:abs/1).
std::optional<FunctionKey> builtin_fun(BindingAnalysis &state, const ast::ExprId &id,
                                       const ast::LocalFunReference &reference);
// Resolve legal guard calls using the same imports/suppression rules as embedded pattern expressions.
std::optional<FunctionKey> guard_identity(BindingAnalysis &state, const ast::ExprId &id,
                                          const ast::CallExpression &call, bool top_test);
// Validate all guard operands, including unreachable ones, and record ordinary immediate service calls; without a
// work budget, the default one for the module's size.
void resolve_services(Module &module, const Reporter &out, std::optional<std::size_t> work_limit = {});
// What a form the capability check flags would change, named for the diagnostic (`-on_load attribute`, `-compile
// option {parse_transform,m}`); empty for a -compile whose options are all inert.
std::optional<std::string> rejected_attribute(const ast::Module &syntax, const ast::FormValue &value);
} // namespace clause::semantic
