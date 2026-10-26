#pragma once
// Added for the ASN.1 compiler (plan 21 step LIB84): the records of the generated .hrl file, as asn1ct's gen_record
// lists them (parameterized type definitions first, then the module's types, each with its inline SEQUENCEs and
// SETs).
#include "checker_internal.hpp"

namespace clause::asn1::detail {
// `definitions` are the parameterized type assignments and `types` the named types of the compiled module, in
// asn1ct's order.
void collect_records(Checker &checker, const std::vector<const AssignmentSyntax *> &definitions,
                     const std::vector<NamedNode> &types, CompiledModule &result);
} // namespace clause::asn1::detail
