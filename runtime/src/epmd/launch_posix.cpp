#include "launch.hpp"
#include <array>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>

extern char **environ;

namespace clause::runtime::epmd {
namespace {
// Standard input, output and error of the started program on /dev/null.
bool null_actions(posix_spawn_file_actions_t &actions) {
    if (posix_spawn_file_actions_init(&actions) != 0) {
        return false;
    }
    int failed = posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0);
    failed |= posix_spawn_file_actions_addopen(&actions, 1, "/dev/null", O_WRONLY, 0);
    failed |= posix_spawn_file_actions_addopen(&actions, 2, "/dev/null", O_WRONLY, 0);
    if (failed != 0) {
        posix_spawn_file_actions_destroy(&actions);
        return false;
    }
    return true;
}
} // namespace

bool start_daemon(const std::string &program) {
    posix_spawn_file_actions_t actions{};
    if (!null_actions(actions)) {
        return false;
    }
    std::string path = program;
    std::string flag = "-daemon";
    std::array<char *, 3> argv{path.data(), flag.data(), nullptr};
    pid_t child = 0;
    const int failed = posix_spawn(&child, path.c_str(), &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    int status = 0;
    return failed == 0 && waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}
} // namespace clause::runtime::epmd
