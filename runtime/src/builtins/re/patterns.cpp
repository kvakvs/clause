#include "../../terms/identities.hpp"
#include "regex.hpp"
#include <array>
#include <cstring>
#include <string_view>
#include <vector>

// Compiled patterns of the re module (plan step 41ZB): re:compile/2's builtin half and re:inspect/2. A pattern's
// handle reference holds the pcre2_code pointer; its finalizer frees the code with the last copy, as OTP's magic
// reference does. Compiled code is only read while matching, so processes share it.
namespace clause::runtime::builtins::regex {
namespace {
// The bytes of a pattern handle's buffer: a tag and the code it owns.
struct Owned {
    std::uint64_t magic;
    pcre2_code *code;
};

constexpr std::uint64_t PATTERN_MAGIC = 0x72655F7061747465; // "re_patte"

// Free the code of a pattern whose last reference died.
void release(std::span<std::byte> bytes) noexcept {
    Owned owned{};
    std::memcpy(&owned, bytes.data(), sizeof(owned));
    pcre2_code_free(owned.code);
}

Term atom(ProcessContext &context, std::string_view name) { return need(TermFactory(context).atom(name)); }

Term small_term(std::int64_t value) { return need(Term::from_word(need(encode_integer(value)))); }
} // namespace

pcre2_code *compile(std::span<const std::byte> bytes, const Options &options, CompileError &error) {
    pcre2_compile_context *context = nullptr;
    if (options.newline != 0 || options.bsr != 0) {
        context = pcre2_compile_context_create(nullptr);
        if (options.newline != 0) {
            pcre2_set_newline(context, options.newline);
        }
        if (options.bsr != 0) {
            pcre2_set_bsr(context, options.bsr);
        }
    }
    PCRE2_SIZE offset = 0;
    auto *code = pcre2_compile(reinterpret_cast<PCRE2_SPTR>(bytes.data()), bytes.size(), options.compile, &error.code,
                               &offset, context);
    error.offset = offset;
    pcre2_compile_context_free(context);
    return code;
}

Term pattern(ProcessContext &context, pcre2_code *code, bool unicode) {
    std::uint32_t captures = 0;
    std::uint32_t newline = 0;
    pcre2_pattern_info(code, PCRE2_INFO_CAPTURECOUNT, &captures);
    pcre2_pattern_info(code, PCRE2_INFO_NEWLINE, &newline);
    const bool crlf = newline == PCRE2_NEWLINE_ANY || newline == PCRE2_NEWLINE_CRLF || newline == PCRE2_NEWLINE_ANYCRLF;
    const Owned owned{PATTERN_MAGIC, code};
    std::array<std::byte, sizeof(Owned)> bytes{};
    std::memcpy(bytes.data(), &owned, sizeof(owned));
    const auto handle = detail::HandleAccess::make(context.heap(), bytes, release);
    if (!handle) {
        pcre2_code_free(code);
        need(handle);
    }
    const std::array parts{atom(context, "re_pattern"), small_term(captures), small_term(unicode ? 1 : 0),
                           small_term(crlf ? 1 : 0), *handle};
    return need(TermFactory(context).tuple(parts));
}

namespace {
// Whether a term has the {re_pattern, Integer, Integer, Integer, _} shape of a compiled pattern.
bool pattern_shape(const Term &term) {
    if (!term.is_tuple() || term.tuple_size_unchecked() != 5) {
        return false;
    }
    const auto tag = term.tuple_element_unchecked(0);
    if (!tag.is_atom() || tag.atom_spelling_unchecked() != "re_pattern") {
        return false;
    }
    for (std::size_t index = 1; index < 4; ++index) {
        const auto element = term.tuple_element_unchecked(index);
        if (!small(element.word())) {
            return false;
        }
    }
    return true;
}
} // namespace

const pcre2_code *pattern_code(const Term &term) {
    if (!pattern_shape(term)) {
        return nullptr;
    }
    const auto buffer = detail::HandleAccess::buffer(term.tuple_element_unchecked(4));
    if (!buffer || (*buffer)->size() != sizeof(Owned)) {
        return nullptr;
    }
    Owned owned{};
    std::memcpy(&owned, (*buffer)->data(), sizeof(owned));
    return owned.magic == PATTERN_MAGIC ? owned.code : nullptr;
}

Term compile_error(ProcessContext &context, const CompileError &error) {
    std::array<PCRE2_UCHAR, 120> text{};
    if (pcre2_get_error_message(error.code, text.data(), text.size()) == PCRE2_ERROR_BADDATA) {
        const std::string unknown = "Unknown error (" + std::to_string(error.code) + ")";
        std::memcpy(text.data(), unknown.c_str(), unknown.size() + 1);
    }
    const std::string_view message(reinterpret_cast<const char *>(text.data()));
    std::vector<Term> characters;
    for (const auto c : message) {
        characters.push_back(small_term(static_cast<unsigned char>(c)));
    }
    const std::array parts{need(TermFactory(context).list(characters)),
                           small_term(static_cast<std::int64_t>(error.offset))};
    return need(TermFactory(context).tuple(parts));
}

namespace {
// The pair {First, Second}.
Term pair(ProcessContext &context, const Term &first, const Term &second) {
    const std::array parts{first, second};
    return need(TermFactory(context).tuple(parts));
}

// clause_re:compile(Bytes, Options): {ok, Pattern} or {error, {Message, Offset}}; badarg for a matching-only or
// invalid option. re.erl converts Unicode chardata to Bytes first.
TermResult<Term> compile_pattern(ProcessContext &context, const BinaryArgument &regex, const ListArgument &list) {
    const auto options = parse(list.term);
    if (!options || options->exec_only || options->export_pattern) {
        bad_argument();
    }
    CompileError error{};
    auto *code = compile(regex.bytes, *options, error);
    if (code == nullptr) {
        return pair(context, atom(context, "error"), compile_error(context, error));
    }
    return pair(context, atom(context, "ok"), pattern(context, code, options->unicode));
}

// The distinct names of a pattern's name table, in its (alphabetical) order.
std::vector<std::string_view> names(const pcre2_code *code) {
    std::uint32_t count = 0;
    std::uint32_t size = 0;
    PCRE2_SPTR table = nullptr;
    pcre2_pattern_info(code, PCRE2_INFO_NAMECOUNT, &count);
    pcre2_pattern_info(code, PCRE2_INFO_NAMEENTRYSIZE, &size);
    pcre2_pattern_info(code, PCRE2_INFO_NAMETABLE, static_cast<void *>(&table));
    std::vector<std::string_view> result;
    for (std::uint32_t index = 0; index < count; ++index) {
        const std::string_view name(reinterpret_cast<const char *>(table + static_cast<std::size_t>(index) * size + 2));
        if (result.empty() || result.back() != name) {
            result.push_back(name);
        }
    }
    return result;
}

// re:inspect(Pattern, namelist): {namelist, [Name]}.
TermResult<Term> inspect(ProcessContext &context, const Term &term, const AtomArgument &item) {
    const auto *code = pattern_code(term);
    if (code == nullptr || item.spelling != "namelist") {
        bad_argument();
    }
    std::vector<Term> binaries;
    for (const auto name : names(code)) {
        binaries.push_back(need(TermFactory(context).binary(std::as_bytes(std::span(name)))));
    }
    return pair(context, atom(context, "namelist"), need(TermFactory(context).list(binaries)));
}

constexpr std::array PATTERN_BUILTINS{
    typed_entry<compile_pattern>("clause_re", "compile"),
    typed_entry<inspect>("re", "inspect"),
};
} // namespace
} // namespace clause::runtime::builtins::regex

namespace clause::runtime {
std::span<const BuiltinEntry> re_pattern_builtins() noexcept { return builtins::regex::PATTERN_BUILTINS; }
} // namespace clause::runtime
