// Created for application resources (plan 21 step LIB24).
#include "app_resources.hpp"
#include "../project/paths.hpp"
#include <algorithm>
#include <clause/compiler/printing.hpp>
#include <fstream>
#include <iterator>
#include <optional>
#include <regex>
#include <set>
#include <sstream>

namespace clause::cli {
namespace {
constexpr std::string_view APP_SUFFIX = ".app";
constexpr std::string_view SOURCE_SUFFIX = ".app.src";

// One resource file: the application it names, the file and the directory whose modules it lists by default.
struct Resource {
    std::string name_;
    std::filesystem::path file_;
    std::filesystem::path modules_directory_;
};

// The application a resource file names: its file name without `.app` or `.app.src`; empty for other files.
std::string resource_name(const std::filesystem::path &file) {
    const auto name = project::path_text(file.filename());
    for (const auto suffix : {SOURCE_SUFFIX, APP_SUFFIX}) {
        if (name.size() > suffix.size() && name.ends_with(suffix)) {
            return name.substr(0, name.size() - suffix.size());
        }
    }
    return {};
}

// The resource files directly in `directory`, sorted.
std::vector<std::filesystem::path> resource_files(const std::filesystem::path &directory) {
    std::vector<std::filesystem::path> files;
    std::error_code error;
    for (const auto &item : std::filesystem::directory_iterator(directory, error)) {
        if (item.is_regular_file(error) && !resource_name(item.path()).empty()) {
            files.push_back(item.path());
        }
    }
    std::ranges::sort(files);
    return files;
}

// The resources beside the sources in `directory` and in the `ebin` next to it, each listing that directory's
// modules.
void add_beside(const std::filesystem::path &directory, std::vector<Resource> &resources) {
    for (const auto &place : {directory, directory.parent_path() / "ebin"}) {
        for (auto &file : resource_files(place)) {
            resources.push_back({resource_name(file), std::move(file), directory});
        }
    }
}

// A project application's resource: ebin/Name.app, else src/Name.app.src.
void add_application(const std::string &name, const std::filesystem::path &directory,
                     std::vector<Resource> &resources) {
    for (const auto &file : {directory / "ebin" / project::native_path(name + std::string(APP_SUFFIX)),
                             directory / "src" / project::native_path(name + std::string(SOURCE_SUFFIX))}) {
        std::error_code error;
        if (std::filesystem::is_regular_file(file, error)) {
            resources.push_back({name, file, directory});
            return;
        }
    }
}

// The applications a resource names in its `applications` key.
std::vector<std::string> dependencies(const std::filesystem::path &file) {
    std::ifstream stream(file, std::ios::binary);
    const std::string text{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    static const std::regex APPLICATIONS(R"(\{\s*applications\s*,\s*\[([^\]]*)\])");
    static const std::regex NAME("[a-z][A-Za-z0-9_@]*");
    std::smatch match;
    std::vector<std::string> names;
    if (std::regex_search(text, match, APPLICATIONS)) {
        const auto list = match.str(1);
        for (auto at = std::sregex_iterator(list.begin(), list.end(), NAME); at != std::sregex_iterator(); ++at) {
            names.push_back(at->str());
        }
    }
    return names;
}

// The resource of a library application directory (`public_key/public_key.app`); none when it has none.
std::optional<Resource> library_resource(const ApplicationPlaces &places, const std::string &name) {
    for (const auto &directory : places.library_) {
        auto file = directory / project::native_path(name + std::string(APP_SUFFIX));
        std::error_code error;
        if (project::path_text(directory.filename()) == name && std::filesystem::is_regular_file(file, error)) {
            return Resource{name, std::move(file), directory};
        }
    }
    return std::nullopt;
}

// The library applications the found resources depend on, in turn, so that application:ensure_all_started/1 finds
// them (ssl's public_key and asn1). Added for the ssl application (plan 21 step LIB81A).
void add_library_dependencies(const ApplicationPlaces &places, std::vector<Resource> &found) {
    for (std::size_t i = 0; i < found.size(); ++i) {
        for (const auto &name : dependencies(found[i].file_)) {
            const auto known = std::ranges::any_of(found, [&name](const Resource &r) { return r.name_ == name; });
            if (auto resource = known ? std::nullopt : library_resource(places, name)) {
                found.push_back(std::move(*resource));
            }
        }
    }
}

// Every resource of the program, the first one of each application name only.
std::vector<Resource> resources(const ApplicationPlaces &places) {
    std::vector<Resource> found;
    std::set<std::filesystem::path> directories;
    for (const auto &[directory, module] : places.modules_) {
        if (directories.insert(directory).second) {
            add_beside(directory, found);
        }
    }
    for (const auto &[name, directory] : places.applications_) {
        add_application(name, directory, found);
    }
    add_library_dependencies(places, found);
    std::set<std::string> names;
    std::erase_if(found, [&names](const Resource &resource) { return !names.insert(resource.name_).second; });
    return found;
}

// The modules whose sources are in `directory` or below it, as an Erlang list.
std::string modules_below(const ApplicationPlaces &places, const std::filesystem::path &directory) {
    std::set<std::string> names;
    const auto prefix = project::path_text(directory / "");
    for (const auto &[source, module] : places.modules_) {
        if (project::path_text(source / "").starts_with(prefix)) {
            names.insert(atom_source(module));
        }
    }
    std::string list;
    for (const auto &name : names) {
        list.append(list.empty() ? "" : ", ").append(name);
    }
    return "[" + list + "]";
}

// The modules of one library application directory, from its sources.
std::string library_clause(const std::filesystem::path &directory) {
    std::set<std::string> names;
    std::error_code error;
    for (const auto &item : std::filesystem::directory_iterator(directory, error)) {
        if (item.path().extension() == ".erl") {
            names.insert(atom_source(project::path_text(item.path().stem())));
        }
    }
    std::string list;
    for (const auto &name : names) {
        list.append(list.empty() ? "" : ", ").append(name);
    }
    return "library_modules(" + atom_source(project::path_text(directory.filename())) + ") -> [" + list + "];\n";
}

// An Erlang string literal of a path, with forward slashes.
std::string path_literal(const std::filesystem::path &path) {
    auto text = project::path_text(path);
    std::ranges::replace(text, '\\', '/');
    std::string literal = "\"";
    for (const char c : text) {
        literal.append(c == '"' ? "\\" : "").push_back(c);
    }
    return literal + "\"";
}

// The function returning one resource's term: its text, its lines kept so diagnostics point into the file.
std::string resource_function(const std::size_t index, const Resource &resource) {
    std::ifstream file(resource.file_, std::ios::binary);
    const std::string text{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    return "-file(" + path_literal(resource.file_) + ", 1).\napp_" + std::to_string(index) + "() -> " + text + "\n";
}
} // namespace

std::string applications_source(const ApplicationPlaces &places) {
    const auto found = resources(places);
    std::string specs;
    for (std::size_t i = 0; i < found.size(); ++i) {
        specs.append(i == 0 ? "" : ",\n    ").append("{app_" + std::to_string(i) + "(), ");
        specs.append(modules_below(places, found[i].modules_directory_)).append("}");
    }
    std::ostringstream source;
    source << "-module(clause_applications).\n-export([specs/0, library_modules/1]).\n";
    source << "specs() ->\n    [" << specs << "].\n";
    for (const auto &directory : places.library_) {
        source << library_clause(directory);
    }
    source << "library_modules(_) -> [].\n";
    for (std::size_t i = 0; i < found.size(); ++i) {
        source << resource_function(i, found[i]);
    }
    return source.str();
}

std::filesystem::path applications_path() { return project::native_path("clause_applications.erl"); }
} // namespace clause::cli
