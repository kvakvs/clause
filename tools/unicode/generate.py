"""Generates runtime/src/builtins/unicode/tables.cpp (plan step 41O1, docs/builtins.md#unicode-util).

Inputs: the host OTP's unicode_util observed by observe.escript (case, category, decomposition, width and identifier
answers per code point), and the Unicode Character Database files OTP 29 pins (references/otp/lib/stdlib/uc_spec):
grapheme break, emoji, Indic syllabic and composition data, which unicode_util does not export. Run once per Unicode
update and commit the output; normal builds never run it.

  escript tools/unicode/observe.escript OBSERVED
  python tools/unicode/generate.py OBSERVED references/otp/lib/stdlib/uc_spec
"""
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
OUTPUT = ROOT / 'runtime/src/builtins/unicode/tables.cpp'

# OTP's categories in the order of unicode_tables.hpp's CATEGORIES.
CATEGORIES = [
    'letter/uppercase', 'letter/lowercase', 'letter/titlecase', 'mark/non_spacing', 'mark/spacing_combining',
    'mark/enclosing', 'number/decimal', 'number/letter', 'number/other', 'separator/space', 'separator/line',
    'separator/paragraph', 'other/control', 'other/format', 'other/surrogate', 'other/private', 'other/not_assigned',
    'letter/modifier', 'letter/other', 'punctuation/connector', 'punctuation/dash', 'punctuation/open',
    'punctuation/close', 'punctuation/initial', 'punctuation/final', 'punctuation/other', 'symbol/math',
    'symbol/currency', 'symbol/modifier', 'symbol/other']

# The property bits of unicode_tables.hpp and library/stdlib/unicode_util.erl.
FLAGS = {
    'wide': 1, 'ws': 2, 'ids': 4, 'idc': 8, 'nlps': 16, 'control': 32, 'extend': 64, 'zwj': 128, 'prepend': 256,
    'regional_indicator': 512, 'l': 1024, 'v': 2048, 't': 4096, 'lv': 8192, 'lvt': 16384,
    'extended_pictographic': 32768, 'consonant': 65536, 'linker': 131072}

# Grapheme and Indic classes as OTP's generator groups them.
PROPERTY_FLAGS = {
    'cr': 'control', 'lf': 'control', 'control': 'control', 'extend': 'extend', 'spacingmark': 'extend', 'zwj': 'zwj',
    'prepend': 'prepend', 'regional_indicator': 'regional_indicator', 'l': 'l', 'v': 'v', 't': 't', 'lv': 'lv',
    'lvt': 'lvt', 'extended_pictographic': 'extended_pictographic', 'consonant': 'consonant',
    'vowel_independent': 'consonant', 'virama': 'linker', 'invisible_stacker': 'linker'}


def code_points(field):
    """The code points of a 'XXXX' or 'XXXX..YYYY' field."""
    first, _, last = field.strip().partition('..')
    return range(int(first, 16), int(last or first, 16) + 1)


def read_properties(path, flags):
    """Adds the flags of a property file's classes OTP uses."""
    for line in path.read_text(encoding='utf-8').splitlines():
        data = line.split('#')[0].strip()
        if not data:
            continue
        points, value = (part.strip() for part in data.split(';')[:2])
        name = PROPERTY_FLAGS.get(value.lower())
        if name:
            for point in code_points(points):
                flags[point] = flags.get(point, 0) | FLAGS[name]


def read_observed(path):
    """The observer's lines: the spec version, pattern whitespace and per-code-point fields."""
    spec, whitespace, points = None, None, {}
    for line in path.read_text(encoding='utf-8').splitlines():
        head, _, rest = line.partition(' ')
        if head == 'spec':
            spec = tuple(int(part) for part in rest.split('.'))
        elif head == 'pattern_whitespace':
            whitespace = rest
        else:
            points[int(head)] = dict(field.split('=', 1) for field in rest.split())
    return spec, whitespace, points


def pairs(text):
    """[(code point, class)] of 'CP:CCC,...'."""
    return [tuple(int(part) for part in pair.split(':')) for pair in text.split(',')]


def compositions(uc_spec, classes):
    """(first, second, composed) for every two-point canonical decomposition of a starter not excluded."""
    excluded = set()
    for line in (uc_spec / 'CompositionExclusions.txt').read_text(encoding='utf-8').splitlines():
        data = line.split('#')[0].strip()
        if data:
            excluded.update(code_points(data))
    result = []
    for line in (uc_spec / 'UnicodeData.txt').read_text(encoding='utf-8').splitlines():
        fields = line.split(';')
        point, decomposition = int(fields[0], 16), fields[5]
        if not decomposition or decomposition.startswith('<') or point in excluded:
            continue
        parts = [int(part, 16) for part in decomposition.split()]
        if len(parts) == 2 and classes.get(parts[0], 0) == 0:
            result.append((parts[0], parts[1], point))
    return sorted(result)


def ranges(values):
    """Maximal runs [first, last, value] of consecutive code points with one value."""
    runs = []
    for point in sorted(values):
        value = values[point]
        if runs and runs[-1][1] == point - 1 and runs[-1][2] == value:
            runs[-1][1] = point
        else:
            runs.append([point, point, value])
    return runs


class Pool:
    """Code point sequences stored once in one array, addressed by (offset, size)."""

    def __init__(self):
        self.points, self.index = [], {}

    def add(self, sequence):
        key = tuple(sequence)
        if key not in self.index:
            self.index[key] = len(self.points)
            self.points.extend(key)
        return self.index[key], len(key)


def emit_array(lines, declaration, items, per_line):
    """An initialized std::array, `per_line` items per source line."""
    lines.append(f'{declaration}{{{{')
    for at in range(0, len(items), per_line):
        lines.append('    ' + ', '.join(items[at:at + per_line]) + ',')
    lines.append('}};')
    lines.append('')


def build(spec, whitespace, observed, uc_spec):
    """The generated source."""
    flags = {}
    for name in ['emoji-data.txt', 'GraphemeBreakProperty.txt', 'IndicSyllabicCategory.txt']:
        read_properties(uc_spec / name, flags)
    for point in range(0x1B0B, 0x1B0D):  # OTP adds these two to the Indic consonants.
        flags[point] = flags.get(point, 0) | FLAGS['consonant']
    for point, fields in observed.items():
        for name in fields.get('flags', '').split(','):
            if name:
                flags[point] = flags.get(point, 0) | FLAGS[name]
    classes = {point: int(fields['ccc']) for point, fields in observed.items() if 'ccc' in fields}
    categories = {point: CATEGORIES.index(fields['cat']) for point, fields in observed.items() if 'cat' in fields}
    tags, pool, decompositions, cases = [], Pool(), [], []
    for point in sorted(observed):
        fields = observed[point]
        decompositions.extend(decomposition(point, fields, classes, tags, pool))
        if 'case' in fields:
            mapped = [pool.add([int(part) for part in value.split('+')]) for value in fields['case'].split(';')]
            cases.append(f'{{{point}, {{{", ".join(str(m[0]) for m in mapped)}}}, '
                         f'{{{", ".join(str(m[1]) for m in mapped)}}}}}')
    lines = [
        '// Generated by tools/unicode/generate.py (plan step 41O1) from the Unicode Character Database files OTP 29',
        f'// pins (Unicode {spec[0]}.{spec[1]}, Copyright Unicode, Inc., Unicode License v3) and the answers of OTP\'s',
        '// unicode_util. Do not edit; rerun the generator instead (docs/builtins.md#unicode-util).',
        '#include "tables.hpp"',
        '',
        'namespace clause::runtime::builtins::unicode_tables {',
        'namespace {',
    ]
    for name, values in [('CATEGORY', categories), ('CLASS', classes), ('FLAG', flags)]:
        runs = ranges(values)
        emit_array(lines, f'constexpr std::array<Range, {len(runs)}> {name}_RANGES',
                   [f'{{{a}, {b}, {v}}}' for a, b, v in runs], 4)
    emit_array(lines, f'constexpr std::array<char32_t, {len(pool.points)}> POINTS', [str(p) for p in pool.points], 12)
    emit_array(lines, f'constexpr std::array<Decomposition, {len(decompositions)}> DECOMPOSITIONS', decompositions, 3)
    emit_array(lines, f'constexpr std::array<CaseMapping, {len(cases)}> CASES', cases, 2)
    composed = compositions(uc_spec, classes)
    emit_array(lines, f'constexpr std::array<Composition, {len(composed)}> COMPOSITIONS',
               [f'{{{a}, {b}, {c}}}' for a, b, c in composed], 4)
    emit_array(lines, f'constexpr std::array<std::string_view, {len(tags)}> COMPAT_TAGS',
               [f'"{tag}"' for tag in tags], 6)
    space = [int(part) for part in whitespace.strip('[]').split('],', 1)[1].split(',')]
    emit_array(lines, f'constexpr std::array<char32_t, {len(space)}> PATTERN_WHITESPACE', [str(p) for p in space], 12)
    lines += [
        '} // namespace',
        '',
        f'std::pair<int, int> spec_version() noexcept {{ return {{{spec[0]}, {spec[1]}}}; }}',
        'std::span<const Range> category_ranges() noexcept { return CATEGORY_RANGES; }',
        'std::span<const Range> class_ranges() noexcept { return CLASS_RANGES; }',
        'std::span<const Range> flag_ranges() noexcept { return FLAG_RANGES; }',
        'std::span<const char32_t> points() noexcept { return POINTS; }',
        'std::span<const Decomposition> decompositions() noexcept { return DECOMPOSITIONS; }',
        'std::span<const CaseMapping> cases() noexcept { return CASES; }',
        'std::span<const Composition> compositions() noexcept { return COMPOSITIONS; }',
        'std::span<const std::string_view> compat_tags() noexcept { return COMPAT_TAGS; }',
        'std::span<const char32_t> pattern_whitespace() noexcept { return PATTERN_WHITESPACE; }',
        '} // namespace clause::runtime::builtins::unicode_tables',
    ]
    return '\n'.join(lines) + '\n'


def decomposition(point, fields, classes, tags, pool):
    """The DECOMPOSITIONS entry of a code point with a canonical or compatibility decomposition, if any."""
    if 'canon' not in fields and 'compat' not in fields:
        return []
    canon = pairs(fields['canon']) if 'canon' in fields else []
    tag, compat = 0, []
    if 'compat' in fields:
        name, _, rest = fields['compat'].partition(':')
        if name not in tags:
            tags.append(name)
        tag, compat = tags.index(name) + 1, pairs(rest)
    for element, ccc in canon + compat:
        # Every element carries its own combining class, so the tables keep only the code points.
        assert classes.get(element, 0) == ccc, (point, element, ccc)
    canon_at, canon_size = pool.add([p for p, _ in canon])
    compat_at, compat_size = pool.add([p for p, _ in compat])
    return [f'{{{point}, {canon_at}, {canon_size}, {tag}, {compat_at}, {compat_size}}}']


def main():
    spec, whitespace, observed = read_observed(pathlib.Path(sys.argv[1]))
    OUTPUT.write_text(build(spec, whitespace, observed, pathlib.Path(sys.argv[2])), encoding='utf-8', newline='\n')
    print(f'wrote {OUTPUT}')


if __name__ == '__main__':
    main()
