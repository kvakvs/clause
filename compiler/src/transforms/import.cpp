// Added for parse transforms: abstract forms to located tokens, parsed by Clause's parser.
#include "import_writer.hpp"
#include "term_text.hpp"
#include <array>
#include <charconv>

namespace clause::transforms {
namespace {
// Deepest expression or type nesting the importer follows, above the parser's own limit.
constexpr std::size_t MAX_DEPTH = 512;

// A diagnostic at a place; a place without a real file points at a small source naming its logical file.
Diagnostic diagnostic(const Place &place, std::string message, const Severity severity, SourceManager &sources) {
    Diagnostic result;
    result.code = severity == Severity::error ? DiagnosticCode::parser_syntax : DiagnosticCode::user_warning;
    result.message = std::move(message);
    result.primary = place.real_.source ? place.real_ : Span{sources.add(place.location_.file, ""), 0, 0};
    result.severity = severity;
    result.location = place.location_;
    return result;
}

// The module named by an {error, {Location, Module, Descriptor}} or {warning, ...} form's info, else none.
std::u32string reporter(const Terms &terms, const TermId info) {
    const auto &parts = terms.node(info).children_;
    return parts.size() == 3 && terms.node(parts[1]).kind_ == TermKind::atom ? terms.node(parts[1]).atom_
                                                                             : std::u32string{};
}

// The message of an {error, {Location, Module, Descriptor}} or {warning, ...} form: the loader's formatted text
// (module clause_message, plan 21 LIB77D), else the descriptor's term text.
std::string reported(const Terms &terms, const TermId info) {
    const auto &parts = terms.node(info).children_;
    if (reporter(terms, info) == U"clause_message") {
        if (const auto message = terms.text(parts[2])) {
            return utf8(*message);
        }
    }
    std::string text;
    write_text(text, terms, parts.size() == 3 ? parts[2] : info);
    const auto module = parts.size() == 3 && terms.node(parts[1]).kind_ == TermKind::atom
                            ? utf8(terms.node(parts[1]).atom_)
                            : std::string("unknown");
    return "reported by " + module + ": " + text;
}
} // namespace

namespace {
// A location written as Line or {Line, Column}.
std::optional<std::pair<std::int64_t, std::int64_t>> plain_position(const Terms &terms, const TermId anno) {
    if (const auto line = terms.small_integer(anno)) {
        return std::pair<std::int64_t, std::int64_t>{*line, 0};
    }
    const auto &node = terms.node(anno);
    if (node.kind_ != TermKind::tuple || node.children_.size() != 2) {
        return std::nullopt;
    }
    const auto line = terms.small_integer(node.children_[0]);
    const auto column = terms.small_integer(node.children_[1]);
    if (!line || !column) {
        return std::nullopt;
    }
    return std::pair<std::int64_t, std::int64_t>{*line, *column};
}

// The value of a {Key, Value} property of an annotation list.
std::optional<TermId> property(const Terms &terms, const TermNode &list, const std::u32string_view key) {
    for (const auto item : list.children_) {
        const auto &pair = terms.node(item);
        if (pair.kind_ == TermKind::tuple && pair.children_.size() == 2 && terms.is_atom(pair.children_[0], key)) {
            return pair.children_[1];
        }
    }
    return std::nullopt;
}

// The position of an annotation: a plain location, or the location property of an annotation list.
std::optional<std::pair<std::int64_t, std::int64_t>> annotation_position(const Terms &terms, const TermId anno) {
    if (auto position = plain_position(terms, anno)) {
        return position;
    }
    const auto &node = terms.node(anno);
    if (node.kind_ != TermKind::list) {
        return std::nullopt;
    }
    const auto location = property(terms, node, U"location");
    return location ? plain_position(terms, *location) : std::nullopt;
}
} // namespace

// ---- tokens -----------------------------------------------------------------------------------------------------

void TokenWriter::add(const TokenKind kind, TokenValue value, const std::u32string_view spelling, const Place &at) {
    if (!text_.empty()) {
        text_.push_back(U' ');
    }
    const auto begin = text_.size();
    text_ += spelling;
    pending_.push_back(
        {.kind_ = kind, .value_ = std::move(value), .begin_ = begin, .end_ = text_.size(), .place_ = at});
}

void TokenWriter::integer(const Integer &value, const Place &at) {
    const std::u32string digits(value.decimal.begin(), value.decimal.end());
    add(TokenKind::integer, value, digits, at);
}

void TokenWriter::floating(const double value, const Place &at) {
    std::array<char, 64> buffer{};
    const auto end = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value).ptr;
    const std::u32string spelling(buffer.data(), end);
    add(TokenKind::floating, value, spelling, at);
}

void TokenWriter::character(const std::int64_t code, const Place &at) {
    add(TokenKind::character, Integer{std::to_string(code)}, U"$?", at);
}

void TokenWriter::string(const std::u32string_view text, const Place &at) {
    add(TokenKind::string, std::u32string(text), U"\"" + std::u32string(text) + U"\"", at);
}

TokenWriter::Form TokenWriter::finish(const Place &at, SourceManager &sources, const std::string &name,
                                      const std::optional<bool> marker) {
    add(TokenKind::dot, std::u32string(U"."), U".", at);
    const auto source = sources.add(name, utf8(text_));
    const auto related = marker ? std::optional{marker_span(source, *marker)} : std::nullopt;
    Form result;
    for (auto &token : pending_) {
        std::vector<Span> origins;
        if (related) {
            origins.push_back(*related);
        } else if (token.place_.real_.source) {
            origins.push_back(token.place_.real_);
        }
        result.tokens_.push_back({token.kind_, std::move(token.value_), Span{source, token.begin_, token.end_},
                                  token.place_.location_, std::move(origins)});
    }
    result.dot_ = result.tokens_.back();
    pending_.clear();
    text_.clear();
    return result;
}

Span TokenWriter::marker_span(const SourcePtr &source, const bool explicit_file) const {
    const auto file = std::ranges::find_if(pending_, [](const Pending &token) {
        return token.kind_ == TokenKind::atom && std::get<std::u32string>(token.value_) == U"file";
    });
    if (!explicit_file || file == pending_.end()) {
        return {source, 0, 0};
    }
    return {source, file->begin_, file->end_};
}

// ---- annotations ------------------------------------------------------------------------------------------------

const Locator::Lines &Locator::lines(const std::string &file) {
    if (const auto found = sources_.find(file); found != sources_.end()) {
        return found->second;
    }
    Lines lines{.source_ = options_.source_ ? options_.source_(file) : nullptr, .starts_ = {0}};
    const auto text = lines.source_ ? std::u32string_view(lines.source_->text) : std::u32string_view();
    for (std::size_t at = 0; at < text.size(); ++at) {
        if (text[at] == U'\n') {
            lines.starts_.push_back(at + 1);
        }
    }
    return sources_.emplace(file, std::move(lines)).first->second;
}

Span Locator::real(const std::string &file, const std::size_t line, const std::size_t column) {
    const auto &lines = this->lines(file);
    if (!lines.source_ || line == 0 || line > lines.starts_.size()) {
        return {};
    }
    const auto size = lines.source_->text.size();
    const auto offset = std::min(lines.starts_[line - 1] + (column > 0 ? column - 1 : 0), size);
    return {lines.source_, offset, std::min(offset + 1, size)};
}

std::optional<Place> Locator::find(const Terms &terms, const TermId anno) {
    const auto position = annotation_position(terms, anno);
    auto file = file_;
    const auto &node = terms.node(anno);
    if (node.kind_ == TermKind::list) {
        const auto name = property(terms, node, U"file");
        if (const auto text = name ? terms.text(*name) : std::nullopt) {
            file = utf8(*text);
        }
    }
    if (!position || position->first < 1 || position->second < 0) {
        return std::nullopt;
    }
    const auto line = static_cast<std::size_t>(position->first);
    const auto column = static_cast<std::size_t>(position->second);
    return Place{.location_ = {.file = file, .line = line, .column = column}, .real_ = real(file, line, column)};
}

Place Locator::place(const Terms &terms, const TermId anno, const Place &fallback) {
    if (auto found = find(terms, anno)) {
        return std::move(*found);
    }
    // Added for qlc: line 0, generated code without a source line (qlc's transform), takes the place before it.
    if (const auto position = annotation_position(terms, anno);
        position && position->first == 0 && position->second >= 0) {
        return fallback;
    }
    throw ImportError(fallback, "invalid annotation");
}

// ---- shapes -----------------------------------------------------------------------------------------------------

std::u32string_view FormImporter::tag(const TermId id) const {
    const auto &node = terms_.node(id);
    if (node.kind_ != TermKind::tuple || node.children_.empty()) {
        return {};
    }
    const auto &first = terms_.node(node.children_[0]);
    return first.kind_ == TermKind::atom ? std::u32string_view(first.atom_) : std::u32string_view();
}

void FormImporter::malformed(const TermId id) const { error("malformed " + utf8(tag(id)) + " node"); }

void FormImporter::error(const std::string &message) const { throw ImportError(last_, message); }

const std::vector<TermId> &FormImporter::items(const TermId list) const {
    const auto &node = terms_.node(list);
    if (node.kind_ != TermKind::list || node.improper_) {
        error("expected a list");
    }
    return node.children_;
}

std::u32string_view FormImporter::atom(const TermId id) const {
    const auto &node = terms_.node(id);
    if (node.kind_ != TermKind::atom) {
        error("expected an atom");
    }
    return node.atom_;
}

Place FormImporter::place(const TermId node) {
    const auto &parts = terms_.node(node).children_;
    if (terms_.node(node).kind_ != TermKind::tuple || parts.size() < 2) {
        error("expected an annotated node");
    }
    last_ = locator_.place(terms_, parts[1], last_);
    return last_;
}

void FormImporter::fail(const TermId node, const std::string &message) {
    const auto &parts = terms_.node(node).children_;
    if (terms_.node(node).kind_ == TermKind::tuple && parts.size() >= 2) {
        last_ = locator_.find(terms_, parts[1]).value_or(last_);
    }
    error(message);
}

// ---- shared syntax ----------------------------------------------------------------------------------------------

void FormImporter::expression(const TermId id, const int minimum) {
    if (++depth_ > MAX_DEPTH) {
        fail(id, "expression nesting too deep");
    }
    if (precedence(*this, id) < minimum) {
        const auto at = place(id);
        writer_.symbol(U"(", at);
        import_expression(*this, id);
        writer_.symbol(U")", at);
    } else {
        import_expression(*this, id);
    }
    --depth_;
}

void FormImporter::sequence(const TermId list, const std::u32string_view separator) {
    const auto &elements = items(list);
    for (std::size_t index = 0; index < elements.size(); ++index) {
        if (index != 0) {
            writer_.symbol(separator, last_);
        }
        expression(elements[index]);
    }
}

void FormImporter::guard(const TermId guard) {
    const auto &alternatives = items(guard);
    for (std::size_t index = 0; index < alternatives.size(); ++index) {
        if (index != 0) {
            writer_.symbol(U";", last_);
        }
        sequence(alternatives[index]);
    }
}

void FormImporter::clause_rest(const TermId clause) {
    const auto &parts = children<5>(clause);
    const auto at = place(clause);
    if (!items(parts[3]).empty()) {
        writer_.keyword(U"when", at);
        guard(parts[3]);
    }
    writer_.symbol(U"->", at);
    sequence(parts[4]);
}

void FormImporter::clauses(const TermId list, const bool parenthesized) {
    const auto &elements = items(list);
    for (std::size_t index = 0; index < elements.size(); ++index) {
        const auto clause = elements[index];
        if (tag(clause) != U"clause") {
            fail(clause, "expected a clause");
        }
        const auto at = place(clause);
        if (index != 0) {
            writer_.symbol(U";", at);
        }
        const auto &patterns = children<5>(clause)[2];
        if (parenthesized) {
            writer_.symbol(U"(", at);
            sequence(patterns);
            writer_.symbol(U")", last_);
        } else if (items(patterns).size() == 1) {
            expression(items(patterns).front());
        } else {
            fail(clause, "expected one pattern in a clause");
        }
        clause_rest(clause);
    }
}

void FormImporter::type(const TermId id, const bool top) {
    if (++depth_ > MAX_DEPTH) {
        fail(id, "type nesting too deep");
    }
    const auto kind = tag(id);
    const bool loose = kind == U"ann_type" || (kind == U"type" && terms_.node(id).children_.size() == 4 &&
                                               terms_.is_atom(terms_.node(id).children_[2], U"union"));
    if (loose && !top) {
        const auto at = place(id);
        writer_.symbol(U"(", at);
        import_type(*this, id);
        writer_.symbol(U")", at);
    } else {
        import_type(*this, id);
    }
    --depth_;
}

void FormImporter::types(const TermId list) {
    const auto &elements = items(list);
    for (std::size_t index = 0; index < elements.size(); ++index) {
        if (index != 0) {
            writer_.symbol(U",", last_);
        }
        type(elements[index]);
    }
}

// ---- forms ------------------------------------------------------------------------------------------------------

namespace {
// The tokens of a function or attribute form; file attributes also switch the importer's file.
TokenWriter::Form import_form(FormImporter &importer, const TermId form, SourceManager &sources) {
    const auto kind = importer.tag(form);
    std::optional<bool> marker;
    if (kind == U"function") {
        import_function(importer, form);
    } else if (kind == U"attribute") {
        marker = import_attribute(importer, form);
    } else {
        importer.fail(form, "unknown form");
    }
    const auto at = importer.place(form);
    return importer.writer().finish(at, sources, importer.locator().file(), marker);
}
} // namespace

namespace {
// Imports forms one at a time into a parser session, collecting diagnostics and the eof location.
class FormsImport {
  public:
    FormsImport(const Terms &terms, const ImportOptions &options, const ParserSession &parser)
        : terms_(terms), options_(options), parser_(parser), locator_(options) {}

    // Import one form; false after the eof form.
    bool step(const TermId form) {
        TokenWriter writer;
        FormImporter importer(terms_, locator_, writer);
        try {
            return dispatch(importer, form);
        } catch (const ImportError &error) {
            report(error.place_, error.what(), Severity::error);
        } catch (const TermError &error) {
            report({}, error.what(), Severity::error);
        }
        return true;
    }

    ImportResult take() { return std::move(result_); }

  private:
    // The forms' arena, options and target parser; generated sources, annotation decoder and what was found.
    const Terms &terms_;
    const ImportOptions &options_;
    const ParserSession &parser_;
    SourceManager sources_;
    Locator locator_;
    ImportResult result_;

    void report(const Place &at, std::string message, const Severity severity) {
        result_.diagnostics_.push_back(diagnostic(at, std::move(message), severity, sources_));
    }

    // The eof form ends the import; error and warning forms are reported; others are parsed.
    bool dispatch(FormImporter &importer, const TermId form) {
        const auto kind = importer.tag(form);
        if (kind == U"eof") {
            const auto end = locator_.place(terms_, importer.children<2>(form)[1], {}).location_;
            result_.eof_ = Position{.byte = 0, .line = end.line, .column = end.column};
            return false;
        }
        if (kind == U"error" || kind == U"warning") {
            const auto &info = importer.children<2>(form)[1];
            // Clause's own preprocessor and parse problems, which clau reports itself (plan 21 LIB77D).
            if (reporter(terms_, info) == U"clause_source") {
                return true;
            }
            const auto &parts = terms_.node(info).children_;
            const auto at = parts.size() == 3 ? locator_.place(terms_, parts[0], {}) : Place{};
            report(at, reported(terms_, info), kind == U"error" ? Severity::error : Severity::warning);
            return true;
        }
        const auto parsed = import_form(importer, form, sources_);
        parser_.parse_form(parsed.tokens_, parsed.dot_, options_.features_);
        return true;
    }
};
} // namespace

ImportResult import_forms(const Terms &terms, const std::span<const TermId> forms, const ImportOptions &options,
                          const ParserSession &parser) {
    FormsImport run(terms, options, parser);
    for (const auto form : forms) {
        if (!run.step(form)) {
            break;
        }
    }
    return run.take();
}
} // namespace clause::transforms
