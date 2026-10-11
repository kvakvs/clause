// Added for parse transforms: the loader request and reply files and the host erl process.
#include "loader.hpp"
#include "etf.hpp"
#include "term_text.hpp"
#include "transforms/loader_source.hpp"
#include <clause/abi/external_term.hpp>
#include <fstream>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/Process.h>
#include <llvm/Support/Program.h>
#include <sstream>

namespace clause::transforms {
namespace {
namespace fs = std::filesystem;

// Wall-clock limit of one loader run, and how much of its output reaches diagnostics.
constexpr unsigned TIME_LIMIT_SECONDS = 600;
constexpr std::size_t OUTPUT_LIMIT = std::size_t{64} * 1024;
// erl evaluates this with the request directory as its only plain argument: compile the loader, run it.
constexpr std::string_view BOOT = "[D]=init:get_plain_arguments(),S=filename:join(D,'clause_transform_loader.erl'),"
                                  "{ok,M,B}=compile:file(S,[binary,report]),{module,M}=code:load_binary(M,S,B),"
                                  "M:main(D).";

std::string text(const fs::path &path) {
    const auto bytes = path.generic_u8string();
    return {bytes.begin(), bytes.end()};
}

std::u32string wide(const std::string &utf8_text) { return Source(0, "text", utf8_text).text; }

// A removed-on-exit temporary directory for one run.
class Workspace {
  public:
    Workspace() {
        llvm::SmallString<128> created;
        if (const auto error = llvm::sys::fs::createUniqueDirectory("clause-transform", created)) {
            throw LoaderError("cannot create a temporary directory: " + error.message());
        }
        path_ = fs::path(std::string(created.str()));
    }

    Workspace(const Workspace &) = delete;
    Workspace &operator=(const Workspace &) = delete;

    ~Workspace() {
        std::error_code ignored;
        fs::remove_all(path_, ignored);
    }

    const fs::path &path() const { return path_; }

  private:
    // The directory holding the loader source, request, reply and output.
    fs::path path_;
};

void write_file(const fs::path &path, const std::string_view bytes) {
    std::ofstream file(path, std::ios::binary);
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!file) {
        throw LoaderError("cannot write " + text(path));
    }
}

std::optional<std::string> read_file(const fs::path &path, const std::size_t limit = SIZE_MAX) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    std::ostringstream content;
    content << file.rdbuf();
    auto result = content.str();
    if (result.size() > limit) {
        result.resize(limit);
    }
    return result;
}

// {d, Name} or {d, Name, Value} of a -D definition; a value that is no single term is left out.
TermId define(Terms &terms, const std::string &definition) {
    const auto equals = definition.find('=');
    const auto name = terms.atom(wide(definition.substr(0, equals)));
    if (equals == std::string::npos) {
        return terms.tuple({terms.atom(U"d"), name});
    }
    std::vector<TermId> values;
    try {
        SourceManager sources;
        values = read_text(sources.add("define", definition.substr(equals + 1) + ".\n"), terms);
    } catch (const TermError &) {
        values.clear();
    }
    if (values.size() != 1) {
        return terms.tuple({terms.atom(U"d"), name});
    }
    return terms.tuple({terms.atom(U"d"), name, values.front()});
}

// The option list erlc would pass: features, reporting, directories, defines, command-line transforms.
TermId options_term(Terms &terms, const CompileOptions &options) {
    std::vector<TermId> features;
    features.reserve(options.features_.size());
    for (const auto &feature : options.features_) {
        features.push_back(terms.atom(wide(feature)));
    }
    std::error_code error;
    const auto cwd = terms.string(wide(text(fs::current_path(error))));
    std::vector<TermId> result{terms.tuple({terms.atom(U"features"), terms.list(std::move(features))}),
                               terms.atom(U"report_warnings"), terms.atom(U"report_errors"),
                               terms.tuple({terms.atom(U"cwd"), cwd}), terms.tuple({terms.atom(U"outdir"), cwd})};
    for (const auto &include : options.includes_) {
        result.push_back(terms.tuple({terms.atom(U"i"), terms.string(wide(text(include)))}));
    }
    for (const auto &definition : options.defines_) {
        result.push_back(define(terms, definition));
    }
    for (const auto &transform : options.transforms_) {
        result.push_back(terms.tuple({terms.atom(U"parse_transform"), terms.atom(transform)}));
    }
    return terms.list(std::move(result));
}

// The request map: #{forms, options, transforms, file, sources}; forms are spliced from the module's own arena.
std::string request_bytes(const AbstractModule &module, const TransformRequest &request) {
    Terms terms;
    std::vector<TermId> transforms;
    transforms.reserve(request.transforms_.size());
    for (const auto &name : request.transforms_) {
        transforms.push_back(terms.atom(name));
    }
    std::vector<TermId> sources;
    sources.reserve(request.sources_.size());
    for (const auto &source : request.sources_) {
        sources.push_back(terms.tuple({terms.string(wide(text(source.path_))), options_term(terms, source.options_)}));
    }
    const auto body = [&terms](const TermId id) { return encode_external(terms, id).substr(1); };
    std::string out;
    abi::external::Writer writer(out);
    writer.version();
    writer.map(5);
    writer.atom("forms");
    writer.list(module.forms_.size());
    for (const auto form : module.forms_) {
        out += encode_external(module.terms_, form).substr(1);
    }
    writer.nil();
    writer.atom("options");
    out += body(options_term(terms, request.options_));
    writer.atom("transforms");
    out += body(terms.list(std::move(transforms)));
    writer.atom("file");
    out += body(terms.string(wide(request.file_)));
    writer.atom("sources");
    out += body(terms.list(std::move(sources)));
    return out;
}

// [{File, Location, Text}] of a reply.
std::vector<TransformMessage> messages(const Terms &terms, const TermId list) {
    std::vector<TransformMessage> result;
    result.reserve(terms.node(list).children_.size());
    for (const auto item : terms.node(list).children_) {
        const auto &parts = terms.node(item).children_;
        if (parts.size() != 3) {
            throw LoaderError("malformed message in the loader reply");
        }
        TransformMessage message;
        message.file_ = utf8(terms.text(parts[0]).value_or(terms.node(parts[0]).atom_));
        message.text_ = utf8(terms.text(parts[2]).value_or(U"?"));
        const auto &location = terms.node(parts[1]);
        if (const auto line = terms.small_integer(parts[1])) {
            message.location_ = {{static_cast<std::size_t>(*line), 0}};
        } else if (location.kind_ == TermKind::tuple && location.children_.size() == 2) {
            message.location_ = {{static_cast<std::size_t>(terms.small_integer(location.children_[0]).value_or(0)),
                                  static_cast<std::size_t>(terms.small_integer(location.children_[1]).value_or(0))}};
        }
        result.push_back(std::move(message));
    }
    return result;
}

// erl prints the modules of the sticky directories, one name per line. Added for transform source caching.
constexpr std::string_view LIST_STICKY =
    "[io:format(\"~ts~n\",[filename:rootname(F)])||A<-[kernel,stdlib,compiler],"
    "{ok,Fs}<-[file:list_dir(filename:join(code:lib_dir(A),\"ebin\"))],F<-Fs,filename:extension(F)=:=\".beam\"],"
    "halt().";

// Ask the host erl for its sticky modules; empty when it cannot answer. Added for transform source caching.
std::set<std::u32string> sticky_modules(const fs::path &erl) {
    const Workspace workspace;
    const auto program = text(erl);
    const auto listing = workspace.path() / "sticky.txt";
    const auto output = text(listing);
    const std::vector<llvm::StringRef> argv{program, "-noshell", "-noinput", "-eval", LIST_STICKY};
    const std::array<std::optional<llvm::StringRef>, 3> redirects{llvm::StringRef(), llvm::StringRef(output),
                                                                  llvm::StringRef()};
    if (llvm::sys::ExecuteAndWait(program, argv, std::nullopt, redirects, TIME_LIMIT_SECONDS) != 0) {
        return {};
    }
    std::set<std::u32string> result;
    std::istringstream lines(read_file(listing).value_or(""));
    for (std::string line; std::getline(lines, line);) {
        if (line.ends_with('\r')) {
            line.pop_back();
        }
        if (!line.empty()) {
            result.insert(wide(line));
        }
    }
    return result;
}

// {ok, Forms, Warnings} | {error, Errors, Warnings} | {failed, Text}.
void parse_reply(const std::string &bytes, TransformReply &reply) {
    const auto root = decode_external(bytes, reply.terms_);
    const auto parts = reply.terms_.node(root).children_;
    if (parts.size() == 2 && reply.terms_.is_atom(parts[0], U"failed")) {
        throw LoaderError("parse transform loader failed: " + utf8(reply.terms_.text(parts[1]).value_or(U"?")));
    }
    if (parts.size() != 3) {
        throw LoaderError("malformed loader reply");
    }
    reply.ok_ = reply.terms_.is_atom(parts[0], U"ok");
    if (reply.ok_) {
        reply.forms_ = reply.terms_.node(parts[1]).children_;
    } else {
        reply.errors_ = messages(reply.terms_, parts[1]);
    }
    reply.warnings_ = messages(reply.terms_, parts[2]);
}
} // namespace

std::optional<fs::path> find_erl(const std::optional<fs::path> &explicit_path) {
    if (explicit_path && *explicit_path == "none") {
        return std::nullopt;
    }
    if (explicit_path) {
        if (llvm::sys::fs::can_execute(text(*explicit_path))) {
            return explicit_path;
        }
        throw LoaderError("erl not found: " + text(*explicit_path));
    }
    if (const auto found = llvm::sys::findProgramByName("erl")) {
        return fs::path(*found);
    }
    // Windows installers may leave Erlang/OTP off PATH; try its default installation directory.
    if (const auto root = llvm::sys::Process::GetEnv("ProgramFiles")) {
        const auto candidate = *root + "/Erlang OTP/bin/erl.exe";
        if (llvm::sys::fs::can_execute(candidate)) {
            return fs::path(candidate);
        }
    }
    return std::nullopt;
}

TransformReply run_transforms(const AbstractModule &module, const TransformRequest &request) {
    const Workspace workspace;
    const auto &directory = workspace.path();
    write_file(directory / "clause_transform_loader.erl", LOADER_SOURCE);
    write_file(directory / "request.etf", request_bytes(module, request));
    const auto program = text(request.erl_);
    std::vector<std::string> arguments{program, "-noshell", "-noinput"};
    for (const auto &path : request.code_paths_) {
        arguments.insert(arguments.end(), {"-pa", text(fs::absolute(path))});
    }
    arguments.insert(arguments.end(), {"-eval", std::string(BOOT), "-extra", text(directory)});
    const std::vector<llvm::StringRef> argv(arguments.begin(), arguments.end());
    const auto log = text(directory / "output.txt");
    const std::array<std::optional<llvm::StringRef>, 3> redirects{llvm::StringRef(), llvm::StringRef(log),
                                                                  llvm::StringRef(log)};
    std::string error;
    bool failed = false;
    const auto status =
        llvm::sys::ExecuteAndWait(program, argv, std::nullopt, redirects, TIME_LIMIT_SECONDS, 0, &error, &failed);
    if (failed) {
        throw LoaderError("cannot run " + program + ": " + error);
    }
    TransformReply reply;
    reply.output_ = read_file(directory / "output.txt", OUTPUT_LIMIT).value_or("");
    const auto bytes = read_file(directory / "reply.etf");
    if (!bytes) {
        throw LoaderError("parse transform loader ended without a reply (exit status " + std::to_string(status) +
                          (error.empty() ? "" : ", " + error) + ")\n" + reply.output_);
    }
    parse_reply(*bytes, reply);
    return reply;
}

bool StickyModules::contains(const fs::path &erl, const std::u32string &module) {
    auto found = modules_.find(erl);
    if (found == modules_.end()) {
        std::set<std::u32string> names;
        try {
            names = sticky_modules(erl);
        } catch (const LoaderError &) {
            names.clear(); // No temporary directory: compile project sources as before.
        }
        found = modules_.emplace(erl, std::move(names)).first;
    }
    return found->second.contains(module);
}
} // namespace clause::transforms
