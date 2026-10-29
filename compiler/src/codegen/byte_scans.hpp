#pragma once
#include "lowering_state.hpp"
#include <functional>

namespace clause::codegen {
// Byte scans (OPT1.5): a binary loop clause that consumes one byte of a class, such as
// `f(<<C, R/binary>>, N) when C >= $0, C =< $9 -> f(R, N + 1)`, runs again for every following byte of the class.
// Its tail call in the loop variant finds that run with one 16-byte vector test and passes the rest past it on.

// The scan clause of a binary loop over `parameter`: the first clause that matches one byte and a rest, when every
// earlier clause needs a literal first byte or an empty binary there, its guard tests only that byte, and its body
// is a self call passing the rest and each other parameter unchanged or plus a constant; none otherwise.
std::optional<ByteScan> byte_scan(const ExpressionLowering &state, const std::vector<ast::FunctionClause> &clauses,
                                  std::size_t parameter);
// The scan clause's variant tail call: `enter` passes a rest on, once unchanged and once past the run of accepted
// bytes that follows (up to 16) with each counter argument of `call` in `state.values` advanced by its step per
// skipped byte. The run is skipped only when 16 bytes remain from a byte boundary, the first of them is accepted and
// every counter is a small integer that 16 steps keep inside the word.
void scan_tail(ExpressionLowering &state, const ByteScan &scan, const LoopRest &rest, const ast::CallExpression &call,
               const std::function<void(const LoopRest &)> &enter);
} // namespace clause::codegen
