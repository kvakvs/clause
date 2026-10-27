#include "spawn.hpp"
#include <array>
#include <cerrno>
#include <cstring>
#include <map>

#include <fcntl.h>
#include <spawn.h>
#include <unistd.h>

extern char **environ;

// Spawned programs on Linux and macOS (docs/ports.md#drivers): posix_spawn with pipes for stdin and stdout;
// {spawn, Command} runs `/bin/sh -c Command`, as OTP does.
namespace clause::runtime::detail {
namespace {
// The POSIX reason of an error number from posix_spawn or pipe.
std::string reason(int error) {
    switch (error) {
    case ENOENT:
    case ENOTDIR:
        return "enoent";
    case EACCES:
    case EPERM:
        return "eacces";
    case ENOEXEC:
        return "enoexec";
    case EMFILE:
    case ENFILE:
        return "emfile";
    default:
        return "einval";
    }
}

// A pipe with close-on-exec ends; the child gets its end through dup2.
std::optional<std::array<int, 2>> make_pipe() {
    std::array<int, 2> ends{-1, -1};
    if (pipe(ends.data()) != 0) {
        return std::nullopt;
    }
    for (const auto fd : ends) {
        fcntl(fd, F_SETFD, FD_CLOEXEC);
    }
    return ends;
}

// The environment with the {env, ...} changes applied, as NAME=VALUE strings.
std::vector<std::string> environment(const PortOptions &options) {
    std::map<std::string, std::string> variables;
    for (auto **entry = environ; *entry != nullptr; ++entry) {
        const std::string text(*entry);
        const auto separator = text.find('=');
        variables[text.substr(0, separator)] = separator == std::string::npos ? "" : text.substr(separator + 1);
    }
    for (const auto &[name, value] : options.env) {
        if (value) {
            variables[name] = *value;
        } else {
            variables.erase(name);
        }
    }
    std::vector<std::string> result;
    result.reserve(variables.size());
    for (const auto &[name, value] : variables) {
        auto &entry = result.emplace_back(name);
        entry += '=';
        entry += value;
    }
    return result;
}

// A null-terminated array of pointers into `strings`, as execve takes.
std::vector<char *> pointers(std::vector<std::string> &strings) {
    std::vector<char *> result;
    result.reserve(strings.size() + 1);
    for (auto &text : strings) {
        result.push_back(text.data());
    }
    result.push_back(nullptr);
    return result;
}

// The program and argv of a request: /bin/sh -c Command, or File with argv[0] and the {args, ...}.
std::pair<std::string, std::vector<std::string>> program(const SpawnRequest &request, const PortOptions &options) {
    if (!request.executable) {
        return {"/bin/sh", {"/bin/sh", "-c", request.command}};
    }
    std::vector<std::string> argv{options.arg0.value_or(request.command)};
    argv.insert(argv.end(), options.args.begin(), options.args.end());
    return {request.command, argv};
}

// The child's stdin and stdout: pipe ends for the port's directions, /dev/null for the others.
struct Standard {
    std::optional<std::array<int, 2>> input;
    std::optional<std::array<int, 2>> output;

    // Close the child's ends in the parent and give the parent's: the stdin pipe's write end and the stdout pipe's
    // read end.
    Spawned parent_ends(pid_t pid) const noexcept {
        if (input) {
            close((*input)[0]);
        }
        if (output) {
            close((*output)[1]);
        }
        return {.input = output ? (*output)[0] : -1, .output = input ? (*input)[1] : -1, .child = pid, .os_pid = pid};
    }

    // Close every descriptor still open in the parent after a failure.
    void close_all() const noexcept {
        for (const auto &ends : {input, output}) {
            if (ends) {
                close((*ends)[0]);
                close((*ends)[1]);
            }
        }
    }
};

// Queue the child's {cd, Dir}; macOS 26 replaced the _np call with the POSIX 2024 name.
int add_chdir(posix_spawn_file_actions_t &actions, const char *directory) {
#if defined(__APPLE__) && defined(__MAC_26_0) && __MAC_OS_X_VERSION_MIN_REQUIRED >= __MAC_26_0
    return posix_spawn_file_actions_addchdir(&actions, directory);
#else
    return posix_spawn_file_actions_addchdir_np(&actions, directory);
#endif
}

// Wire the child's standard descriptors through file actions.
int file_actions(posix_spawn_file_actions_t &actions, const Standard &handles, const PortOptions &options) {
    int failed = posix_spawn_file_actions_init(&actions);
    if (handles.input) {
        failed |= posix_spawn_file_actions_adddup2(&actions, (*handles.input)[0], 0);
    } else {
        failed |= posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0);
    }
    if (handles.output) {
        failed |= posix_spawn_file_actions_adddup2(&actions, (*handles.output)[1], 1);
    } else {
        failed |= posix_spawn_file_actions_addopen(&actions, 1, "/dev/null", O_WRONLY, 0);
    }
    if (options.stderr_to_stdout) {
        failed |= posix_spawn_file_actions_adddup2(&actions, 1, 2);
    }
    if (options.cd) {
        failed |= add_chdir(actions, options.cd->c_str());
    }
    return failed;
}

// Start the program of `request` with the child's standard descriptors wired to `handles`; its pid.
std::expected<pid_t, DriverError> start(const SpawnRequest &request, const PortOptions &options,
                                        const Standard &handles) {
    posix_spawn_file_actions_t actions;
    auto [path, argv] = program(request, options);
    auto env = environment(options);
    auto arguments = pointers(argv);
    auto variables = pointers(env);
    pid_t pid = 0;
    int error = file_actions(actions, handles, options) != 0 ? EINVAL : 0;
    if (error == 0) {
        error = posix_spawn(&pid, path.c_str(), &actions, nullptr, arguments.data(), variables.data());
    }
    posix_spawn_file_actions_destroy(&actions);
    if (error != 0) {
        return std::unexpected(DriverError{reason(error)});
    }
    return pid;
}
} // namespace

std::expected<Spawned, DriverError> spawn_program(const SpawnRequest &request, const PortOptions &options) {
    Standard handles{options.output ? make_pipe() : std::nullopt, options.input ? make_pipe() : std::nullopt};
    if ((options.output && !handles.input) || (options.input && !handles.output)) {
        handles.close_all();
        return std::unexpected(DriverError{"emfile"});
    }
    const auto pid = start(request, options, handles);
    if (!pid) {
        handles.close_all();
        return std::unexpected(pid.error());
    }
    return handles.parent_ends(*pid);
}
} // namespace clause::runtime::detail
