#include "../erlang/ports.hpp"
#include "crypto.hpp"
#include <array>
#include <openssl/evp.h>
#include <terms.hpp>

// Errors and term helpers of the crypto builtins (plan 21 step LIB79).
namespace clause::runtime::builtins::crypto {
namespace {
// The atom of a fault class, as OTP names them.
AtomName fault_name(Fault fault) {
    switch (fault) {
    case Fault::badarg:
        return "badarg"_atom;
    case Fault::notsup:
        return "notsup"_atom;
    case Fault::error:
        break;
    }
    return "error"_atom;
}

// The file name of a source path, without its directories.
std::string_view base_name(std::string_view path) {
    const auto slash = path.find_last_of("/\\");
    return slash == std::string_view::npos ? path : path.substr(slash + 1);
}
} // namespace

void reject(ProcessContext &context, Fault fault, Rejection rejection, std::source_location location) {
    TermFactory factory(context);
    const std::array<std::pair<AtomName, Term>, 3> details{
        std::pair<AtomName, Term>{"c_file_name"_atom, need(string_term(context, base_name(location.file_name())))},
        std::pair<AtomName, Term>{"c_file_line_num"_atom, need(factory.integer(location.line()))},
        std::pair<AtomName, Term>{"c_function_arg_num"_atom, need(factory.integer(rejection.argument))}};
    const std::array parts{atom(context, fault_name(fault)), need(atom_map(context, details)),
                           need(string_term(context, rejection.message))};
    throw BuiltinFailure{.reason = abi::v1::ErrorReason::raised_error, .payload = need(factory.tuple(parts)).word()};
}

void raise_notsup(ProcessContext &context) {
    throw BuiltinFailure{.reason = abi::v1::ErrorReason::raised_error, .payload = atom(context, "notsup"_atom).word()};
}

std::optional<std::vector<std::byte>> iodata(const Term &data) { return iodata_bytes(data); }

std::vector<std::byte> iodata_argument(ProcessContext &context, const Term &data, Rejection rejection) {
    auto bytes = iodata_bytes(data);
    if (!bytes) {
        reject(context, Fault::badarg, rejection);
    }
    return std::move(*bytes);
}

const unsigned char *bytes_address(std::span<const std::byte> bytes) {
    static constexpr unsigned char NO_BYTES = 0;
    return bytes.empty() ? &NO_BYTES : reinterpret_cast<const unsigned char *>(bytes.data());
}

void CipherContextDeleter::operator()(EVP_CIPHER_CTX *context) const noexcept { EVP_CIPHER_CTX_free(context); }

TermResult<Term> string_term(ProcessContext &context, std::string_view text) {
    TermFactory factory(context);
    std::vector<Term> characters;
    characters.reserve(text.size());
    for (const char c : text) {
        characters.push_back(need(factory.integer(static_cast<unsigned char>(c))));
    }
    return factory.list(characters);
}

TermResult<Term> atom_map(ProcessContext &context, std::span<const std::pair<AtomName, Term>> entries) {
    std::vector<std::pair<Term, Term>> pairs;
    pairs.reserve(entries.size());
    for (const auto &[key, value] : entries) {
        pairs.emplace_back(atom(context, key), value);
    }
    return TermFactory(context).map(pairs);
}
} // namespace clause::runtime::builtins::crypto
