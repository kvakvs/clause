#include "daemon.hpp"
#include <csignal>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace clause::runtime::epmd {
namespace {
// Fork; the parent exits at once with status 0, as OTP's epmd's parents do. False when fork fails.
bool continue_in_child() {
    const pid_t child = ::fork();
    if (child > 0) {
        ::_exit(0);
    }
    return child == 0;
}
} // namespace

void null_standard_io() {
    const int null = ::open("/dev/null", O_RDWR);
    if (null < 0) {
        return;
    }
    for (int descriptor = 0; descriptor < 3; ++descriptor) {
        ::dup2(null, descriptor);
    }
    if (null > 2) {
        ::close(null);
    }
}

Detached detach() {
    // The first child is no process group leader, so it can start a session; the second is no session leader, so it
    // never gains a controlling terminal.
    if (!continue_in_child() || ::setsid() < 0) {
        return Detached::failed;
    }
    static_cast<void>(std::signal(SIGHUP, SIG_IGN));
    if (!continue_in_child() || ::chdir("/") != 0) {
        return Detached::failed;
    }
    ::umask(0);
    null_standard_io();
    return Detached::serve;
}
} // namespace clause::runtime::epmd
