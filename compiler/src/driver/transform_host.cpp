// Added for the transform host (plan 21 LIB77A): the transform host of an invocation and its identity.
#include "transform_host.hpp"
#include "../cache/store.hpp"
#include "../linking/link.hpp"
#include "../linking/toolchain.hpp"
#include "library_modules.hpp"
#include "transforms/host.hpp"
#include <set>

namespace clause::cli {
namespace {
namespace fs = std::filesystem;

// Length of each hash in a host directory's name.
constexpr std::size_t NAME_DIGITS = 16;

// Where this clau's host lives: hashes of clau's path and of what the host is built from (clau, its runtime archive
// and its library).
transforms::HostPlace place() {
    const auto clau = cache::program_identity();
    const std::vector<fs::path> inputs{linking::runtime_library_path(std::nullopt), linking::library_directory(),
                                       linking::generated_library_directory()};
    cache::KeyBuilder key;
    key.add(CLAUSE_VERSION).add(clau).add(transforms::file_listing(inputs));
    return {.clau_ = cache::hash_text(clau.substr(0, clau.find('\n'))).substr(0, NAME_DIGITS),
            .identity_ = key.finish().substr(0, NAME_DIGITS)};
}

// The host of this invocation: the library's modules, and the executable once found or built.
class TransformHost final : public transforms::ClauseHost {
  public:
    TransformHost() {
        const auto library = load_library(linking::library_directory(), linking::generated_library_directory());
        for (const auto &name : library.provided_) {
            provided_.emplace(name.begin(), name.end()); // modules.terms names are ASCII atoms.
        }
    }

    bool provides(const std::u32string &module) const override { return provided_.contains(module); }

    fs::path executable() override {
        if (!program_) {
            program_ = transforms::ensure_host(place());
        }
        return *program_;
    }

  private:
    // The modules Clause's library provides; the executable, after the first request.
    std::set<std::u32string> provided_;
    std::optional<fs::path> program_;
};
} // namespace

std::shared_ptr<transforms::ClauseHost> transform_host() { return std::make_shared<TransformHost>(); }
} // namespace clause::cli
