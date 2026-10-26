#pragma once
// Added for the ASN.1 compiler (plan 21 step LIB84): the Erlang module and .hrl file of a compiled ASN.1 module.
// The module's encode/2 and decode/2 run the runtime's codec over a schema term (docs/asn1.md#schema).
#include "model.hpp"

namespace clause::asn1 {
struct Generated {
    std::string erl_;
    // Empty when asn1ct would write no .hrl (no records and no values, or the maps option).
    std::string hrl_;
};

Generated generate(const CompiledModule &module, const CompileOptions &options);

// The schema term the runtime codec reads; its layout is documented in docs/asn1.md#schema.
Term schema(const CompiledModule &module, const CompileOptions &options);
} // namespace clause::asn1
