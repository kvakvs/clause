"""Generates runtime/src/builtins/rand/tables.cpp (plan 21 step LIB6, docs/builtins.md#random-numbers).

Input: the host OTP's rand observed by observe.escript. The ziggurat's strip bounds and widths are taken as observed;
the strip heights f are computed at high precision from the ziggurat's defining constants (OTP's slow path compares
with them, so they matter only within a unit in the last place). Each jump is linear over GF(2) in the generator
state: OTP's jumped states for two seeds and this module's own simulation of each jump procedure give the jump
polynomial (solved on one seed, checked on the other). Run once per OTP change of these algorithms and commit the
output; normal builds never run it.

  escript tools/rand/observe.escript OBSERVED
  python tools/rand/generate.py OBSERVED
"""
import decimal
import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
OUTPUT = ROOT / 'runtime/src/builtins/rand/tables.cpp'
M58 = (1 << 58) - 1
M64 = (1 << 64) - 1

# The ziggurat's base strip edge and strip area for 256 strips (Marsaglia and Tsang).
ZIGGURAT_R = '3.6541528853610087963519472518'
ZIGGURAT_V = '4.928673233974655e-3'


def bsl(bits, x, n):
    """(x masked to bits - n) shifted left by n."""
    return (x & ((1 << (bits - n)) - 1)) << n


def rotl58(x, n):
    return bsl(58, x, n) | (x >> (58 - n))


def exsp_next(state):
    s1, s0 = state
    s0_1 = s0 & M58
    s1_b = (s1 & M58) ^ bsl(58, s1, 24)
    return [s0_1, s1_b ^ s0_1 ^ (s1_b >> 11) ^ (s0_1 >> 41)]


def exrop_next(state):
    s0, s1 = state
    s1_a = s1 ^ s0
    return [rotl58(s0, 24) ^ s1_a ^ bsl(58, s1_a, 2), rotl58(s1_a, 35)]


def exs1024_next(state):
    left, right = state
    if len(left) == 1:
        left, right = left + right[::-1], []
    s0, s1 = left[0], left[1]
    s11 = s1 ^ bsl(64, s1, 31)
    s12 = s11 ^ (s11 >> 11)
    s01 = s0 ^ (s0 >> 30)
    return [[s01 ^ s12] + left[2:], [s0] + right]


def exro928_next(state):
    left, right = state
    if len(left) == 1:
        rest = right[::-1]
        s15, s0, ss, rs = left[0], rest[0], rest[1:], []
    else:
        s15, s0, ss, rs = left[0], left[1], left[2:], right
    s0_1 = s0 & M58
    q = (s15 & M58) ^ s0_1
    return [[rotl58(q, 45)] + ss, [rotl58(s0_1, 44) ^ q ^ bsl(58, q, 9)] + rs]


def ring(state):
    """A two-list state's words in ring order: the list, then the reversed list."""
    left, right = state
    return left + right[::-1]


# Per algorithm: jump steps, generator step, the words a step contributes, the word width, and how the jumped state
# is laid out from the accumulated words.
def exs1024_layout(before, words):
    split = 16 - len(before[1])
    return [words[:split], words[split:][::-1]]


ALGORITHMS = {
    'exsplus': (116, exsp_next, list, 58, lambda before, words: words),
    'exrop': (116, exrop_next, list, 58, lambda before, words: words),
    'exs1024': (1024, exs1024_next, ring, 64, exs1024_layout),
    'exro928ss': (928, exro928_next, ring, 58, lambda before, words: [words, []]),
    'exro928_2pow20': (928, exro928_next, ring, 58, lambda before, words: [words, []]),
}


def flatten(words, width):
    value = 0
    for word in words:
        value = (value << width) | word
    return value


def contributions(name, state):
    """The state contribution of each jump step, as one integer of bits."""
    steps, step, words, width, _ = ALGORITHMS[name]
    result = []
    for _ in range(steps):
        result.append(flatten(words(state), width))
        state = step(state)
    return result


def unlayout(name, before, after):
    """The accumulated words of an observed jumped state."""
    if name == 'exs1024':
        return after[0] + after[1][::-1]
    if name.startswith('exro928'):
        return after[0]
    return after


def solve(name, before, after):
    """The jump polynomial's coefficients as a bit mask over the steps."""
    width = ALGORITHMS[name][3]
    target = flatten(unlayout(name, before, after), width)
    basis = {}
    for index, vector in enumerate(contributions(name, before)):
        combination = 1 << index
        while vector:
            top = vector.bit_length() - 1
            if top not in basis:
                basis[top] = (vector, combination)
                break
            vector ^= basis[top][0]
            combination ^= basis[top][1]
        else:
            sys.exit(f'{name}: dependent jump steps')
    solution = 0
    while target:
        top = target.bit_length() - 1
        if top not in basis:
            sys.exit(f'{name}: jumped state outside the steps span')
        target ^= basis[top][0]
        solution ^= basis[top][1]
    return solution


def jumped(name, before, mask):
    """This module's simulation of OTP's jump with the solved polynomial."""
    steps, step, words, width, layout = ALGORITHMS[name]
    accumulated = [0] * len(words(before))
    state = before
    for index in range(steps):
        if mask >> index & 1:
            accumulated = [a ^ w for a, w in zip(accumulated, words(state))]
        state = step(state)
    return layout(before, accumulated)


def heights():
    """The strip heights f[i] = exp(-x[i]^2 / 2) of the ziggurat, f[0] = 1."""
    decimal.getcontext().prec = 40
    d = decimal.Decimal
    edge = d(ZIGGURAT_R)
    area = d(ZIGGURAT_V)
    f = [d(0)] * 256
    f[0] = d(1)
    f[255] = (-edge * edge / 2).exp()
    for i in range(254, 0, -1):
        edge = (-2 * (area / edge + (-edge * edge / 2).exp()).ln()).sqrt()
        f[i] = (-edge * edge / 2).exp()
    return [float(v) for v in f]


def words_of(mask, steps):
    return [(mask >> (64 * i)) & M64 for i in range((steps + 63) // 64)]


def cpp_words(values):
    lines = []
    for start in range(0, len(values), 4):
        lines.append('    ' + ', '.join(f'0x{v:016x}U' for v in values[start:start + 4]) + ',')
    return '\n'.join(lines)


def main():
    observed = json.loads(pathlib.Path(sys.argv[1]).read_text(encoding='utf-8'))
    masks = {}
    for name, pairs in observed['jumps'].items():
        (before, after), (check_before, check_after) = pairs
        masks[name] = solve(name, before, after)
        if jumped(name, check_before, masks[name]) != check_after:
            sys.exit(f'{name}: the solved jump does not reproduce the second observation')
    strips = observed['ziggurat']
    f = heights()
    out = ['// Generated by tools/rand/generate.py from the host OTP observed by tools/rand/observe.escript; do not edit.',
           '// The tables of plan 21 step LIB6 (docs/builtins.md#random-numbers).',
           '#include "tables.hpp"', '', 'namespace clause::runtime::builtins::rand {',
           'const std::array<Strip, 256> STRIPS{{']
    for (bound, width), height in zip(strips, f):
        out.append(f'    {{{bound}U, {float(width).hex()}, {height.hex()}}},')
    out.append('}};')
    for name, steps in [('exsplus', 116), ('exrop', 116), ('exs1024', 1024), ('exro928ss', 928),
                        ('exro928_2pow20', 928)]:
        values = words_of(masks[name], steps)
        out.append('')
        out.append(f'const std::array<std::uint64_t, {len(values)}> {name.upper()}_JUMP{{')
        out.append(cpp_words(values))
        out.append('};')
    out.append('} // namespace clause::runtime::builtins::rand')
    OUTPUT.write_bytes(('\n'.join(out) + '\n').encode())
    print(f'{OUTPUT}: 256 strips, {len(masks)} jumps')


if __name__ == '__main__':
    main()
