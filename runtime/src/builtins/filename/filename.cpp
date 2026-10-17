#include "filename.hpp"
#include "../collections/collections.hpp"
#include "../lists/list_walk.hpp"
#include "../text.hpp"
#include "../unicode/chardata.hpp"
#include <algorithm>
#include <array>
#include <limits>
#include <ranges>
#include <string_view>

// filename's walks over one name (plan 21 step LIB5G, docs/builtins.md#filename): flatten/1, append/2,
// basename/1,2, dirname/1, extension/1, rootname/1,2 and validate/1, each following OTP's clauses so the result's
// list or binary shape, the separators it normalizes and its errors agree.
namespace clause::runtime::builtins::filename {
namespace {
using lists::no_clause;

constexpr std::byte SLASH{'/'};
constexpr std::byte BACKSLASH{'\\'};
constexpr std::byte COLON{':'};

bool letter_byte(std::byte b) {
    const auto c = std::to_integer<char>(b);
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

bool separator_byte(std::byte b) { return b == SLASH || (WINDOWS && b == BACKSLASH); }

std::vector<Term> chars_of_atom(ProcessContext &context, const Term &atom) {
    std::vector<Term> chars;
    for (const auto code : code_points(atom.atom_spelling_unchecked())) {
        chars.push_back(need(TermFactory(context).integer(static_cast<std::int64_t>(code))));
    }
    return chars;
}

// The names of a function taking two: both binaries when either is. basename/2 converts only a list beside a binary
// (`lists`), rootname/2 and append/2 anything.
std::pair<Term, Term> same_kind(ProcessContext &context, const Term &name, const Term &suffix, bool lists) {
    if (name.is_binary() && !suffix.is_binary() && (!lists || suffix.is_list())) {
        return {name, to_binary(context, suffix)};
    }
    if (suffix.is_binary() && !name.is_binary() && (!lists || name.is_list())) {
        return {to_binary(context, name), suffix};
    }
    return {name, suffix};
}

// The code points of a binary as a list, as binary_to_list/1.
Elements byte_elements(ProcessContext &context, const Term &binary) {
    Elements elements;
    for (const auto b : bytes_of(binary)) {
        elements.push_back(character(context, std::to_integer<unsigned char>(b)));
    }
    return elements;
}

Term bytes_binary(ProcessContext &context, const Elements &elements) {
    std::vector<std::byte> bytes;
    for (const auto &element : elements) {
        if (!element.integer || element.code < 0 || element.code > 255) {
            bad_argument();
        }
        bytes.push_back(static_cast<std::byte>(element.code));
    }
    return binary_of(context, bytes);
}

// basename/1 of a list name: the characters after the last separator (a trailing one dropped), a drive skipped.
Term list_basename(ProcessContext &context, const Term &name) {
    const auto elements = flatten(context, name);
    std::size_t start = WINDOWS && elements.size() >= 2 && elements[0].letter() && elements[1].is(':') ? 2 : 0;
    std::size_t tail = start;
    for (auto at = start; at < elements.size(); ++at) {
        if (elements[at].separator()) {
            if (at + 1 == elements.size()) {
                return list_of(context, std::span(elements).subspan(tail, at - tail));
            }
            tail = at + 1;
        } else if (!elements[at].integer) {
            no_clause();
        }
    }
    return list_of(context, std::span(elements).subspan(tail));
}

// The binary's parts between separators, empty ones dropped.
std::vector<std::span<const std::byte>> parts_of(std::span<const std::byte> bytes) {
    std::vector<std::span<const std::byte>> parts;
    std::size_t start = 0;
    for (std::size_t at = 0; at <= bytes.size(); ++at) {
        if (at == bytes.size() || separator_byte(bytes[at])) {
            if (at > start) {
                parts.push_back(bytes.subspan(start, at - start));
            }
            start = at + 1;
        }
    }
    return parts;
}

Term binary_basename(ProcessContext &context, const Term &name) {
    const auto bytes = bytes_of(name);
    auto span = std::span<const std::byte>(bytes);
    if (WINDOWS && span.size() >= 2 && letter_byte(span[0]) && span[1] == COLON) {
        span = span.subspan(2);
    }
    const auto parts = parts_of(span);
    return binary_of(context, parts.empty() ? std::span<const std::byte>{} : parts.back());
}

Term basename1(ProcessContext &context, const Term &name) {
    return name.is_binary() ? binary_basename(context, name) : list_basename(context, name);
}

// Whether two element runs are equal lists (=:=).
bool same_terms(std::span<const Element> left, std::span<const Element> right) {
    return std::ranges::equal(left, right,
                              [](const Element &a, const Element &b) { return collections::same(a.term, b.term); });
}

// The position after a Windows drive letter and colon at the start, else 0.
std::size_t after_drive(const Elements &elements) {
    return WINDOWS && elements.size() >= 2 && elements[0].letter() && elements[1].is(':') ? 2 : 0;
}

// Whether OTP's without_extension stops at `at`: the rest equals the extension, the name ends, or a backslash (seen
// as a slash) makes the rest equal it.
bool extension_at(ProcessContext &context, Elements &elements, std::size_t at, const Elements &ext) {
    const auto rest = std::span(elements).subspan(at);
    if (same_terms(rest, ext) || rest.empty()) {
        return true;
    }
    if (WINDOWS && rest[0].is('\\')) {
        elements[at] = character(context, '/');
        return same_terms(std::span(elements).subspan(at), ext);
    }
    return false;
}

// OTP's without_extension: the last component's characters, stopping where the rest equals the extension.
Term list_basename2(ProcessContext &context, const Term &name, const Term &extension) {
    auto elements = flatten(context, name);
    const auto ext = flatten(context, extension);
    std::size_t at = after_drive(elements);
    std::size_t tail = at;
    for (; !extension_at(context, elements, at, ext); ++at) {
        if (elements[at].is('/') && at + 1 == elements.size()) {
            break;
        }
        if (elements[at].is('/')) {
            tail = at + 1;
        } else if (!elements[at].integer) {
            no_clause();
        }
    }
    return list_of(context, std::span(elements).subspan(tail, at - tail));
}

// basename/2 of binaries: the bytes before the extension at the end of the name, as long as the last component.
Term binary_basename2(ProcessContext &context, const Term &name, const Term &extension) {
    const auto base = bytes_of(binary_basename(context, name));
    const auto bytes = bytes_of(name);
    const auto ext = bytes_of(extension);
    if (base.size() < ext.size()) {
        return binary_of(context, base);
    }
    const auto keep = base.size() - ext.size();
    const auto start = bytes.size() - keep - ext.size();
    if (!std::ranges::equal(std::span(bytes).subspan(start + keep), ext)) {
        return binary_of(context, base);
    }
    return binary_of(context, std::span(bytes).subspan(start, keep));
}

Term basename2(ProcessContext &context, const Term &name, const Term &extension) {
    const auto [named, suffix] = same_kind(context, name, extension, true);
    if (named.is_binary() && suffix.is_binary()) {
        return binary_basename2(context, named, suffix);
    }
    return list_basename2(context, named, suffix);
}

// The directory parts of a binary name: all parts between separators but the last, trailing empty ones dropped.
std::vector<std::span<const std::byte>> directory_parts(std::span<const std::byte> span) {
    std::vector<std::span<const std::byte>> parts;
    std::size_t start = 0;
    for (std::size_t at = 0; at <= span.size(); ++at) {
        if (at == span.size() || separator_byte(span[at])) {
            parts.push_back(span.subspan(start, at - start));
            start = at + 1;
        }
    }
    parts.pop_back();
    while (parts.size() >= 2 && parts.back().empty()) {
        parts.pop_back();
    }
    return parts;
}

// OTP's dir_join: an empty part where a component starts is a separator; the others are joined by separators.
void join_parts(std::vector<std::byte> &out, const std::vector<std::span<const std::byte>> &parts) {
    for (std::size_t index = 0; index < parts.size();) {
        if (parts[index].empty()) {
            out.push_back(SLASH);
            ++index;
            if (index == parts.size()) {
                break;
            }
        }
        out.insert(out.end(), parts[index].begin(), parts[index].end());
        if (++index < parts.size()) {
            out.push_back(SLASH);
        }
    }
}

// dirname/1 of a binary: the directory parts joined after the drive, or "." when there is neither.
Term binary_dirname(ProcessContext &context, const Term &name) {
    const auto bytes = bytes_of(name);
    auto span = std::span<const std::byte>(bytes);
    std::vector<std::byte> out;
    if (WINDOWS && span.size() >= 2 && letter_byte(span[0]) && span[1] == COLON) {
        out.assign(span.begin(), span.begin() + 2);
        span = span.subspan(2);
    }
    const auto parts = directory_parts(span);
    if (parts.empty() && out.empty()) {
        out.push_back(std::byte{'.'});
    }
    join_parts(out, parts);
    return binary_of(context, out);
}

// OTP's dir_chars over a list name: the directory so far (reversed) and the component being read (reversed, with its
// separator).
class DirChars final {
  public:
    DirChars(ProcessContext &context, Elements elements) : context_(context), elements_(std::move(elements)) {}

    Term run() {
        for (std::size_t at = 0; at < elements_.size(); ++at) {
            step(at);
        }
        return finish();
    }

  private:
    void step(std::size_t &at) {
        const auto &element = elements_[at];
        if (element.separator()) {
            dir_.insert(dir_.begin(), file_.begin(), file_.end());
            file_.assign(1, character(context_, '/'));
            return;
        }
        if (WINDOWS && dir_.empty() && file_.empty() && element.letter() && at + 1 < elements_.size() &&
            elements_[at + 1].is(':')) {
            dir_ = {elements_[at + 1], element};
            ++at;
            return;
        }
        if (!element.integer) {
            no_clause();
        }
        file_.insert(file_.begin(), element);
    }

    bool file_rooted() const { return !file_.empty() && file_.back().is('/'); }

    // "/" for a name under the root, else ".".
    Term root_or_dot() const { return list_of(context_, Elements{character(context_, file_rooted() ? '/' : '.')}); }

    Term finish() {
        while (!dir_.empty() && dir_.front().is('/')) {
            dir_.erase(dir_.begin());
        }
        if (dir_.empty()) {
            return root_or_dot();
        }
        if (WINDOWS && dir_.size() == 2 && dir_[0].is(':')) {
            auto drive = Elements{dir_[1], dir_[0]};
            if (file_rooted()) {
                drive.push_back(character(context_, '/'));
            }
            return list_of(context_, drive);
        }
        std::ranges::reverse(dir_);
        return list_of(context_, dir_);
    }

    ProcessContext &context_;
    Elements elements_;
    Elements dir_;
    Elements file_;
};

Term dirname1(ProcessContext &context, const Term &name) {
    return name.is_binary() ? binary_dirname(context, name) : DirChars(context, flatten(context, name)).run();
}

// extension/1 of a binary: from the last dot, unless a separator stands just before it or anywhere after it.
Term binary_extension(ProcessContext &context, const Term &name) {
    const auto bytes = bytes_of(name);
    const auto dot = std::ranges::find(std::ranges::reverse_view(bytes), std::byte{'.'});
    if (dot == std::ranges::reverse_view(bytes).end() || dot.base() - 1 == bytes.begin()) {
        return binary_of(context, {});
    }
    const auto at = static_cast<std::size_t>(dot.base() - 1 - bytes.begin());
    if (std::ranges::any_of(std::span(bytes).subspan(at - 1), separator_byte)) {
        return binary_of(context, {});
    }
    return binary_of(context, std::span(bytes).subspan(at));
}

// OTP's extension_chars: the suffix from the last dot of the last component; a dot right after a separator starts
// no extension.
Term list_extension(ProcessContext &context, const Term &name) {
    auto elements = flatten(context, name);
    elements.insert(elements.begin(), character(context, '/'));
    std::optional<std::size_t> start;
    for (std::size_t at = 0; at < elements.size(); ++at) {
        const auto &element = elements[at];
        if (element.is('.')) {
            start = at;
        } else if (element.separator() && at + 1 < elements.size() && elements[at + 1].is('.')) {
            start.reset();
            ++at;
        } else if (element.separator()) {
            start.reset();
        } else if (!element.integer) {
            no_clause();
        }
    }
    return list_of(context, start ? std::span(elements).subspan(*start) : std::span<const Element>{});
}

Term extension1(ProcessContext &context, const Term &name) {
    return name.is_binary() ? binary_extension(context, name) : list_extension(context, name);
}

// OTP's root_chars over a list name: the root so far (reversed) and a pending extension from the last dot.
Elements root_chars(ProcessContext &context, const Elements &elements) {
    Elements root;
    Elements ext;
    for (const auto &element : elements) {
        if (element.separator()) {
            root.insert(root.begin(), ext.begin(), ext.end());
            root.insert(root.begin(), character(context, '/'));
            ext.clear();
        } else if (element.is('.') && !(ext.empty() && !root.empty() && root.front().is('/'))) {
            root.insert(root.begin(), ext.begin(), ext.end());
            ext.assign(1, element);
        } else if (!element.integer) {
            no_clause();
        } else if (ext.empty()) {
            root.insert(root.begin(), element);
        } else {
            ext.insert(ext.begin(), element);
        }
    }
    std::ranges::reverse(root);
    return root;
}

Term rootname1(ProcessContext &context, const Term &name) {
    if (name.is_binary()) {
        return bytes_binary(context, root_chars(context, byte_elements(context, name)));
    }
    return list_of(context, root_chars(context, flatten(context, name)));
}

// OTP's without_suffix: the name without the extension at its end; a dotfile after a separator keeps it.
Elements without_suffix(const Elements &elements, std::span<const Element> ext) {
    std::size_t at = 0;
    while (!same_terms(std::span(elements).subspan(at), ext)) {
        if (at == elements.size()) {
            return elements;
        }
        if (!elements[at++].integer) {
            no_clause();
        }
    }
    const bool rooted = at > 0 && elements[at - 1].separator();
    return {elements.begin(), rooted ? elements.end() : elements.begin() + static_cast<std::ptrdiff_t>(at)};
}

Term rootname2(ProcessContext &context, const Term &name, const Term &extension) {
    const auto [named, suffix] = same_kind(context, name, extension, false);
    if (named.is_binary()) {
        return bytes_binary(context, without_suffix(byte_elements(context, named), byte_elements(context, suffix)));
    }
    return list_of(context, without_suffix(flatten(context, named), flatten(context, suffix)));
}

// append(Dir, Name): Dir/Name with no normalization; binaries when either is one.
Term append2(ProcessContext &context, const Term &dir, const Term &name) {
    const auto [left, right] = same_kind(context, dir, name, false);
    if (left.is_binary()) {
        auto bytes = bytes_of(left);
        bytes.push_back(SLASH);
        const auto tail = bytes_of(right);
        bytes.insert(bytes.end(), tail.begin(), tail.end());
        return binary_of(context, bytes);
    }
    if (!need(left.is_proper_list())) {
        bad_argument();
    }
    std::vector<Term> items;
    for (Term list = left; list.is_cons(); list = list.tail_unchecked()) {
        items.push_back(list.head_unchecked());
    }
    items.push_back(need(TermFactory(context).integer('/')));
    return need(TermFactory(context).list(items, right));
}

// validate/1's character rule for the native UTF-8 encoding of the host.
bool valid_code(const Element &element) {
    if (!element.integer || element.code < 1 || element.code >= 0x110000) {
        return false;
    }
    if (WINDOWS) {
        return element.code <= 0xFFFF;
    }
    return element.code < 0xD800 || element.code > 0xDFFF;
}

// validate/1 of a list or atom: every character valid, at least one; nested lists, atoms and an improper tail's
// character or atom read as OTP's valid_chars/4 reads them.
bool valid_chars(ProcessContext &context, const Term &name, std::size_t &count) {
    std::vector<Term> pending{name};
    while (!pending.empty()) {
        const auto term = pending.back();
        pending.pop_back();
        if (term.is_cons()) {
            pending.push_back(term.tail_unchecked());
            pending.push_back(term.head_unchecked());
        } else if (term.is_atom()) {
            const auto chars = chars_of_atom(context, term);
            pending.insert(pending.end(), chars.rbegin(), chars.rend());
        } else if (term.is_integer()) {
            ++count;
            if (!valid_code(element_of(term))) {
                return false;
            }
        } else if (!term.is_nil()) {
            return false;
        }
    }
    return true;
}

Term validate1(ProcessContext &context, const Term &name) {
    bool valid = false;
    if (name.is_binary()) {
        const auto bytes = bytes_of(name);
        valid = !bytes.empty() && std::ranges::find(bytes, std::byte{0}) == bytes.end();
    } else if (name.is_list() || name.is_atom()) {
        std::size_t count = 0;
        valid = valid_chars(context, name, count) && count > 0;
    } else {
        no_clause();
    }
    return need(TermFactory(context).boolean(valid));
}

Term flatten1(ProcessContext &context, const Term &name) {
    return name.is_binary() ? name : list_of(context, flatten(context, name));
}

constexpr std::array FILENAME_BUILTINS{
    typed_entry<flatten1>("filename", "flatten"),   typed_entry<append2>("filename", "append"),
    typed_entry<basename1>("filename", "basename"), typed_entry<basename2>("filename", "basename"),
    typed_entry<dirname1>("filename", "dirname"),   typed_entry<extension1>("filename", "extension"),
    typed_entry<rootname1>("filename", "rootname"), typed_entry<rootname2>("filename", "rootname"),
    typed_entry<validate1>("filename", "validate"),
};
} // namespace

Element element_of(const Term &term) {
    if (!term.is_integer()) {
        return {.term = term};
    }
    const auto code = term.integer_value();
    return {.term = term, .integer = true, .code = code ? *code : std::numeric_limits<std::int64_t>::min()};
}

Element character(ProcessContext &context, char32_t code) {
    return element_of(need(TermFactory(context).integer(static_cast<std::int64_t>(code))));
}

namespace {
void append_atom(ProcessContext &context, Elements &elements, const Term &atom) {
    for (const auto &c : chars_of_atom(context, atom)) {
        elements.push_back(element_of(c));
    }
}

// A list cell: its tail walked after its head; a nested list head walked next, an atom spelled out, anything else
// kept.
void flatten_cell(ProcessContext &context, Elements &elements, std::vector<Term> &pending, const Term &cell) {
    pending.push_back(cell.tail_unchecked());
    const auto head = cell.head_unchecked();
    if (head.is_list()) {
        pending.push_back(head);
    } else if (head.is_atom()) {
        append_atom(context, elements, head);
    } else {
        elements.push_back(element_of(head));
    }
}
} // namespace

Elements flatten(ProcessContext &context, const Term &name) {
    Elements elements;
    std::vector<Term> pending{name};
    while (!pending.empty()) {
        const auto term = pending.back();
        pending.pop_back();
        if (term.is_cons()) {
            flatten_cell(context, elements, pending, term);
        } else if (term.is_atom()) {
            append_atom(context, elements, term);
        } else if (!term.is_nil()) {
            no_clause();
        }
    }
    return elements;
}

Term list_of(ProcessContext &context, std::span<const Element> elements) {
    std::vector<Term> terms;
    terms.reserve(elements.size());
    for (const auto &element : elements) {
        terms.push_back(element.term);
    }
    return need(TermFactory(context).list(terms));
}

Term to_binary(ProcessContext &context, const Term &name) {
    const auto flat = list_of(context, flatten(context, name));
    const auto converted = unicode::convert(context, flat, unicode::Encoding{}, false);
    if (converted.outcome != unicode::Conversion::Outcome::done) {
        bad_argument();
    }
    std::string text;
    for (const auto code : converted.codes) {
        encode(code, text);
    }
    return binary_of(context, std::as_bytes(std::span(text)));
}

Term binary_of(ProcessContext &context, std::span<const std::byte> bytes) {
    return need(TermFactory(context).binary(bytes));
}

std::vector<std::byte> bytes_of(const Term &binary) { return need(binary.binary_bytes()); }

Walk::Walk(ProcessContext &context, const Term &list) : context_(context), tail_(list) {
    std::vector<Term> items;
    for (; tail_.is_cons(); tail_ = tail_.tail_unchecked()) {
        items.push_back(tail_.head_unchecked());
    }
    items_.assign(items.rbegin(), items.rend());
}

bool Walk::is(std::size_t index, char32_t c) const { return index < items_.size() && element_of(at(index)).is(c); }

void Walk::pop() { items_.pop_back(); }

void Walk::replace(char32_t c) { items_.back() = need(TermFactory(context_).integer(static_cast<std::int64_t>(c))); }

void Walk::push(char32_t c) { items_.push_back(need(TermFactory(context_).integer(static_cast<std::int64_t>(c)))); }

std::vector<Term> Walk::spread(const Term &term) const {
    if (term.is_atom()) {
        return chars_of_atom(context_, term);
    }
    std::vector<Term> items;
    Term list = term;
    for (; list.is_cons(); list = list.tail_unchecked()) {
        items.push_back(list.head_unchecked());
    }
    if (!list.is_nil()) {
        bad_argument();
    }
    return items;
}

void Walk::open_head() {
    const auto items = spread(items_.back());
    items_.pop_back();
    items_.insert(items_.end(), items.rbegin(), items.rend());
}

void Walk::open_at(std::size_t index) {
    const auto items = spread(at(index));
    const auto position = items_.end() - 1 - static_cast<std::ptrdiff_t>(index);
    const auto after = items_.erase(position);
    items_.insert(after, items.rbegin(), items.rend());
}
} // namespace clause::runtime::builtins::filename

namespace clause::runtime {
std::span<const BuiltinEntry> filename_builtins() noexcept { return builtins::filename::FILENAME_BUILTINS; }
} // namespace clause::runtime
