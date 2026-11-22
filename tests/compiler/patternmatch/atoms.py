"""Execute locally authored atom and boolean fragments through the public CLI and native runtime."""
import pathlib
import sys
from stored import load
from evidence import run
from matrix import combinations


def main():
    """Compare all four optimization/specialization modes and both source entry paths with stored OTP results."""
    tool, cmake, root, directory, settings, config, suffix = sys.argv[1:]
    source, work = pathlib.Path(root), pathlib.Path(directory)
    work.mkdir(parents=True, exist_ok=True)
    load(source, "atoms", work)
    for level, extra, _, project in combinations():
        name = level + ("-off" if extra else "-on")
        run([cmake, f"-DTOOL={tool}", f"-DOPTIMIZATION={level}", f"-DEXTRA_OPTIONS={extra}",
             f"-DPROJECT_MODE={project}", f"-DSOURCE_ROOT={source.as_posix()}",
             f"-DTEST_DIR={(work / name).as_posix()}", f"-DINPUT_ROOT={work.as_posix()}",
             f"-DHOST_SETTINGS={settings}", f"-DHOST_CONFIG={config}", f"-DHOST_SUFFIX={suffix}",
             "-P", str(source / "tests/compiler/codegen/atoms.cmake")])
    print("OTP atom spellings/booleans match all four native policies; ownership and limits pass.")


if __name__ == "__main__":
    main()
