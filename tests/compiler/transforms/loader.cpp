// Added for parse transforms: the loader on the host Erlang/OTP with real transform modules and every reply shape.
#include "transforms/loader.hpp"
#include "transforms/export.hpp"
#include <algorithm>
#include <clause/compiler/parser.hpp>
#include <iostream>

using namespace clause;
using namespace clause::transforms;

namespace {
void require(const bool condition, const std::string &message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

// The fixture module subject.erl as abstract forms.
AbstractModule subject(const std::filesystem::path &fixtures, SourceManager &sources) {
    const auto source = sources.read(fixtures / "subject.erl");
    PreprocessorOptions options;
    options.working_directory = fixtures;
    PreprocessorSession session(source, options);
    const auto parsed = parse_module(session);
    require(parsed.succeeded(), "subject.erl does not parse");
    const auto &syntax = parsed.module;
    return export_module(syntax, syntax.forms(), end_of(syntax, syntax.forms(), *source));
}

// A request for `transforms`, compiling the named fixture transform sources in the loader.
TransformRequest request(const std::filesystem::path &erl, const std::filesystem::path &fixtures,
                         const std::vector<std::u32string> &transforms, const std::vector<std::string> &sources) {
    TransformRequest result;
    result.erl_ = erl;
    result.transforms_ = transforms;
    result.file_ = "subject.erl";
    for (const auto &name : sources) {
        result.sources_.push_back({.path_ = fixtures / (name + ".erl"), .options_ = {}});
    }
    return result;
}

bool contains(const std::vector<TransformMessage> &messages, const std::string &text) {
    return std::ranges::any_of(messages,
                               [&](const auto &message) { return message.text_.find(text) != std::string::npos; });
}

void check_shapes(const std::filesystem::path &erl, const std::filesystem::path &fixtures,
                  const AbstractModule &module) {
    const auto same = run_transforms(module, request(erl, fixtures, {U"pt_identity"}, {"pt_identity"}));
    require(same.ok_ && same.forms_.size() == module.forms_.size(), "pt_identity changed the module");
    for (std::size_t index = 0; index < same.forms_.size(); ++index) {
        require(equal(module.terms_, module.forms_[index], same.terms_, same.forms_[index]),
                "pt_identity changed a form");
    }
    const auto warned =
        run_transforms(module, request(erl, fixtures, {U"pt_identity", U"pt_warning"}, {"pt_identity", "pt_warning"}));
    require(warned.ok_ && contains(warned.warnings_, "pt_warning says hello"), "warning reply");
    require(warned.warnings_.front().location_ == std::pair<std::size_t, std::size_t>{1, 2}, "warning location");
    const auto refused = run_transforms(module, request(erl, fixtures, {U"pt_error"}, {"pt_error"}));
    require(!refused.ok_ && contains(refused.errors_, "pt_error refuses"), "error reply");
    const auto crashed = run_transforms(module, request(erl, fixtures, {U"pt_crash"}, {"pt_crash"}));
    require(!crashed.ok_ && contains(crashed.errors_, "error in parse transform 'pt_crash'"), "crash reply");
    const auto missing = run_transforms(module, request(erl, fixtures, {U"no_such_transform"}, {}));
    require(!missing.ok_ && contains(missing.errors_, "undefined parse transform 'no_such_transform'"), "undefined");
}

void check_columns_and_sources(const std::filesystem::path &erl, const std::filesystem::path &fixtures,
                               const AbstractModule &module) {
    const auto lines = run_transforms(module, request(erl, fixtures, {U"pt_lines"}, {"pt_lines"}));
    require(lines.ok_, "pt_lines failed");
    const auto &first = lines.terms_.node(lines.forms_.front()).children_;
    require(lines.terms_.small_integer(first.at(1)).has_value(), "columns were not stripped for pt_lines");
    const auto broken = run_transforms(module, request(erl, fixtures, {U"pt_broken"}, {"pt_broken"}));
    require(!broken.ok_ && contains(broken.errors_, "precompile it into a .beam"), "broken source hint");
    // A source of a sticky host module (kernel, stdlib, compiler) is skipped: the host's own module is used.
    const auto sticky =
        run_transforms(module, request(erl, fixtures, {U"pt_identity"}, {"sticky/lists", "pt_identity"}));
    require(sticky.ok_, "a source of a sticky host module was compiled or reloaded");
    try {
        find_erl(fixtures / "no-such-erl.exe");
        require(false, "a missing explicit erl was accepted");
    } catch (const LoaderError &) {
    }
}
} // namespace

// transforms_loader ERL|- FIXTURES
int main(const int argc, char **argv) {
    try {
        require(argc == 3, "usage: transforms_loader <erl or -> <fixtures>");
        if (std::string_view(argv[1]) == "-") {
            std::cout << "SKIP: no host Erlang/OTP\n";
            return 0;
        }
        const std::filesystem::path fixtures = argv[2];
        const auto erl = find_erl(std::filesystem::path(argv[1]));
        SourceManager sources;
        const auto module = subject(fixtures, sources);
        check_shapes(*erl, fixtures, module);
        check_columns_and_sources(*erl, fixtures, module);
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "transforms_loader: ok\n";
    return 0;
}
