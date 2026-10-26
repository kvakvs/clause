#include "decode.hpp"
#include "decode_options.hpp"
#include "entry.hpp"
#include "schema.hpp"
#include <algorithm>
#include <set>

namespace clause::project {
namespace {
// Recognize the portable first character of a target name without locale rules.
bool initial(const char value) {
    return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z') || (value >= '0' && value <= '9') ||
           value == '_';
}

// Allow dots and hyphens only after the initial target-name character.
bool continuation(const char value) { return initial(value) || value == '.' || value == '-'; }

// Decode the optional entry key, rejecting spellings that cannot name an Erlang function.
std::optional<Text> entry(const toml::table &values, const schema::Context &context) {
    const auto *node = values.get("entry");
    if (!node) {
        return std::nullopt;
    }
    auto result = schema::text(*node, context, "entry");
    if (!parse_entry(result.value)) {
        fail(result.site, "invalid entry; expected MODULE[:FUNCTION[/ARITY]] with ARITY 0 or 1");
    }
    return result;
}

// `[[targets.asn1]]`: groups of ASN.1 specifications with their asn1ct options and output directory. Added for
// ASN.1 sources.
std::vector<Asn1Group> asn1_groups(const toml::node &node, const schema::Context &context) {
    const auto *array = node.as_array();
    if (array == nullptr) {
        fail(schema::site(node, context, "asn1"), "expected an array of tables");
    }
    std::vector<Asn1Group> result;
    for (const auto &item : *array) {
        const auto &values = schema::table(item, context, "asn1");
        schema::keys(values, {"sources", "options", "outdir"}, context);
        Asn1Group group;
        group.sources = schema::strings(values, "sources", context);
        if (group.sources.empty()) {
            fail(schema::site(item, context, "asn1"), "an asn1 group requires sources");
        }
        group.options = schema::strings(values, "options", context);
        if (const auto *outdir = values.get("outdir")) {
            group.outdir = schema::text(*outdir, context, "outdir");
        }
        result.push_back(std::move(group));
    }
    return result;
}

// Decode one target's source declarations; options are decoded in their own module.
Target target(const toml::node &node, schema::Context context) {
    const auto &values = schema::table(node, context, "targets");
    schema::keys(values, {"name", "sources", "source_dirs", "output", "entry", "build_dir", "options", "asn1"},
                 context);
    const auto *name = values.get("name");
    if (!name) {
        fail(schema::site(node, context, "name"), "missing target name");
    }
    Target result;
    result.name = schema::text(*name, context, "name");
    if (!initial(result.name.value.front()) || !std::ranges::all_of(result.name.value, continuation)) {
        fail(result.name.site, "invalid target name");
    }
    context.target = result.name.value;
    result.name.site.target = context.target;
    result.sources = schema::strings(values, "sources", context);
    result.source_dirs = schema::strings(values, "source_dirs", context);
    if (result.sources.empty() && result.source_dirs.empty()) {
        fail(result.name.site, "target requires sources or source_dirs");
    }
    if (const auto *output = values.get("output")) {
        result.output = schema::text(*output, context, "output");
    }
    result.entry = entry(values, context);
    if (const auto *build = values.get("build_dir")) {
        result.build_dir_ = schema::text(*build, context, "build_dir");
    }
    if (const auto *options = values.get("options")) {
        result.options = decode_options(*options, context);
    }
    if (const auto *asn1 = values.get("asn1")) {
        result.asn1_ = asn1_groups(*asn1, context);
    }
    return result;
}

// Require the version before interpreting any target fields.
const toml::array &targets(const Document &document, const Limits &limits, const schema::Context &context) {
    schema::keys(document.table, {"schema_version", "targets"}, context);
    const auto *version = document.table.get("schema_version");
    if (!version || !version->is_integer() || version->value<std::int64_t>() != 1) {
        fail(schema::site(document.table, context, "schema_version"), "expected schema_version = 1");
    }
    const auto *array = document.table["targets"].as_array();
    if (!array || array->empty()) {
        fail(schema::site(document.table, context, "targets"), "expected nonempty targets array");
    }
    if (array->size() > limits.targets) {
        fail(schema::site(*array, context, "targets"), "target count limit exceeded");
    }
    return *array;
}
} // namespace

Manifest decode(const Document &document, const Limits &limits) {
    const schema::Context context{document.file, {}};
    const auto &array = targets(document, limits, context);
    auto remaining = limits.entries;
    schema::budget(document.table, remaining, context);
    Manifest result{document.file, {}};
    result.targets.reserve(array.size());
    std::set<std::string> names;
    for (const auto &node : array) {
        auto value = target(node, context);
        if (!names.insert(value.name.value).second) {
            fail(value.name.site, "duplicate target name");
        }
        result.targets.push_back(std::move(value));
    }
    return result;
}
} // namespace clause::project
