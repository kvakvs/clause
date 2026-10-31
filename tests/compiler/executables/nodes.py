"""Multi-node golden runs (docs/distribution.md#testing, plan 30 step DIST3a): a run listing 'nodes' gets its own
epmd on a free port and starts every node at once with ERL_EPMD_PORT naming it. Shared by run.py, which starts
clause-epmd (or OTP's epmd for runs of 'epmd' otp, and none for runs of 'epmd' bundled, whose Clause nodes start the
bundled epmd), and regenerate.py, which runs every node under the OTP oracle on OTP's epmd."""
import concurrent.futures
import contextlib
import os
import pathlib
import shutil
import socket
import stat
import subprocess
import time

# Seconds an epmd may take to accept connections after it starts.
EPMD_START_TIMEOUT = 10
# Attempts to start an epmd: another process may take the free port before the epmd binds it.
EPMD_ATTEMPTS = 3
# Fields of a node the oracle observes; the others are authored.
GENERATED_NODE_FIELDS = ('exit_status', 'stdout')


def multi(run):
    """Whether a golden run is a multi-node run."""
    return 'nodes' in run


def bundled(run):
    """Whether a multi-node run starts no epmd: its Clause nodes start the bundled one (plan 30 step DIST3b)."""
    return run.get('epmd') == 'bundled'


def needs_otp(run):
    """Whether a multi-node run needs a host OTP to run here: OTP's epmd or an otp node."""
    return run.get('epmd') == 'otp' or any(node.get('kind') == 'otp' for node in run['nodes'])


def otp_program(escript, name):
    """A program of the OTP installation of `escript`, such as epmd: beside it, in the installation's erts-*/bin
    (OTP's top-level bin holds no epmd on Windows), else on PATH."""
    path = pathlib.Path(escript).resolve()
    program = name + path.suffix
    candidates = [path.with_name(program), *sorted(path.parent.parent.glob(f'erts-*/bin/{program}'), reverse=True)]
    found = next((candidate for candidate in candidates if candidate.exists()), None) or shutil.which(name)
    if not found:
        raise RuntimeError(f'no {name} in the OTP installation of {escript}')
    return str(found)


def remove_tree(path):
    """Removes a directory tree that may hold read-only files, such as a run's cookie file (plan 30 step DIST4b1c)."""
    if pathlib.Path(path).exists():
        shutil.rmtree(path, onexc=lambda remove, name, _: (os.chmod(name, stat.S_IWRITE), remove(name)))


def home_environment(path):
    """Environment entries that make the fresh, empty directory `path` a program's home directory, where the cookie
    file is (plan 30 step DIST4b1c): HOME, and on Windows HOMEDRIVE and HOMEPATH, which erl and Clause read first."""
    path = pathlib.Path(path).resolve()
    remove_tree(path)
    path.mkdir(parents=True)
    result = {'HOME': str(path)}
    if path.drive:
        result |= {'HOMEDRIVE': path.drive, 'HOMEPATH': str(path)[len(path.drive):]}
    return result


def node_home(node, directory):
    """A node's home environment: a fresh directory below `directory` for a node of 'home' true, else nothing."""
    return home_environment(pathlib.Path(directory) / f'home-{node["name"]}') if node.get('home') else {}


def free_port():
    """A TCP port nothing listens on now."""
    with socket.socket() as probe:
        probe.bind(('127.0.0.1', 0))
        return probe.getsockname()[1]


def listening(port):
    """Whether something accepts connections on the loopback port."""
    with contextlib.suppress(OSError), socket.create_connection(('127.0.0.1', port), timeout=1):
        return True
    return False


def start_epmd(command):
    """Starts `command` plus '-port N' in the foreground on a free port; returns the process and the port."""
    for _ in range(EPMD_ATTEMPTS):
        port = free_port()
        process = subprocess.Popen([*command, '-port', str(port)], stdin=subprocess.DEVNULL,
                                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        deadline = time.monotonic() + EPMD_START_TIMEOUT
        while process.poll() is None and time.monotonic() < deadline:
            if listening(port):
                return process, port
            time.sleep(0.05)
        stop(process)
    raise RuntimeError(f'epmd {command} did not start')


def stop(process):
    """Ends a process that may have exited already (a node may KILL its epmd)."""
    if process.poll() is None:
        process.kill()
    process.wait()


def run_nodes(commands, environments, cwd, timeout):
    """Runs every node command at once; returns (exit status, stdout, stderr) per node, or None for one that timed
    out, in which case every node still running is killed."""
    processes = [subprocess.Popen(command, cwd=cwd, env=environment, stdin=subprocess.DEVNULL,
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                 for command, environment in zip(commands, environments)]
    deadline = time.monotonic() + timeout

    def collect(process):
        # Each node's pipes are read on a thread of their own, so a talkative node never blocks on a full pipe.
        try:
            stdout, stderr = process.communicate(timeout=max(0, deadline - time.monotonic()))
            return process.returncode, stdout, stderr
        except subprocess.TimeoutExpired:
            for other in processes:
                stop(other)
            return None

    with concurrent.futures.ThreadPoolExecutor(max_workers=len(processes)) as pool:
        results = list(pool.map(collect, processes))
    for process in processes:
        stop(process)
    return results
