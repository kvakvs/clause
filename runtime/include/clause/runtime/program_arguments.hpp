#pragma once
#include <optional>
#include <string>
#include <vector>

namespace clause::runtime {
// The command line and home directory a program started with, decoded once at startup, for the entry's argv and
// the init module's answers (plan step 41L, docs/builtins.md#init-module).
struct ProgramArguments {
    // UTF-8 arguments after the runtime options: the arity-1 entry's argv and init:get_plain_arguments/0.
    std::vector<std::string> plain = {};
    // UTF-8 absolute path of the running executable (init:script_name/0); empty when the host cannot tell.
    std::string executable = {};
    // UTF-8 home directory at startup, init's `home` flag; none when the host defines none.
    std::optional<std::string> home = {};
};
} // namespace clause::runtime
