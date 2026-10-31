#include "bundled.hpp"
#include <clause/abi/startup.hpp>

// The epmd runtime archive's extension entry (plan 30 step DIST3d, docs/distribution.md#epmd): a program whose startup
// descriptor lists it carries the bundled epmd; it adds no builtins.
namespace clause::runtime::epmd {
namespace {
std::unique_ptr<BundledEpmd> make_bundled() { return std::make_unique<Bundled>(); }

const RuntimeExtension EXTENSION{.name = "epmd", .families = {}, .bundled_epmd = make_bundled};
} // namespace
} // namespace clause::runtime::epmd

const void *CLAUSE_epmd_extension_v1() noexcept { return &clause::runtime::epmd::EXTENSION; }
