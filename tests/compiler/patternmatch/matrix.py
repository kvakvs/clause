"""Select the optimization/specialization/driver matrix for full or fast test runs."""
import os

# Every documented policy as (optimization level, extra option, artifact directory name).
POLICIES = [("O0", "", "O0"), ("O0", "--no-type-specialization", "O0-off"),
            ("O2", "", "O2"), ("O2", "--no-type-specialization", "O2-off")]


def fast():
    """Development runs opt into CLAUSE_TEST_MODE=fast; unset or full keeps every combination."""
    mode = os.environ.get("CLAUSE_TEST_MODE", "full")
    if mode not in ("fast", "full"):
        raise SystemExit(f"CLAUSE_TEST_MODE must be fast or full, not {mode!r}")
    return mode == "fast"


def combinations():
    """Full: four policies with both drivers. Fast: O0 positional (generic code) and O2 project (inferred proofs and
    variants, step 59) still cover each axis."""
    full = [(level, extra, name, project) for level, extra, name in POLICIES for project in (False, True)]
    if not fast():
        return full
    print("fast mode: O0 positional and O2 project runs only; 'four policies' below means this subset")
    return [full[0], full[5]]


def option_lists():
    """Policy option lists paired with the driver, for compile-only inspection loops."""
    return [([f"-{level}"] + ([extra] if extra else []), project) for level, extra, _, project in combinations()]


def rejection_lists():
    """One policy with both drivers, for semantic rejection loops (plan 11 step VAL14): the driver reports these
    diagnostics in analyze() (compiler/src/driver/analysis.cpp) before code generation reads the policy."""
    return [(["-O0"], False), (["-O0"], True)]

