#include "detach.hpp"
#include <fcntl.h>
#include <unistd.h>

namespace clause::runtime::detail {
namespace {
// Fork; the parent exits at once with status 0, as erlexec's parents do. False when fork fails.
bool continue_in_child() {
    const pid_t child = ::fork();
    if (child > 0) {
        ::_exit(0);
    }
    return child == 0;
}

// Standard input, output and error on /dev/null.
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
} // namespace

Detached detach() {
    // The first child is no process group leader, so it can start a session; the second is no session leader, so it
    // never gains a controlling terminal. The working directory stays, as erlexec leaves it.
    if (!continue_in_child() || ::setsid() < 0 || !continue_in_child()) {
        return Detached::failed;
    }
    null_standard_io();
    return Detached::run;
}
} // namespace clause::runtime::detail
