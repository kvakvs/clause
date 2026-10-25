#include "../string/string.hpp"
#include "front_end.hpp"
#include "lint.hpp"
#include <algorithm>
#include <array>

// erl_lint's state (plan 21 step LIB34C): start/2's options and enabled warnings, adding errors and warnings,
// return_status/1's packing, and the small queries the checks share.
namespace clause::runtime::builtins::front_end::lint {
namespace {
using forms::element;
using forms::is_atom;
using forms::is_node;

// bool_options/0: each warning category and whether it is on by default.
constexpr std::array<std::pair<std::string_view, bool>, 46> BOOL_OPTIONS{{
    {"unused_vars", true},
    {"underscore_match", true},
    {"export_all", true},
    {"export_vars", false},
    {"export_var_subexpr", true},
    {"shadow_vars", true},
    {"unused_import", false},
    {"unused_function", true},
    {"unused_type", true},
    {"bif_clash", true},
    {"unused_record", true},
    {"deprecated_function", true},
    {"deprecated_type", true},
    {"deprecated_callback", true},
    {"deprecated_catch", true},
    {"obsolete_guard", true},
    {"obsolete_bool_op", false},
    {"untyped_record", false},
    {"missing_spec", false},
    {"missing_spec_documented", false},
    {"missing_spec_all", false},
    {"removed", true},
    {"nif_inline", true},
    {"keywords", false},
    {"redefined_builtin_type", true},
    {"match_float_zero", true},
    {"match_alias_pats", true},
    {"update_literal", true},
    {"behaviours", true},
    {"conflicting_behaviours", true},
    {"undefined_behaviour_func", true},
    {"undefined_behaviour", true},
    {"undefined_behaviour_callbacks", true},
    {"ill_defined_behaviour_callbacks", true},
    {"ill_defined_optional_callbacks", true},
    {"unexported_function", true},
    {"novalue", true},
    {"undefined_field", true},
    {"unsafe_function", true},
    {"possibly_unsafe_function", false},
    {"native_record_header", true},
}};

// The category a warn_X or nowarn_X option names, and whether it enables it.
std::optional<std::pair<std::string_view, bool>> category_option(const Term &option) {
    if (!option.is_atom()) {
        return std::nullopt;
    }
    auto name = option.atom_spelling_unchecked();
    bool enable = true;
    if (name.starts_with("warn_")) {
        name.remove_prefix(5);
    } else if (name.starts_with("nowarn_")) {
        name.remove_prefix(7);
        enable = false;
    } else {
        return std::nullopt;
    }
    const auto found = std::ranges::find(BOOL_OPTIONS, name, &std::pair<std::string_view, bool>::first);
    if (found == BOOL_OPTIONS.end()) {
        return std::nullopt;
    }
    return std::pair{found->first, enable};
}

// The byte length of the UTF-8 sequence a lead byte starts.
std::size_t utf8_length(unsigned char lead) {
    if (lead < 0x80) {
        return 1;
    }
    if (lead < 0xE0) {
        return 2;
    }
    return lead < 0xF0 ? 3 : 4;
}

// The code point of the `length` bytes at `at`; a truncated sequence decodes the bytes there are.
char32_t utf8_code(std::string_view text, std::size_t at, std::size_t length) {
    const auto lead = static_cast<unsigned char>(text[at]);
    char32_t code = length == 1 ? lead : lead & (0x7FU >> length);
    const auto end = std::min(at + length, text.size());
    for (std::size_t next = at + 1; next < end; ++next) {
        code = (code << 6U) | (static_cast<unsigned char>(text[next]) & 0x3FU);
    }
    return code;
}

// The code points of UTF-8 text.
std::u32string decode_utf8(std::string_view text) {
    std::u32string out;
    for (std::size_t at = 0; at < text.size();) {
        const auto length = utf8_length(static_cast<unsigned char>(text[at]));
        out.push_back(utf8_code(text, at, length));
        at += length;
    }
    return out;
}

// The text of an atom as code points.
std::u32string code_points(const Term &atom) { return decode_utf8(atom.atom_spelling_unchecked()); }

// The value of a features option, `true` for the bare atom, none for another option.
std::optional<Term> features_option(const Lint &lint, const Term &option) {
    if (is_option_pair(option, "features")) {
        return element(option, 1);
    }
    return is_atom(option, "features") ? std::optional{lint.atom("true"_atom)} : std::nullopt;
}

bool is_control(char32_t code) { return code < 0x20 || (code >= 0x7F && code < 0xA0); }

// The infos of one file of a packed list: [{File, [Info]}].
Term per_file(const Lint &lint, const std::vector<std::pair<Term, Term>> &infos, bool sort) {
    TermSet files;
    for (const auto &[file, info] : infos) {
        files.insert(file);
    }
    std::vector<Term> out;
    for (const auto &file : files) {
        std::vector<Term> mine;
        for (const auto &[from, info] : infos) {
            if (collections::compare(from, file) == 0) {
                mine.push_back(info);
            }
        }
        if (sort) {
            std::ranges::stable_sort(mine, TermLess{});
        }
        out.push_back(lint.pair(file, lint.build_.list(mine)));
    }
    return lint.build_.list(out);
}
} // namespace

Lint::Lint(ProcessContext &context, const Term &file, const CompileOptions &options)
    : build_(context), module_(build_.atom(""_atom)), compile_(options.list), on_load_anno_(build_.integer(0)),
      type_id_(build_.nil()), file_(file) {
    compile_list_ = forms::elements(options.list);
    for (const auto &[name, on] : BOOL_OPTIONS) {
        if (on) {
            enabled_.insert(name);
        }
    }
    for (const auto &option : compile_list_) {
        apply_warning_option(option);
    }
    read_bif_clash_and_features();
    add_pseudo_functions();
}

// Options of the warn_X and nowarn_X kind, and warn_format.
void Lint::apply_warning_option(const Term &option) {
    if (const auto category = category_option(option)) {
        if (category->second) {
            enabled_.insert(category->first);
        } else {
            enabled_.erase(category->first);
        }
    }
    if (is_atom(option, "warn_format")) {
        warn_format_ = 1;
    } else if (is_atom(option, "nowarn_format")) {
        warn_format_ = 0;
    } else if (is_option_pair(option, "warn_format")) {
        warn_format_ = string::small_of(element(option, 1)).value_or(warn_format_);
    }
}

// The nowarn_bif_clash functions and the features of the compile options.
void Lint::read_bif_clash_and_features() {
    std::optional<Term> features;
    TermSet nowarn;
    for (const auto &option : compile_list_) {
        if (is_option_pair(option, "nowarn_bif_clash")) {
            const auto listed = flatten(element(option, 1));
            nowarn.insert(listed.begin(), listed.end());
        }
        if (!features) {
            features = features_option(*this, option);
        }
    }
    nowarn_bif_clash_.assign(nowarn.begin(), nowarn.end());
    features_ = features && features->is_list() ? forms::elements(*features) : std::vector<Term>{};
}

// The functions every module has: module_info/0,1 exported and record_info/2.
void Lint::add_pseudo_functions() {
    const std::array pseudo{fa(atom("module_info"_atom), 0), fa(atom("module_info"_atom), 1),
                            fa(atom("record_info"_atom), 2)};
    exports_.insert(pseudo[0]);
    exports_.insert(pseudo[1]);
    defined_.insert(pseudo.begin(), pseudo.end());
    for (const auto &function : pseudo) {
        called_.emplace_back(function, build_.integer(0));
    }
    if (warn_enabled("unused_function")) {
        usage_.calls.emplace();
        usage_.calls->emplace(pseudo[1], std::vector<Term>(pseudo.begin(), pseudo.end()));
    }
}

// lists:flatten([Term]).
std::vector<Term> flatten(const Term &term) {
    std::vector<Term> out;
    std::vector<Term> pending{term};
    while (!pending.empty()) {
        const auto item = pending.back();
        pending.pop_back();
        if (!item.is_list()) {
            out.push_back(item);
            continue;
        }
        std::vector<Term> items;
        for (auto rest = item; rest.is_cons(); rest = rest.tail_unchecked()) {
            items.push_back(rest.head_unchecked());
        }
        pending.insert(pending.end(), items.rbegin(), items.rend());
    }
    return out;
}

// The value of attribute `name` when `form` is one.
std::optional<Term> attribute(const Term &form, std::string_view name) {
    if (forms::is_node(form, "attribute", 4) && forms::is_atom(forms::element(form, 2), name)) {
        return forms::element(form, 3);
    }
    return std::nullopt;
}

Term Lint::pair(const Term &first, const Term &second) const {
    const std::array items{first, second};
    return build_.tuple(items);
}

Term Lint::triple(const Term &first, const Term &second, const Term &third) const {
    const std::array items{first, second, third};
    return build_.tuple(items);
}

Term Lint::chars(std::string_view text) const {
    std::vector<Term> codes;
    for (const auto code : decode_utf8(text)) {
        codes.push_back(build_.integer(code));
    }
    return build_.list(codes);
}

Term Lint::fa(const Term &name, std::int64_t arity) const { return pair(name, build_.integer(arity)); }

bool Lint::warn_enabled(std::string_view category) const { return enabled_.contains(category); }

std::pair<Term, Term> Lint::loc(const Term &anno) const {
    const auto location = anno_location(build_.context(), anno);
    return {anno_item(anno, "file").value_or(file_), location};
}

Term Lint::set_file(const Term &anno, const Term &file) const { return anno_set(build_.context(), "file", file, anno); }

// A guard error inside a binary segment size reads as a size error.
Term Lint::bitsize_error(const Term &error) const {
    if (is_atom(error, "illegal_guard_expr")) {
        return atom("illegal_bitsize"_atom);
    }
    if (is_node(error, "illegal_guard_local_call", 2)) {
        return pair(atom("illegal_bitsize_local_call"_atom), element(error, 1));
    }
    return error;
}

void Lint::add_error(const AnnoTerm &report) {
    const auto reported = gexpr_context_ == GexprContext::bin_seg_size ? bitsize_error(report.term) : report.term;
    const auto [file, location] = loc(report.anno);
    errors_.emplace_back(file, triple(location, atom("erl_lint"_atom), reported));
}

void Lint::add_error_info(const Term &info) { errors_.emplace_back(file_, info); }

void Lint::add_warning(const AnnoTerm &report) {
    const auto [file, location] = loc(report.anno);
    warnings_.emplace_back(file, triple(location, atom("erl_lint"_atom), report.term));
}

void Lint::add_warning_info(const Term &info) { warnings_.emplace_back(file_, info); }

void Lint::maybe_add_warning(const Term &anno, const Term &warning) {
    const auto tag = warning.is_tuple() ? element(warning, 0) : warning;
    if (tag.is_atom() && warn_enabled(tag.atom_spelling_unchecked())) {
        add_warning({anno, warning});
    }
}

Snapshot Lint::snapshot() const {
    return {.errors = errors_.size(),
            .warnings = warnings_.size(),
            .called = called_.size(),
            .usage = usage_,
            .remote_self_calls = remote_self_calls_,
            .load_nif = load_nif_,
            .fun_used_vars = fun_used_vars_};
}

bool Lint::is_imported_from_erlang(const Term &key) const {
    const auto found = imports_.find(key);
    return found != imports_.end() && is_atom(found->second, "erlang");
}

bool Lint::bif_clash_disabled(const Term &key) const {
    return std::ranges::any_of(nowarn_bif_clash_, [&](const Term &entry) { return collections::same(entry, key); });
}

bool Lint::no_guard_bif_clash(const Term &key) const {
    const auto from_erlang = is_imported_from_erlang(key);
    return !is_local_function(key) && (!is_imported_function(key) || from_erlang) &&
           (!autoimport_suppressed(key) || from_erlang);
}

bool Lint::export_all() const {
    return std::ranges::any_of(compile_list_, [](const Term &option) { return is_atom(option, "export_all"); });
}

TermSet Lint::exports() const { return export_all() ? defined_ : exports_; }

void Lint::check_module_name(const AnnoTerm &module) {
    if (!module.term.is_atom()) {
        add_error({module.anno, atom("bad_module_name"_atom)});
        return;
    }
    const auto all = code_points(module.term);
    const auto visible =
        std::ranges::count_if(all, [](char32_t code) { return code != U' ' && code != 0xA0 && code != 0xAD; });
    if (all.empty()) {
        add_error({module.anno, atom("empty_module_name"_atom)});
    } else if (visible == 0) {
        add_error({module.anno, atom("blank_module_name"_atom)});
    } else {
        if (std::ranges::any_of(all, [](char32_t code) { return code > 0xFF; })) {
            add_error({module.anno, atom("non_latin1_module_unsupported"_atom)});
        }
        if (std::ranges::any_of(all, is_control)) {
            add_error({module.anno, atom("ctrl_chars_in_module_name"_atom)});
        }
    }
}

// Whether a candidate beats the best so far: more similar, or equally similar and later in atom order.
namespace {
bool better_candidate(const std::optional<std::pair<double, Term>> &best, double similarity, const Term &candidate) {
    if (!best || similarity > best->first) {
        return true;
    }
    return similarity == best->first && candidate.atom_spelling_unchecked() > best->second.atom_spelling_unchecked();
}
} // namespace

std::optional<Term> Lint::most_possible(const Term &name, const std::vector<Term> &names) const {
    const auto text = chars(name.atom_spelling_unchecked());
    std::optional<std::pair<double, Term>> best;
    for (const auto &candidate : names) {
        const auto similarity =
            string::jaro_similarity(build_.context(), text, chars(candidate.atom_spelling_unchecked()));
        if (better_candidate(best, similarity, candidate)) {
            best = std::pair{similarity, candidate};
        }
    }
    constexpr double SUFFICIENTLY_SIMILAR = 0.66;
    if (best && best->first > SUFFICIENTLY_SIMILAR) {
        return best->second;
    }
    return std::nullopt;
}

void Lint::keyword_warning(const Term &anno, const Term &name) {
    if (!warn_enabled("keywords") || !name.is_atom()) {
        return;
    }
    const auto text = anno_item(anno, "text");
    if (text && text->is_cons() && string::is_char(text->head_unchecked(), '\'')) {
        return;
    }
    const auto feature = feature_of_keyword(name.atom_spelling_unchecked());
    if (!feature.empty()) {
        add_warning({anno, triple(atom("future_feature"_atom), atom(feature), name)});
    }
}

void Lint::call_function(const AnnoTerm &callee, std::int64_t arity) {
    const auto called = fa(callee.term, arity);
    if (usage_.calls) {
        auto &callees = (*usage_.calls)[func_.value_or(build_.nil())];
        callees.insert(callees.begin(), called);
    }
    called_.emplace_back(called, set_file(callee.anno, file_));
}

void Lint::used_record(const Term &name) { usage_.used_records.insert(name); }

void Lint::used_type(const Term &type_pair, const Term &anno) {
    usage_.used_types[type_pair].push_back({.anno = set_file(anno, file_), .at = type_id_});
}

Term Lint::status() const {
    const auto warnings = per_file(*this, warnings_, true);
    if (errors_.empty()) {
        return pair(atom("ok"_atom), warnings);
    }
    return triple(atom("error"_atom), per_file(*this, errors_, false), warnings);
}
} // namespace clause::runtime::builtins::front_end::lint
