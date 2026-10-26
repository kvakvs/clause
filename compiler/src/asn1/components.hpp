#pragma once
// Added for the ASN.1 compiler (plan 21 step LIB84): the components of SEQUENCE, SET and CHOICE types.
#include "checker_internal.hpp"

namespace clause::asn1::detail {
// Check the body of `syntax` into the components of node `id`: extension structure, COMPONENTS OF, automatic tags
// and DEFAULT values.
void check_components(Checker &checker, NodeId id, const TypeSyntax &syntax, const Scope &scope,
                      const Context &context);
} // namespace clause::asn1::detail
