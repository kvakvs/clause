// Added for ASN.1 sources (plan 21 step LIB84).
#include "asn1_sources.hpp"
#include "../asn1/checker.hpp"
#include "../asn1/generate.hpp"
#include "../project/paths.hpp"
#include <fstream>
#include <functional>
#include <map>
#include <sstream>

namespace clause::cli {
namespace {
using Setter = std::function<void(asn1::CompileOptions &)>;

// The asn1ct options clau understands; `noobj`, `asn1config`, `verbose`, `warnings`, `errors` and `deterministic`
// change nothing here.
const std::map<std::string, Setter, std::less<>> &setters() {
    static const std::map<std::string, Setter, std::less<>> SETTERS{
        {"ber", [](auto &options) { options.rule_ = asn1::EncodingRule::ber; }},
        {"per", [](auto &options) { options.rule_ = asn1::EncodingRule::per; }},
        {"uper", [](auto &options) { options.rule_ = asn1::EncodingRule::uper; }},
        {"der", [](auto &options) { options.der_ = true; }},
        {"maps", [](auto &options) { options.maps_ = true; }},
        {"undec_rest", [](auto &options) { options.undec_rest_ = true; }},
        {"no_ok_wrapper", [](auto &options) { options.no_ok_wrapper_ = true; }},
        {"no_final_padding", [](auto &options) { options.no_final_padding_ = true; }},
        {"legacy_erlang_types", [](auto &options) { options.legacy_erlang_types_ = true; }},
        {"legacy_bit_string",
         [](auto &options) {
             options.bit_string_format_ = "legacy";
             options.legacy_erlang_types_ = true;
         }},
        {"compact_bit_string",
         [](auto &options) {
             options.bit_string_format_ = "compact";
             options.legacy_erlang_types_ = true;
         }},
        {"noobj", [](auto &) {}},
        {"asn1config", [](auto &) {}},
        {"verbose", [](auto &) {}},
        {"warnings", [](auto &) {}},
        {"errors", [](auto &) {}},
        {"deterministic", [](auto &) {}},
    };
    return SETTERS;
}

asn1::CompileOptions compile_options(const std::vector<std::string> &spelled) {
    asn1::CompileOptions options;
    for (const auto &option : spelled) {
        setters().find(option)->second(options);
        options.spelled_.push_back(asn1::atom_text(option));
    }
    if (options.der_) {
        options.rule_ = asn1::EncodingRule::ber;
    }
    return options;
}

std::string read_text(const std::filesystem::path &path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw asn1::Error({project::path_text(path), 0, 0}, "cannot read ASN.1 source");
    }
    std::ostringstream text;
    text << stream.rdbuf();
    return text.str();
}

std::vector<std::shared_ptr<asn1::ModuleSyntax>> parse(const std::filesystem::path &path) {
    return asn1::parse_file(read_text(path), project::path_text(path));
}

bool set_file(const std::filesystem::path &path) { return path.filename().string().ends_with(".set.asn"); }

std::optional<std::filesystem::path> set_member(const std::filesystem::path &path) {
    std::error_code error;
    for (const auto *extension : {"", ".asn1", ".asn", ".py"}) {
        auto candidate = path;
        candidate += extension;
        if (std::filesystem::is_regular_file(candidate, error)) {
            return candidate;
        }
    }
    return std::nullopt;
}

// The modules a .set.asn lists, one file name per line, relative to the set file.
std::vector<std::shared_ptr<asn1::ModuleSyntax>> set_modules(const std::filesystem::path &path) {
    std::istringstream lines(read_text(path));
    std::vector<std::shared_ptr<asn1::ModuleSyntax>> modules;
    for (std::string line; std::getline(lines, line);) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) {
            line.pop_back();
        }
        // asn1ct reads each line as a file name, with or without its extension, and passes over other lines.
        if (const auto file = set_member(path.parent_path() / project::native_path(line))) {
            modules.push_back(parse(*file).front());
        }
    }
    return modules;
}

// Finds Name.asn1, Name.asn or Name.py in the source's directory, then the include directories (asn1ct's order).
asn1::ModuleLoader loader(const std::filesystem::path &source, const Asn1Settings &settings) {
    std::vector<std::filesystem::path> directories{source.parent_path()};
    directories.insert(directories.end(), settings.include_dirs_.begin(), settings.include_dirs_.end());
    return [directories](const std::string &name) -> std::shared_ptr<asn1::ModuleSyntax> {
        for (const auto &directory : directories) {
            for (const auto *extension : {".asn1", ".asn", ".py"}) {
                const auto candidate = directory / project::native_path(name + extension);
                std::error_code error;
                if (std::filesystem::is_regular_file(candidate, error)) {
                    return parse(candidate).front();
                }
            }
        }
        return nullptr;
    };
}

void write_file(const std::filesystem::path &path, const std::string &text) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream << text;
    if (!stream) {
        throw asn1::Error({project::path_text(path), 0, 0}, "cannot write generated file");
    }
}

// Compile one source; returns the generated .erl file.
std::filesystem::path compile(const std::filesystem::path &source, const Asn1Settings &settings) {
    const auto options = compile_options(settings.options_);
    std::vector<std::shared_ptr<asn1::ModuleSyntax>> modules;
    std::string name;
    if (set_file(source)) {
        modules = set_modules(source);
        name = source.filename().string();
        name.resize(name.size() - std::string_view(".set.asn").size());
    } else {
        modules = parse(source);
        name = modules.front()->name_;
    }
    const auto compiled = asn1::check(modules, name, options, loader(source, settings));
    const auto generated = asn1::generate(compiled, options);
    std::filesystem::create_directories(settings.outdir_);
    const auto erl = settings.outdir_ / project::native_path(name + ".erl");
    write_file(erl, generated.erl_);
    if (!generated.hrl_.empty()) {
        write_file(settings.outdir_ / project::native_path(name + ".hrl"), generated.hrl_);
    }
    return erl;
}
} // namespace

bool asn1_source(const std::filesystem::path &path) {
    const auto extension = path.extension();
    return extension == ".asn" || extension == ".asn1";
}

std::optional<std::string> check_asn1_options(const std::vector<std::string> &options) {
    for (const auto &option : options) {
        if (option == "jer") {
            return "the ASN.1 option jer (JSON encoding rules) is not supported";
        }
        if (!setters().contains(option)) {
            return "unknown ASN.1 option '" + option + "'";
        }
    }
    return std::nullopt;
}

std::optional<std::vector<std::filesystem::path>> generate_asn1(const std::vector<std::filesystem::path> &sources,
                                                                const Asn1Settings &settings,
                                                                const DiagnosticSink &sink) {
    std::vector<std::filesystem::path> generated;
    for (const auto &source : sources) {
        try {
            generated.push_back(compile(source, settings));
        } catch (const asn1::Error &error) {
            sink(error.what());
            return std::nullopt;
        }
    }
    return generated;
}
} // namespace clause::cli
