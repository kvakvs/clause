// Created for crypto's FIPS mode (plan 21 step LIB79F).
#include "crypto.hpp"
#include <array>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <openssl/bio.h>
#include <openssl/conf.h>
#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/provider.h>
#include <terms.hpp>
#include <utility>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <climits>
#include <cstdlib>
#include <mach-o/dyld.h>
#endif

// FIPS mode of the crypto builtins (docs/builtins.md#crypto-module): OTP's switchable mode over OpenSSL's default
// `fips=yes` property, and the pinned mode of programs built with clau --fips, whose FIPS provider sits beside the
// executable, is loaded before the program runs and cannot be switched off.
namespace clause::runtime::builtins::crypto {
namespace {
// Set by the start hook of a pinned program, before any process runs.
std::atomic<bool> pinned{false};
// The loaded FIPS provider: loaded by the start hook, or tried by the first crypto call.
std::atomic<OSSL_PROVIDER *> provider{nullptr};
// OpenSSL's default `fips=yes` property, cached for the checks of every call.
std::atomic<bool> enabled{false};
// Serializes the mode switches of enable_fips_mode/1.
std::mutex switching;

// The settings file `openssl fipsinstall` writes, and the section the provider's settings are in.
constexpr std::string_view MODULE_CONFIG = "fipsmodule.cnf";
constexpr const char *CONFIG_SECTION = "fips_sect";
// The provider module beside a pinned executable, as clau --fips names it.
#ifdef _WIN32
constexpr std::string_view MODULE_NAME = "fips.dll";
#elif defined(__APPLE__)
constexpr std::string_view MODULE_NAME = "fips.dylib";
#else
constexpr std::string_view MODULE_NAME = "fips.so";
#endif

// A path as UTF-8, as OpenSSL takes paths and as diagnostics print them.
std::string utf8(const std::filesystem::path &path) {
    const auto text = path.u8string();
    return {text.begin(), text.end()};
}

// A path as UTF-8 with forward slashes, as an OpenSSL configuration value takes it (backslashes escape there).
std::string generic_utf8(const std::filesystem::path &path) {
    const auto text = path.generic_u8string();
    return {text.begin(), text.end()};
}

#ifdef _WIN32
// The running executable's path from the module table, growing the buffer until it fits.
std::filesystem::path executable_path() {
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        const auto size = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (size == 0) {
            return {};
        }
        if (size < path.size()) {
            path.resize(size);
            return path;
        }
        path.resize(path.size() * 2);
    }
}
#elif defined(__APPLE__)
// The running executable's path as dyld loaded it, resolved.
std::filesystem::path executable_path() {
    std::array<char, PATH_MAX> path{};
    auto size = static_cast<std::uint32_t>(path.size());
    std::array<char, PATH_MAX> resolved{};
    if (_NSGetExecutablePath(path.data(), &size) != 0 || !realpath(path.data(), resolved.data())) {
        return {};
    }
    return resolved.data();
}
#else
// The running executable's path from /proc.
std::filesystem::path executable_path() {
    std::error_code error;
    auto path = std::filesystem::read_symlink("/proc/self/exe", error);
    return error ? std::filesystem::path{} : path;
}
#endif

// The text of OpenSSL's earliest queued error, for a start failure; the queue is cleared.
std::string openssl_error() {
    std::array<char, 256> text{};
    const auto code = ERR_get_error();
    ERR_error_string_n(code, text.data(), text.size());
    ERR_clear_error();
    return code == 0 ? std::string("no OpenSSL error recorded") : std::string(text.data());
}

struct BioDeleter {
    void operator()(BIO *bio) const noexcept { BIO_free(bio); }
};

struct ConfDeleter {
    void operator()(CONF *conf) const noexcept { NCONF_free(conf); }
};

// A file's text; none when it cannot be read.
std::optional<std::string> file_text(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

// An OpenSSL configuration loading the FIPS provider `module` with the settings of fipsmodule.cnf, then the base
// provider: OpenSSL's own provider configuration, which every libcrypto since 3.0 takes.
std::string provider_config(const std::string &settings, const std::filesystem::path &module) {
    return "openssl_conf = clause_fips_init\n"
           "[clause_fips_init]\nproviders = clause_fips_providers\n"
           "[clause_fips_providers]\nfips = " +
           std::string(CONFIG_SECTION) + "\nbase = clause_fips_base\n[clause_fips_base]\nactivate = 1\n" + settings +
           "\n[" + CONFIG_SECTION + "]\nactivate = 1\nmodule = " + generic_utf8(module) + "\n";
}

#ifdef _WIN32
// Keeps Windows from showing its error dialogs ("Bad Image") while a module loads, for the calling thread: a module
// that does not load is reported on stderr and stops the program instead of waiting for a click.
class QuietLoading {
  public:
    QuietLoading() { SetThreadErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX, &previous_); }

    ~QuietLoading() { SetThreadErrorMode(previous_, nullptr); }

    QuietLoading(const QuietLoading &) = delete;
    QuietLoading &operator=(const QuietLoading &) = delete;

  private:
    // The thread's error mode to restore.
    DWORD previous_ = 0;
};
#else
// Other systems load modules without dialogs.
struct QuietLoading {};
#endif

// Load the providers a configuration text names; false when one of them does not load.
bool load_configured(const std::string &text) {
    [[maybe_unused]] const QuietLoading quiet;
    OPENSSL_load_builtin_modules();
    const std::unique_ptr<BIO, BioDeleter> bio(BIO_new_mem_buf(text.data(), static_cast<int>(text.size())));
    const std::unique_ptr<CONF, ConfDeleter> conf(NCONF_new(nullptr));
    long line = 0;
    return bio && conf && NCONF_load_bio(conf.get(), bio.get(), &line) == 1 &&
           CONF_modules_load(conf.get(), nullptr, 0) > 0 && OSSL_PROVIDER_available(nullptr, "fips") == 1;
}

// info_fips(): enabled or not_enabled; not_supported without a FIPS provider (OTP built without FIPS support).
Term info_fips(ProcessContext &context) {
    ensure_probed();
    if (!fips_provider()) {
        return atom(context, "not_supported"_atom);
    }
    return atom(context, fips_enabled() ? "enabled"_atom : "not_enabled"_atom);
}

// enable_fips_mode(Enable): whether the mode is now as asked; a pinned program never leaves FIPS mode.
Term enable_fips_mode(ProcessContext &context, const Term &flag) {
    const auto name = flag.is_atom() ? flag.atom_spelling_unchecked() : std::string_view{};
    if (name != "true" && name != "false") {
        bad_argument();
    }
    ensure_probed();
    const std::scoped_lock lock(switching);
    const bool enable = name == "true";
    bool switched = false;
    if (enable) {
        switched = fips_provider() && OSSL_PROVIDER_available(nullptr, "fips") == 1 &&
                   EVP_default_properties_enable_fips(nullptr, 1) == 1;
    } else {
        switched = !pinned.load() && EVP_default_properties_enable_fips(nullptr, 0) == 1;
    }
    enabled.store(EVP_default_properties_is_fips_enabled(nullptr) == 1);
    ERR_clear_error();
    return atom(context, switched ? "true"_atom : "false"_atom);
}

constexpr std::array FIPS_BUILTINS{
    typed_entry<info_fips>("clause_crypto", "info_fips"),
    typed_entry<enable_fips_mode>("clause_crypto", "enable_fips_mode"),
};
} // namespace

bool fips_enabled() noexcept { return enabled.load(std::memory_order_relaxed); }

OSSL_PROVIDER *fips_provider() noexcept { return provider.load(); }

void load_providers() {
    if (pinned.load()) {
        return;
    }
    [[maybe_unused]] const QuietLoading quiet;
    provider.store(OSSL_PROVIDER_load(nullptr, "fips"));
    OSSL_PROVIDER_load(nullptr, "default");
    OSSL_PROVIDER_load(nullptr, "base");
    OSSL_PROVIDER_load(nullptr, "legacy");
    enabled.store(EVP_default_properties_is_fips_enabled(nullptr) == 1);
}

std::string pin_fips() {
    // No OpenSSL configuration of the system may add providers to a pinned program.
    OPENSSL_init_crypto(OPENSSL_INIT_NO_LOAD_CONFIG, nullptr);
    const auto directory = executable_path().parent_path();
    if (directory.empty()) {
        return "cannot locate the executable's directory, where its FIPS provider is";
    }
    const auto config = directory / MODULE_CONFIG;
    const auto settings = file_text(config);
    if (!settings) {
        return "cannot read " + utf8(config);
    }
    if (!load_configured(provider_config(*settings, directory / MODULE_NAME))) {
        return "cannot load the FIPS provider from " + utf8(directory) + ": " + openssl_error();
    }
    if (EVP_default_properties_enable_fips(nullptr, 1) != 1) {
        return "cannot enable FIPS mode: " + openssl_error();
    }
    // Loading an activated provider again only hands out its handle.
    provider.store(OSSL_PROVIDER_load(nullptr, "fips"));
    enabled.store(true);
    pinned.store(true);
    return {};
}

std::span<const BuiltinEntry> fips_builtins() noexcept { return FIPS_BUILTINS; }
} // namespace clause::runtime::builtins::crypto
