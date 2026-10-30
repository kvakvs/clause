#include "server.hpp"
#include <boost/asio/io_context.hpp>
#include <cstdio>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// clause-epmd (docs/distribution.md#epmd): Clause's epmd as a program of its own, running in the foreground. Created
// for plan 30 step DIST3a, where the multi-node test runner starts it; -daemon, -names and -kill come later.
namespace {
using clause::runtime::epmd::ServerOptions;

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

// Apply the flag at `at` (with its value); false when it is not one of the flags or lacks its value.
bool apply_flag(std::span<char *> args, std::size_t &at, ServerOptions &options) {
    const std::string_view flag = args[at];
    if (flag == "-relaxed_command_check") {
        options.relaxed_ = true;
        return true;
    }
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

// The options of the environment, then of the command line; a message when they are invalid.
std::optional<ServerOptions> parse(std::span<char *> args) {
    auto options = clause::runtime::epmd::environment_options();
    if (!options) {
        std::cerr << "clause-epmd: " << options.error() << '\n';
        return std::nullopt;
    }
    for (std::size_t at = 1; at < args.size(); ++at) {
        if (!apply_flag(args, at, *options)) {
            std::cerr << "clause-epmd: invalid argument '" << args[at]
                      << "'\nusage: clause-epmd [-port No] [-address Addresses] [-relaxed_command_check]\n";
            return std::nullopt;
        }
    }
    return *options;
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
} // namespace

int main(int argc, char **argv) {
    try {
        const auto options = parse(std::span(argv, static_cast<std::size_t>(argc)));
        return options ? serve(*options) : 1;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "clause-epmd: %s\n", error.what());
        return 1;
    } catch (...) {
        std::fputs("clause-epmd: unexpected failure\n", stderr);
        return 1;
    }
}
