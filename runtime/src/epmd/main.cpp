#include "client.hpp"
#include "daemon.hpp"
#include "server.hpp"
#include <boost/asio/io_context.hpp>
#include <cstdio>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// clause-epmd (docs/distribution.md#epmd): Clause's epmd as a program of its own. Created for plan 30 step DIST3a,
// where the multi-node test runner starts it in the foreground; -daemon, -names and -kill added in DIST3c, when clau
// writes it beside programs.
namespace {
using clause::runtime::epmd::ServerOptions;

// What the command line asks for.
enum class Action : std::uint8_t { serve, names, kill };

struct Command {
    // The epmd's settings, also the port -names and -kill ask.
    ServerOptions options_;
    Action action_ = Action::serve;
    // -daemon: detach, then serve.
    bool daemon_ = false;
    // -detached: the process a Windows -daemon started, which serves with standard I/O on NUL.
    bool detached_ = false;
};

// Split a comma-separated address list.
std::vector<std::string> address_list(std::string_view text) {
    std::vector<std::string> addresses;
    while (!text.empty()) {
        const auto comma = text.find(',');
        if (const auto part = text.substr(0, comma); !part.empty()) {
            addresses.emplace_back(part);
        }
        text = comma == std::string_view::npos ? std::string_view{} : text.substr(comma + 1);
    }
    return addresses;
}

// Apply a flag without value; false when it is none of them.
bool apply_switch(std::string_view flag, Command &command) {
    if (flag == "-relaxed_command_check") {
        command.options_.relaxed_ = true;
    } else if (flag == "-daemon") {
        command.daemon_ = true;
    } else if (flag == "-detached") {
        command.detached_ = true;
    } else {
        return false;
    }
    return true;
}

// Apply the flag at `at` with its value; false when it is not one of them or lacks its value.
bool apply_value(std::span<char *> args, std::size_t &at, ServerOptions &options) {
    const std::string_view flag = args[at];
    if ((flag != "-port" && flag != "-address") || at + 1 >= args.size()) {
        return false;
    }
    const std::string_view value = args[++at];
    if (flag == "-address") {
        options.addresses_ = address_list(value);
        return true;
    }
    const auto port = clause::runtime::epmd::port_number(value);
    options.port_ = port.value_or(options.port_);
    return port.has_value();
}

// The question of -names or -kill, which OTP's epmd takes only as the last argument (after -port).
std::optional<Action> question(std::string_view flag) {
    if (flag == "-names") {
        return Action::names;
    }
    if (flag == "-kill") {
        return Action::kill;
    }
    return std::nullopt;
}

// The command of the environment, then of the command line; a message when it is invalid.
std::optional<Command> parse(std::span<char *> args) {
    auto options = clause::runtime::epmd::environment_options();
    if (!options) {
        std::cerr << "clause-epmd: " << options.error() << '\n';
        return std::nullopt;
    }
    Command command{.options_ = *options};
    for (std::size_t at = 1; at < args.size(); ++at) {
        if (const auto action = question(args[at]); action && at + 1 == args.size()) {
            command.action_ = *action;
            return command;
        }
        if (!apply_switch(args[at], command) && !apply_value(args, at, command.options_)) {
            std::cerr << "clause-epmd: invalid argument '" << args[at]
                      << "'\nusage: clause-epmd [-address Addresses] [-port No] [-daemon] [-relaxed_command_check]\n"
                         "       clause-epmd [-port No] [-names|-kill]\n";
            return std::nullopt;
        }
    }
    return command;
}

// Serve until a KILL request; the exit status.
int serve(const ServerOptions &options) {
    boost::asio::io_context io(1);
    auto server = clause::runtime::epmd::Server::open(io, options, [&io] { io.stop(); });
    if (!server) {
        std::cerr << "clause-epmd: " << server.error().message_ << '\n';
        return 1;
    }
    io.run();
    return 0;
}

// Detach for -daemon, then serve; the exit status.
int serve_detached(const ServerOptions &options) {
    using clause::runtime::epmd::Detached;
    switch (clause::runtime::epmd::detach()) {
    case Detached::started:
        return 0;
    case Detached::failed:
        std::cerr << "clause-epmd: cannot start detached\n";
        return 1;
    case Detached::serve:
        break;
    }
    return serve(options);
}

// Carry out a parsed command; the exit status.
int run(const Command &command) {
    if (command.action_ == Action::names) {
        return clause::runtime::epmd::print_names(command.options_.port_);
    }
    if (command.action_ == Action::kill) {
        return clause::runtime::epmd::kill_epmd(command.options_.port_);
    }
    if (command.detached_) {
        clause::runtime::epmd::null_standard_io();
        return serve(command.options_);
    }
    return command.daemon_ ? serve_detached(command.options_) : serve(command.options_);
}
} // namespace

int main(int argc, char **argv) {
    try {
        const auto command = parse(std::span(argv, static_cast<std::size_t>(argc)));
        return command ? run(*command) : 1;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "clause-epmd: %s\n", error.what());
        return 1;
    } catch (...) {
        std::fputs("clause-epmd: unexpected failure\n", stderr);
        return 1;
    }
}
