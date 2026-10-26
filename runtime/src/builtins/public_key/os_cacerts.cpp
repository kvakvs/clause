#include "public_key.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
// wincrypt.h needs windows.h first.
#include <wincrypt.h>
// Programs link the runtime archive alone, so the archive names the certificate store library it needs.
#pragma comment(lib, "crypt32.lib")
#endif

// Added for public_key trust stores (plan 21 step LIB80D6): the certificates of Windows' current user ROOT store, as
// OTP's pubkey_os_cacerts NIF reads them for public_key:cacerts_load/0.
namespace clause::runtime::builtins::public_key {
namespace {
#ifdef _WIN32
// The atom OTP's NIF tags a certificate's encoding with.
AtomName encoding_name(DWORD encoding) {
    switch (encoding) {
    case X509_ASN_ENCODING:
        return AtomName{"x509_asn_encoding"};
    case PKCS_7_ASN_ENCODING:
        return AtomName{"pkcs_7_asn_encoding"};
    default:
        return AtomName{"unknown"};
    }
}

// {Encoding, Der} of one certificate of the store.
Term certificate(ProcessContext &context, const CERT_CONTEXT &cert) {
    const std::span<const std::byte> der{reinterpret_cast<const std::byte *>(cert.pbCertEncoded), cert.cbCertEncoded};
    const std::array parts{need(TermFactory(context).atom(encoding_name(cert.dwCertEncodingType))),
                           need(TermFactory(context).binary(der))};
    return need(TermFactory(context).tuple(parts));
}
#endif
} // namespace

// os_cacerts(): [{Encoding, Der}] in reverse store order as the NIF conses them, internal_error when the store does
// not open; [] off Windows, where public_key never asks.
Term os_cacerts(ProcessContext &context) {
    std::vector<Term> certificates;
#ifdef _WIN32
    HCERTSTORE store = CertOpenStore(CERT_STORE_PROV_SYSTEM_W, 0, 0, CERT_SYSTEM_STORE_CURRENT_USER, L"ROOT");
    if (store == nullptr) {
        return need(TermFactory(context).atom(AtomName{"internal_error"}));
    }
    PCCERT_CONTEXT cert = nullptr;
    while ((cert = CertEnumCertificatesInStore(store, cert)) != nullptr) {
        certificates.push_back(certificate(context, *cert));
    }
    CertCloseStore(store, 0);
    std::ranges::reverse(certificates);
#endif
    return need(TermFactory(context).list(certificates));
}
} // namespace clause::runtime::builtins::public_key
