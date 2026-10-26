#pragma once
// Added for public_key's PEM text (plan 21 step LIB80E1): base64's encoders for other builtins, with the results and
// errors of the base64 builtins.
#include "../support.hpp"

namespace clause::runtime::builtins::encoding {
// base64:encode/1 of a binary or byte list.
Term base64_encode(ProcessContext &context, const Term &data);
// base64:mime_decode/1 of a binary or byte list.
Term base64_mime_decode(ProcessContext &context, const Term &data);
} // namespace clause::runtime::builtins::encoding
