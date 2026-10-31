#pragma once
#include <optional>
#include <string>
#include <vector>

namespace clause::runtime {
// A flag init reports for a runtime option, under OTP's name with its values (`kernel` with ["net_ticktime", "20"]).
// Added for node start options (plan 30 step DIST4a1).
struct ProgramFlag {
    std::string name;
    std::vector<std::string> values;
};

// The command line and home directory a program started with, decoded once at startup, for the entry's argv and
// the init module's answers (plan step 41L, docs/builtins.md#init-module).
struct ProgramArguments {
    // UTF-8 arguments after the runtime options: the arity-1 entry's argv and init:get_plain_arguments/0.
    std::vector<std::string> plain = {};
    // UTF-8 absolute path of the running executable (init:script_name/0); empty when the host cannot tell.
    std::string executable = {};
    // UTF-8 home directory at startup, init's `home` flag; none when the host defines none.
    std::optional<std::string> home = {};
    // init's flags after `home`: the node start options and --env in the order they were given, the later of two
    // same single-valued options replacing the earlier (docs/distribution.md#node-start-options). Added for DIST4a1.
    std::vector<ProgramFlag> flags = {};
};
} // namespace clause::runtime
