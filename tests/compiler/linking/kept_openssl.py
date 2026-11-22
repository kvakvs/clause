"""OpenSSL builds the linking tests keep across runs (plan 11 step VAL16). A source build is minutes of a serial
nmake, and only a changed source tree or recipe changes it, so a test empties its work directory but for the kept
builds, which rebuild only when what they depend on changed:

- clau's own builds (--with-crypto=SOURCE) in <build-dir>/cache/openssl: clau keys each one on the tree's path and
  VERSION.dat and the recorded recipe; the kept directory adds clau's configure-and-build code (RECIPE_SOURCES), so a
  changed recipe is built, and tested, again.
- a test's own build (fips.py's FIPS provider), under a key of its own commands and code."""
import hashlib
import os
from pathlib import Path
import shutil
import stat
import time

# clau's source-build recipe: changing it discards the builds clau kept.
RECIPE_SOURCES = ("compiler/src/linking/openssl.cpp", "compiler/src/linking/openssl.hpp")
STAMP = ".clause-test-recipe"


def remove(path):
    """Remove a file or tree; an OpenSSL build leaves read-only files, which a plain rmtree keeps on Windows."""
    if path.is_dir() and not path.is_symlink():
        shutil.rmtree(path, onexc=lambda function, name, _: (os.chmod(name, stat.S_IWRITE), function(name)))
    elif path.exists():
        os.chmod(path, stat.S_IWRITE)
        path.unlink()


def fresh_work(work, kept):
    """Empty `work` but for the directory `kept` inside it (created when missing)."""
    work, kept = Path(work), Path(work) / kept
    directory = work
    for part in kept.relative_to(work).parts:
        if not directory.is_dir():
            break
        for entry in directory.iterdir():
            if entry.name != part:
                remove(entry)
        directory = directory / part
    kept.mkdir(parents=True, exist_ok=True)
    return kept


def key(*parts):
    """A short hex key of texts."""
    digest = hashlib.sha256()
    for part in parts:
        digest.update(part.encode("utf-8") + b"\0")
    return digest.hexdigest()[:16]


class ClauBuilds:
    """The OpenSSL builds clau keeps in a build directory's cache, kept by a test across its runs."""

    def __init__(self, root, work, build="build"):
        """Empty `work` but for <build>/cache/openssl; drop it when clau's recipe changed since it was built."""
        self.started_ = time.time()  # Builds finished after this were made by this run.
        self.directory_ = fresh_work(work, Path(build) / "cache" / "openssl")
        recipe = key(*((Path(root) / name).read_text(encoding="utf-8") for name in RECIPE_SOURCES))
        stamp = self.directory_ / STAMP
        if not stamp.exists() or stamp.read_text(encoding="utf-8") != recipe:
            for entry in self.directory_.iterdir():
                remove(entry)
            stamp.write_text(recipe, encoding="utf-8")

    def completed(self):
        """The completion marks of the builds clau finished, built now or kept."""
        return sorted(self.directory_.glob("*/install/.complete"))

    def built_now(self):
        """Whether this run built OpenSSL rather than reusing a kept build."""
        return any(mark.stat().st_mtime >= self.started_ for mark in self.completed())

    def prune(self):
        """After a passing run: keep only the installs this run used (a new build replaces older ones), without the
        intermediate build trees, which clau needs no more."""
        marks = self.completed()
        if self.built_now():
            for mark in marks:
                if mark.stat().st_mtime < self.started_:
                    remove(mark.parent.parent)
        for directory in self.directory_.glob("openssl-*"):
            if (directory / "install" / ".complete").exists():
                remove(directory / "build")
            else:
                remove(directory)  # An interrupted build.
