#pragma once
// Added for ssl (plan 21 step LIB81A): the builtins of Clause's ssl module (docs/libraries/ssl.md): OTP's cipher suite
// table and lists, and ssl_cipher_format's suite name walks.
#include "../typed.hpp"
#include <optional>
#include <string>
#include <string_view>

namespace clause::runtime::builtins::ssl {
// The roles of the builtins' arguments.
struct DescriptionRole;
struct VersionRole;
struct StringTypeRole;
struct SuiteRole;
struct NameRole;
struct CodeRole;

// One part of a suite map: absent, an atom (its spelling as code points) or another term (badarg where it is used).
struct SuitePart {
    bool present_ = false;
    std::optional<std::u32string> atom_;
};

// The four parts of a suite map, as the name walks read them.
struct SuiteParts {
    SuitePart key_exchange_;
    SuitePart cipher_;
    SuitePart mac_;
    SuitePart prf_;
};

// The RFC and OpenSSL names of a suite, as ssl:suite_to_str/1 and suite_to_openssl_str/1 build them; function_clause
// for parts no clause takes, badarg for a used part that is not an atom.
std::u32string rfc_name(const SuiteParts &suite);
std::u32string openssl_name(const SuiteParts &suite);
// Whether a spelling is a part of a suite of the table, so its atom exists as in OTP's ssl_cipher_format.
bool known_suite_part(std::string_view spelling);

// cipher_suites(Description, Version): the suite maps of an OTP list; function_clause for anything else.
Term cipher_suites(ProcessContext &context, const Named<DescriptionRole> &description,
                   const Named<VersionRole> &version);
// cipher_suites(Description, Version, rfc | openssl): the same as names.
Term cipher_suite_names(ProcessContext &context, const Named<DescriptionRole> &description,
                        const Named<VersionRole> &version, const Named<StringTypeRole> &type);
// suite_to_str(Suite), suite_to_openssl_str(Suite), str_to_suite(Name).
Term suite_to_str(ProcessContext &context, const Named<SuiteRole> &suite);
Term suite_to_openssl_str(ProcessContext &context, const Named<SuiteRole> &suite);
Term str_to_suite(ProcessContext &context, const Named<NameRole> &name);
// clause_ssl_algorithms:suite_code(Suite) and code_suite(Code): a listed suite's two-byte code and back, else
// undefined (LIB81C1: connections name suites by code).
Term suite_code(ProcessContext &context, const Named<SuiteRole> &suite);
Term code_suite(ProcessContext &context, const Named<CodeRole> &code);
// clause_ssl_algorithms:hello_extensions(Message, {Major, Minor}): the extension map of a ClientHello or ServerHello
// handshake message read for the local highest version, as {handshake, hello} returns it, else error (LIB81C2).
Term hello_extensions(ProcessContext &context, const BinaryArgument &message, const TupleArgument &local);

// The character list of code points.
Term string_term(ProcessContext &context, std::u32string_view text);
// error:function_clause.
[[noreturn]] void function_clause();
} // namespace clause::runtime::builtins::ssl
