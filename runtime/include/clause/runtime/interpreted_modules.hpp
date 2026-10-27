#pragma once
#include "base_types.hpp"
#include <atomic>
#include <cstddef>
#include <map>
#include <optional>
#include <set>
#include <shared_mutex>
#include <string>
#include <utility>
#include <vector>

namespace clause::runtime {
// Modules clause_interp runs from their forms (plan 21 LIB77B, docs/libraries/library.md#module-interpreter): calls
// of the program's code into them enter one route frame, error_handler:undefined_function/3, and code queries and
// function_exported/3 see them as loaded. Atom words are the keys: they stay valid for the runtime's lifetime.
class InterpretedModules final {
  public:
    // An exported function: its name's atom word and its arity.
    using Export = std::pair<Word, std::size_t>;

    // What a code query answers for one interpreted module.
    struct Module final {
        // The file code:which/1 and code:is_loaded/1 name, as code points.
        std::u32string file;
        // Its exports as {Function atom word, Arity}, module_info/0,1 among them.
        std::set<Export> exports;
    };

    // Interpret `module` from now on, replacing an earlier entry; `route` is the frame calls into it enter.
    void add(Word module, Module entry, const void *route);
    // Stop interpreting `module`; false when it was not interpreted.
    bool remove(Word module);
    // Route every call of an undefined function, not only those of interpreted modules, so the interpreter can load
    // their modules from .beam files (plan 21 LIB77C).
    void route_undefined(const void *route);
    // The route frame of a call into `module` that nothing in the program answers, or null when it is not
    // interpreted (and undefined calls are not routed).
    const void *route(Word module) const noexcept;
    // Whether `module` is interpreted.
    bool contains(Word module) const noexcept;
    // Whether interpreted `module` exports `function`.
    bool exported(Word module, const Export &function) const noexcept;
    // The file of interpreted `module`; none when it is not interpreted.
    std::optional<std::u32string> file(Word module) const;
    // Every interpreted module with its file, in atom word order.
    std::vector<std::pair<Word, std::u32string>> modules() const;

  private:
    // Guards modules_ and route_: shared for queries, exclusive for changes.
    mutable std::shared_mutex mutex_;
    // Set while any module is interpreted or undefined calls are routed, so the calls and queries of programs
    // without any skip the lock.
    std::atomic<bool> any_{false};
    // Every undefined call takes the route (route_undefined).
    bool all_ = false;
    // The interpreted modules by module atom word.
    std::map<Word, Module> modules_;
    // error_handler:undefined_function/3's frame, as the last registration gave it.
    const void *route_ = nullptr;
};
} // namespace clause::runtime
