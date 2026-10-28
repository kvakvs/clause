#include "engine.hpp"
#include <array>
#include <clause/abi/startup.hpp>

// The ssl runtime archive's extension entry (plan 21 step LIB81B): a program whose startup descriptor lists it links
// this archive, and startup registers the clause_ssl_engine builtins after the production ones.
namespace clause::runtime::builtins::ssl_engine {
namespace {
const std::array FAMILIES{context_builtins(), connection_builtins(), info_builtins()};

const RuntimeExtension EXTENSION{.name = "ssl", .families = FAMILIES};
} // namespace
} // namespace clause::runtime::builtins::ssl_engine

const void *CLAUSE_ssl_extension_v1() noexcept { return &clause::runtime::builtins::ssl_engine::EXTENSION; }
