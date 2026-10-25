#include "../lists/list_walk.hpp"
#include "filename.hpp"
#include <array>
#include <optional>

// filename's joins and splits (plan 21 step LIB5G, docs/builtins.md#filename): join/1,2, absname_join/2,
// nativename/1, pathtype/1 and split/1, following OTP's clauses over deep lists as they open them, so the
// normalized separators, Windows' lowered drive letters and the results' shapes agree.
namespace clause::runtime::builtins::filename {
namespace {
using lists::no_clause;

enum class PathType : std::uint8_t { absolute, relative, volumerelative };

bool slash(std::byte b) { return b == std::byte{'/'} || b == std::byte{'\\'}; }

bool slash_item(const Walk &walk, std::size_t index) {
    return index < walk.size() && (walk.is(index, '/') || walk.is(index, '\\'));
}

bool list_item(const Walk &walk, std::size_t index) { return index < walk.size() && walk.at(index).is_list(); }

PathType unix_pathtype(ProcessContext &context, const Term &name) {
    if (name.is_binary()) {
        const auto bytes = bytes_of(name);
        return !bytes.empty() && bytes[0] == std::byte{'/'} ? PathType::absolute : PathType::relative;
    }
    Walk walk(context, name);
    while (walk.size() > 0 && (walk.at(0).is_list() || walk.at(0).is_atom())) {
        walk.open_head();
    }
    return walk.is(0, '/') ? PathType::absolute : PathType::relative;
}

PathType windows_bytes_pathtype(const std::vector<std::byte> &bytes) {
    if (bytes.size() >= 2 && slash(bytes[0]) && slash(bytes[1])) {
        return PathType::absolute;
    }
    if (!bytes.empty() && slash(bytes[0])) {
        return PathType::volumerelative;
    }
    if (bytes.size() >= 2 && bytes[1] == std::byte{':'}) {
        return bytes.size() >= 3 && slash(bytes[2]) ? PathType::absolute : PathType::volumerelative;
    }
    return PathType::relative;
}

// OTP's windows_pathtype over a list: it opens nested lists and atoms at the head and lists in the second and third
// places as its clauses reach them.
// OTP's first windows_pathtype clauses: a head list or atom, then a list in the second place, opened.
bool open_early(Walk &walk) {
    if (walk.size() > 0 && (walk.at(0).is_list() || walk.at(0).is_atom())) {
        walk.open_head();
        return true;
    }
    if (list_item(walk, 1)) {
        walk.open_at(1);
        return true;
    }
    return false;
}

std::optional<PathType> windows_clause(Walk &walk) {
    if (open_early(walk)) {
        return std::nullopt;
    }
    if (slash_item(walk, 0)) {
        return slash_item(walk, 1) ? PathType::absolute : PathType::volumerelative;
    }
    if (list_item(walk, 2)) {
        walk.open_at(2);
        return std::nullopt;
    }
    if (walk.size() >= 2 && walk.is(1, ':')) {
        return slash_item(walk, 2) ? PathType::absolute : PathType::volumerelative;
    }
    return PathType::relative;
}

PathType windows_list_pathtype(Walk &walk) {
    while (true) {
        if (const auto type = windows_clause(walk)) {
            return *type;
        }
    }
}

// Whether OTP's finish_join keeps a trailing separator: a root "/", a drive's root "c:/" or Windows' "//".
bool keeps_separator(std::size_t size, bool colon_before, bool slash_before) {
    return size == 1 || (WINDOWS && ((size == 3 && colon_before) || (size == 2 && slash_before)));
}

PathType pathtype_of(ProcessContext &context, const Term &name) {
    if (name.is_atom()) {
        return pathtype_of(context, list_of(context, flatten(context, name)));
    }
    if (!name.is_list() && !name.is_binary()) {
        no_clause();
    }
    if (!WINDOWS) {
        return unix_pathtype(context, name);
    }
    if (name.is_binary()) {
        return windows_bytes_pathtype(bytes_of(name));
    }
    Walk walk(context, name);
    return windows_list_pathtype(walk);
}

Term pathtype1(ProcessContext &context, const Term &name) {
    constexpr std::array NAMES{"absolute", "relative", "volumerelative"};
    return need(TermFactory(context).atom(NAMES.at(static_cast<std::size_t>(pathtype_of(context, name)))));
}

// The joined characters, the latest last (OTP's reversed Result).
class Joined final {
  public:
    explicit Joined(ProcessContext &context) : context_(context) {}

    bool empty() const { return chars_.empty(); }

    bool last_is(char32_t c, std::size_t back = 0) const {
        return chars_.size() > back && element_of(chars_[chars_.size() - 1 - back]).is(c);
    }

    void push(const Term &c) { chars_.push_back(c); }

    void push(char32_t c) { chars_.push_back(need(TermFactory(context_).integer(static_cast<std::int64_t>(c)))); }

    void pop() { chars_.pop_back(); }

    // Before the next name: a separator, unless one (or a drive) ends the result; a trailing "/." becomes "/".
    void separate() {
        if (WINDOWS && last_is(':')) {
            return;
        }
        if (last_is('/')) {
            return;
        }
        if (last_is('.') && last_is('/', 1)) {
            pop();
            return;
        }
        push('/');
    }

    // OTP's finish_join: no trailing separator, except for a root, a drive's root or Windows' "//".
    std::vector<Term> finish() {
        if (last_is('/') && !keeps_separator(chars_.size(), last_is(':', 1), last_is('/', 1))) {
            pop();
        }
        return std::move(chars_);
    }

  private:
    ProcessContext &context_;
    std::vector<Term> chars_;
};

// OTP's Windows clauses at the start of a join: an upper-case drive lowered, two backslashes made "//", "//" kept.
// An upper-case drive letter and colon, lowered, or "//", taken at the start.
bool drive_or_network(Walk &walk, Joined &joined) {
    const auto head = element_of(walk.at(0));
    const bool drive = head.integer && head.code >= 'A' && head.code <= 'Z' && walk.is(1, ':');
    if (!drive && !(walk.is(0, '/') && walk.is(1, '/'))) {
        return false;
    }
    joined.push(drive ? static_cast<char32_t>(head.code - 'A' + 'a') : '/');
    joined.push(drive ? ':' : '/');
    walk.pop();
    walk.pop();
    return true;
}

bool join_start(Walk &walk, Joined &joined) {
    if (!WINDOWS || !joined.empty()) {
        return false;
    }
    if (walk.is(0, '\\') && walk.is(1, '\\')) {
        walk.pop();
        walk.replace('/');
        walk.push('/');
        return true;
    }
    return drive_or_network(walk, joined);
}

// OTP's separator clauses: a backslash made a slash, "/./" made "/", repeated slashes dropped.
bool join_separator(Walk &walk, Joined &joined) {
    if (WINDOWS && walk.is(0, '\\')) {
        walk.replace('/');
        return true;
    }
    if (!walk.is(0, '/')) {
        return false;
    }
    if (joined.last_is('.') && joined.last_is('/', 1)) {
        joined.pop();
    } else if (!joined.last_is('/')) {
        return false;
    }
    walk.pop();
    return true;
}

// One step of OTP's join_chars on the head of `walk`; false once the walk has no head.
bool join_step(Walk &walk, Joined &joined) {
    if (walk.size() == 0) {
        if (!walk.ends()) {
            no_clause();
        }
        return false;
    }
    if (join_start(walk, joined) || join_separator(walk, joined)) {
        return true;
    }
    const auto head = element_of(walk.at(0));
    if (head.term.is_list() || head.term.is_atom()) {
        walk.open_head();
    } else if (head.integer) {
        joined.push(head.term);
        walk.pop();
    } else {
        no_clause();
    }
    return true;
}

// OTP's join_chars(Name, Next): Name's characters, then Next's after a separator.
Term join_lists(ProcessContext &context, const Term &name, const std::optional<Term> &next) {
    Joined joined(context);
    Walk walk(context, name);
    while (join_step(walk, joined)) {
    }
    if (next && !next->is_nil()) {
        joined.separate();
        Walk rest(context, *next);
        while (join_step(rest, joined)) {
        }
    }
    return need(TermFactory(context).list(joined.finish()));
}

// OTP's join_bytes, the binary twin of join_chars.
class JoinedBytes final {
  public:
    void add(const std::vector<std::byte> &bytes) {
        for (std::size_t at = 0; at < bytes.size(); ++at) {
            at += step(bytes, at);
        }
    }

    void separate() {
        if ((WINDOWS && last_is(':')) || last_is('/')) {
            return;
        }
        if (last_is('.') && last_is('/', 1)) {
            out_.pop_back();
            return;
        }
        out_.push_back(std::byte{'/'});
    }

    std::vector<std::byte> finish() {
        if (last_is('/') && !keeps_separator(out_.size(), last_is(':', 1), last_is('/', 1))) {
            out_.pop_back();
        }
        return std::move(out_);
    }

  private:
    bool last_is(char c, std::size_t back = 0) const {
        return out_.size() > back && out_[out_.size() - 1 - back] == static_cast<std::byte>(c);
    }

    // OTP's Windows clauses at the start: an upper-case drive lowered, a leading "//" or two backslashes (or a
    // backslash and a slash, which OTP turns into "//") kept as "//"; whether they applied.
    bool start(const std::vector<std::byte> &bytes, std::size_t at) {
        if (!WINDOWS || !out_.empty() || at + 1 >= bytes.size()) {
            return false;
        }
        const auto b = std::to_integer<char>(bytes[at]);
        if (b >= 'A' && b <= 'Z' && bytes[at + 1] == std::byte{':'}) {
            out_.push_back(static_cast<std::byte>(b - 'A' + 'a'));
            out_.push_back(std::byte{':'});
            return true;
        }
        const bool unc = bytes[at] == std::byte{'\\'} ? slash(bytes[at + 1]) : bytes[at + 1] == std::byte{'/'};
        if (slash(bytes[at]) && unc) {
            out_.push_back(std::byte{'/'});
            out_.push_back(std::byte{'/'});
            return true;
        }
        return false;
    }

    // One byte or a pair at `at`; the extra bytes consumed.
    std::size_t step(const std::vector<std::byte> &bytes, std::size_t at) {
        if (start(bytes, at)) {
            return 1;
        }
        const auto b = std::to_integer<char>(bytes[at]);
        const bool separator = b == '/' || (WINDOWS && b == '\\');
        if (separator && last_is('.') && last_is('/', 1)) {
            out_.pop_back();
        } else if (separator && last_is('/')) {
        } else {
            out_.push_back(separator ? std::byte{'/'} : bytes[at]);
        }
        return 0;
    }

    std::vector<std::byte> out_;
};

Term join_binaries(ProcessContext &context, const Term &name, const Term &next) {
    JoinedBytes joined;
    joined.add(bytes_of(name));
    if (const auto rest = bytes_of(next); !rest.empty()) {
        joined.separate();
        joined.add(rest);
    }
    return binary_of(context, joined.finish());
}

Term atom_chars(ProcessContext &context, const Term &atom) { return list_of(context, flatten(context, atom)); }

// OTP's join/2 conversions: a list beside a binary to a binary, an atom to its characters.
std::pair<Term, Term> join_kinds(ProcessContext &context, const Term &name1, const Term &name2) {
    if (name1.is_binary() && name2.is_list()) {
        return {name1, to_binary(context, name2)};
    }
    if (name1.is_list() && name2.is_binary()) {
        return {to_binary(context, name1), name2};
    }
    if (name1.is_atom()) {
        return {atom_chars(context, name1), name2};
    }
    if (name2.is_atom()) {
        return {name1, atom_chars(context, name2)};
    }
    no_clause();
}

// join(Name1, Name2): Name2 alone when it is not relative.
Term join2(ProcessContext &context, const Term &name1, const Term &name2) {
    const auto relative = [&] { return pathtype_of(context, name2) == PathType::relative; };
    if (name1.is_list() && name2.is_list()) {
        return relative() ? join_lists(context, name1, name2) : join_lists(context, name2, std::nullopt);
    }
    if (name1.is_binary() && name2.is_binary()) {
        return relative() ? join_binaries(context, name1, name2)
                          : join_binaries(context, name2, binary_of(context, {}));
    }
    const auto [left, right] = join_kinds(context, name1, name2);
    return join2(context, left, right);
}

// join([Name]): one name normalized.
Term join_one(ProcessContext &context, const Term &name) {
    if (name.is_list()) {
        return join_lists(context, name, std::nullopt);
    }
    if (name.is_binary()) {
        return join_binaries(context, name, binary_of(context, {}));
    }
    if (name.is_atom()) {
        return join_one(context, atom_chars(context, name));
    }
    no_clause();
}

// join(Components): joined pairwise from the left, then normalized once more.
Term join1(ProcessContext &context, const Term &components) {
    if (!components.is_cons()) {
        no_clause();
    }
    auto joined = components.head_unchecked();
    Term rest = components.tail_unchecked();
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        joined = join2(context, joined, rest.head_unchecked());
    }
    if (!rest.is_nil()) {
        no_clause();
    }
    return join_one(context, joined);
}

// absname_join(Base, Name): Base joined with Name flattened (a binary stays one).
Term absname_join2(ProcessContext &context, const Term &base, const Term &name) {
    return join2(context, base, name.is_binary() ? name : list_of(context, flatten(context, name)));
}

Term nativename1(ProcessContext &context, const Term &name) {
    auto joined = join_one(context, name);
    if (!WINDOWS) {
        return joined;
    }
    if (joined.is_binary()) {
        auto bytes = bytes_of(joined);
        std::ranges::replace(bytes, std::byte{'/'}, std::byte{'\\'});
        return binary_of(context, bytes);
    }
    auto chars = flatten(context, joined);
    for (auto &c : chars) {
        if (c.is('/')) {
            c = character(context, '\\');
        }
    }
    return list_of(context, chars);
}

// The binary's parts between separators (both on Windows), empty ones dropped.
std::vector<Term> binary_parts(ProcessContext &context, std::span<const std::byte> bytes) {
    std::vector<Term> parts;
    std::size_t start = 0;
    for (std::size_t at = 0; at <= bytes.size(); ++at) {
        if (at == bytes.size() || bytes[at] == std::byte{'/'} || (WINDOWS && bytes[at] == std::byte{'\\'})) {
            if (at > start) {
                parts.push_back(binary_of(context, bytes.subspan(start, at - start)));
            }
            start = at + 1;
        }
    }
    return parts;
}

std::byte lower_drive(std::byte b) {
    const auto c = std::to_integer<char>(b);
    return c >= 'A' && c <= 'Z' ? static_cast<std::byte>(c - 'A' + 'a') : b;
}

bool letter_byte(std::byte b) {
    const auto c = std::to_integer<char>(b);
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

// A Windows binary name's drive (lowered, with its separator) or "//"; none for another start.
std::vector<std::byte> windows_root(std::span<const std::byte> span) {
    if (span.size() < 2) {
        return {};
    }
    if (letter_byte(span[0]) && span[1] == std::byte{':'}) {
        std::vector<std::byte> drive{lower_drive(span[0]), std::byte{':'}};
        if (span.size() >= 3 && slash(span[2])) {
            drive.push_back(std::byte{'/'});
        }
        return drive;
    }
    if (slash(span[0]) && span[0] == span[1]) {
        return {std::byte{'/'}, std::byte{'/'}};
    }
    return {};
}

// The first part split/1 gives a binary name: a Windows drive or "//", else "/" for a leading separator.
std::vector<std::byte> binary_root(std::span<const std::byte> span) {
    if (auto root = WINDOWS ? windows_root(span) : std::vector<std::byte>{}; !root.empty()) {
        return root;
    }
    if (!span.empty() && (span[0] == std::byte{'/'} || (WINDOWS && span[0] == std::byte{'\\'}))) {
        return {std::byte{'/'}};
    }
    return {};
}

Term split_binary(ProcessContext &context, const Term &name) {
    const auto bytes = bytes_of(name);
    const auto span = std::span<const std::byte>(bytes);
    const auto root = binary_root(span);
    std::vector<Term> parts;
    if (!root.empty()) {
        parts.push_back(binary_of(context, root));
    }
    // A Windows drive's separator (or the second slash of "//") is part of the root.
    const auto skip = std::min(root.size(), span.size());
    const auto rest = binary_parts(context, span.subspan(skip));
    parts.insert(parts.end(), rest.begin(), rest.end());
    return need(TermFactory(context).list(parts));
}

// OTP's components/3,4: a leading separator as "/", then the parts between separators.
class Components final {
  public:
    Components(ProcessContext &context, std::vector<Term> done) : context_(context), done_(std::move(done)) {}

    Term run(const Elements &elements, std::size_t at, bool leading) {
        if (leading && at < elements.size() && elements[at].separator()) {
            done_.push_back(list_of(context_, std::array{character(context_, '/')}));
            ++at;
        }
        Elements part;
        for (; at < elements.size(); ++at) {
            const auto &element = elements[at];
            if (element.separator()) {
                end_part(part);
            } else if (element.integer) {
                part.push_back(element);
            } else {
                no_clause();
            }
        }
        end_part(part);
        return need(TermFactory(context_).list(done_));
    }

  private:
    // A part read up to a separator or the end, if any.
    void end_part(Elements &part) {
        if (!part.empty()) {
            done_.push_back(list_of(context_, part));
            part.clear();
        }
    }

    ProcessContext &context_;
    std::vector<Term> done_;
};

// OTP's windows_split over a flattened name: its rewrites of backslashes and an upper-case drive, then a "//", a
// drive or a leading separator as the first part.
// OTP's windows_split rewrites: a backslash at one of the first three places (after integers) made a slash, an
// upper-case drive lowered; whether one applied.
bool windows_rewrite(ProcessContext &context, Elements &e) {
    for (std::size_t index = 0; index < 3 && index < e.size(); ++index) {
        if (e[index].is('\\')) {
            e[index] = character(context, '/');
            return true;
        }
        if (!e[index].integer) {
            break;
        }
    }
    if (e.size() >= 2 && e[0].integer && e[0].code >= 'A' && e[0].code <= 'Z' && e[1].is(':')) {
        e[0] = character(context, static_cast<char32_t>(e[0].code - 'A' + 'a'));
        return true;
    }
    return false;
}

// Whether the name starts with two equal slashes (a Windows network name).
bool network_start(const Elements &e) {
    return e.size() >= 2 && (e[0].is('/') || e[0].is('\\')) && e[1].integer && e[1].code == e[0].code;
}

// OTP's windows_split over a flattened name: "//", its rewrites of backslashes and an upper-case drive, then a
// drive or a leading separator as the first part.
Term windows_split(ProcessContext &context, Elements e) {
    while (!network_start(e) && windows_rewrite(context, e)) {
    }
    if (network_start(e)) {
        return Components(context, {list_of(context, std::array{character(context, '/'), character(context, '/')})})
            .run(e, 2, true);
    }
    if (e.size() >= 2 && e[1].is(':')) {
        const std::size_t drive = e.size() >= 3 && e[2].is('/') ? 3 : 2;
        return Components(context, {list_of(context, std::span(e).subspan(0, drive))}).run(e, drive, false);
    }
    return Components(context, {}).run(e, 0, true);
}

Term split1(ProcessContext &context, const Term &name) {
    if (name.is_binary()) {
        return split_binary(context, name);
    }
    auto elements = flatten(context, name);
    if (WINDOWS) {
        return windows_split(context, std::move(elements));
    }
    return Components(context, {}).run(elements, 0, true);
}

constexpr std::array FILENAME_JOIN_BUILTINS{
    typed_entry<join1>("filename", "join"),
    typed_entry<join2>("filename", "join"),
    typed_entry<absname_join2>("filename", "absname_join"),
    typed_entry<nativename1>("filename", "nativename"),
    typed_entry<pathtype1>("filename", "pathtype"),
    typed_entry<split1>("filename", "split"),
};
} // namespace

bool is_relative(ProcessContext &context, const Term &name) { return pathtype_of(context, name) == PathType::relative; }

Term join(ProcessContext &context, const Term &name1, const Term &name2) { return join2(context, name1, name2); }
} // namespace clause::runtime::builtins::filename

namespace clause::runtime {
std::span<const BuiltinEntry> filename_join_builtins() noexcept { return builtins::filename::FILENAME_JOIN_BUILTINS; }
} // namespace clause::runtime
