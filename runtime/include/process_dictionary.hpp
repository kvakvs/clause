#pragma once

// The process dictionary (plan step 41C, docs/processes.md#process-dictionary): key/value words of the owner's
// heap, collection roots until erased or the process ends. Lookup by exact equality lives with the builtins.
#include "terms.hpp"

#include <cstddef>
#include <vector>

namespace clause::runtime {
class ProcessDictionary final {
  public:
    struct Entry {
        // Words of terms on the owner's heap; the collector rewrites them in place.
        Word key;
        Word value;
    };

    ProcessDictionary() = default;
    ProcessDictionary(const ProcessDictionary &) = delete;
    ProcessDictionary &operator=(const ProcessDictionary &) = delete;
    ProcessDictionary(ProcessDictionary &&) = delete;
    ProcessDictionary &operator=(ProcessDictionary &&) = delete;
    ~ProcessDictionary() = default;

    // The entries in insertion order; erasing one moves the last entry into its place.
    std::vector<Entry> &entries() noexcept { return entries_; }

    // Visit every key and value word as a root.
    template <typename Visitor> void visit(Visitor &&visit) {
        for (auto &entry : entries_) {
            visit(entry.key);
            visit(entry.value);
        }
    }

  private:
    // One entry per distinct key (exact equality).
    std::vector<Entry> entries_;
};
} // namespace clause::runtime
