#include "port.hpp"
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <mutex>
#include <string>
#include <utility>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// Programs that link the runtime get the registry functions with it, as Boost.Asio's headers bring Winsock.
#pragma comment(lib, "advapi32.lib")

// The registry driver of win32reg (plan 21 step LIB55B, docs/ports.md#the-registry): {spawn_driver,
// "clause_registry"}, Windows only. It keeps OTP's registry driver's state (access mode, current key and its root)
// and answers OTP's commands, as port_control/3 operations, with the replies OTP's driver sends as port data: 'o',
// 'e' and the error, 'k' and a key, 'v' and a value, 's' and the current key. Listings give every reply, each
// preceded by its 32-bit size.
namespace clause::runtime::detail {
namespace {
// A big-endian 32-bit number at the start of `bytes`.
std::uint32_t int32_at(std::span<const std::byte> bytes) {
    std::uint32_t value = 0;
    for (std::size_t index = 0; index < 4 && index < bytes.size(); ++index) {
        value = (value << 8) | std::to_integer<std::uint32_t>(bytes[index]);
    }
    return value;
}

void put_int32(ByteBuffer &out, std::uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) {
        out.push_back(static_cast<std::byte>((value >> shift) & 0xff));
    }
}

void put_text(ByteBuffer &out, std::string_view text) {
    for (const char c : text) {
        out.push_back(static_cast<std::byte>(c));
    }
}

// The text up to the first zero byte.
std::string c_string(std::span<const std::byte> bytes) {
    std::string text;
    for (const auto byte : bytes) {
        if (byte == std::byte{0}) {
            break;
        }
        text.push_back(static_cast<char>(byte));
    }
    return text;
}

// The C runtime's errno of a Windows error, as its _dosmaperr maps them.
int dos_errno(DWORD error) {
    static constexpr std::array<std::pair<DWORD, int>, 45> TABLE{{
        {ERROR_INVALID_FUNCTION, EINVAL},    {ERROR_FILE_NOT_FOUND, ENOENT},
        {ERROR_PATH_NOT_FOUND, ENOENT},      {ERROR_TOO_MANY_OPEN_FILES, EMFILE},
        {ERROR_ACCESS_DENIED, EACCES},       {ERROR_INVALID_HANDLE, EBADF},
        {ERROR_ARENA_TRASHED, ENOMEM},       {ERROR_NOT_ENOUGH_MEMORY, ENOMEM},
        {ERROR_INVALID_BLOCK, ENOMEM},       {ERROR_BAD_ENVIRONMENT, E2BIG},
        {ERROR_BAD_FORMAT, ENOEXEC},         {ERROR_INVALID_ACCESS, EINVAL},
        {ERROR_INVALID_DATA, EINVAL},        {ERROR_INVALID_DRIVE, ENOENT},
        {ERROR_CURRENT_DIRECTORY, EACCES},   {ERROR_NOT_SAME_DEVICE, EXDEV},
        {ERROR_NO_MORE_FILES, ENOENT},       {ERROR_LOCK_VIOLATION, EACCES},
        {ERROR_BAD_NETPATH, ENOENT},         {ERROR_NETWORK_ACCESS_DENIED, EACCES},
        {ERROR_BAD_NET_NAME, ENOENT},        {ERROR_FILE_EXISTS, EEXIST},
        {ERROR_CANNOT_MAKE, EACCES},         {ERROR_FAIL_I24, EACCES},
        {ERROR_INVALID_PARAMETER, EINVAL},   {ERROR_NO_PROC_SLOTS, EAGAIN},
        {ERROR_DRIVE_LOCKED, EACCES},        {ERROR_BROKEN_PIPE, EPIPE},
        {ERROR_DISK_FULL, ENOSPC},           {ERROR_INVALID_TARGET_HANDLE, EBADF},
        {ERROR_WAIT_NO_CHILDREN, ECHILD},    {ERROR_CHILD_NOT_COMPLETE, ECHILD},
        {ERROR_DIRECT_ACCESS_HANDLE, EBADF}, {ERROR_NEGATIVE_SEEK, EINVAL},
        {ERROR_SEEK_ON_DEVICE, EACCES},      {ERROR_DIR_NOT_EMPTY, ENOTEMPTY},
        {ERROR_NOT_LOCKED, EACCES},          {ERROR_BAD_PATHNAME, ENOENT},
        {ERROR_MAX_THRDS_REACHED, EAGAIN},   {ERROR_LOCK_FAILED, EACCES},
        {ERROR_ALREADY_EXISTS, EEXIST},      {ERROR_FILENAME_EXCED_RANGE, ENOENT},
        {ERROR_NESTING_NOT_ALLOWED, EAGAIN}, {ERROR_NOT_ENOUGH_QUOTA, ENOMEM},
        {ERROR_SHARING_VIOLATION, EACCES},
    }};
    for (const auto &[code, number] : TABLE) {
        if (code == error) {
            return number;
        }
    }
    if (error >= ERROR_WRITE_PROTECT && error <= ERROR_SHARING_BUFFER_EXCEEDED) {
        return EACCES;
    }
    return error >= ERROR_INVALID_STARTING_CODESEG && error <= ERROR_INFLOOP_IN_RELOC_CHAIN ? ENOEXEC : EINVAL;
}

// The errno of a registry error, as OTP's driver maps it: damaged registries are EIO, a deleted key EINVAL.
int registry_errno(LONG error) {
    switch (error) {
    case ERROR_BADDB:
    case ERROR_BADKEY:
    case ERROR_CANTOPEN:
    case ERROR_CANTWRITE:
    case ERROR_REGISTRY_RECOVERED:
    case ERROR_REGISTRY_CORRUPT:
    case ERROR_REGISTRY_IO_FAILED:
    case ERROR_NOT_REGISTRY_FILE:
        return EIO;
    case ERROR_KEY_DELETED:
        return EINVAL;
    default:
        return dos_errno(static_cast<DWORD>(error));
    }
}

// The lowercase errno name OTP's erl_errno_id gives the errnos the registry reports.
std::string_view errno_name(int number) {
    static constexpr std::array<std::pair<int, std::string_view>, 16> NAMES{{
        {EINVAL, "einval"},
        {ENOENT, "enoent"},
        {EMFILE, "emfile"},
        {EACCES, "eacces"},
        {EBADF, "ebadf"},
        {ENOMEM, "enomem"},
        {E2BIG, "e2big"},
        {ENOEXEC, "enoexec"},
        {EXDEV, "exdev"},
        {EEXIST, "eexist"},
        {EAGAIN, "eagain"},
        {EPIPE, "epipe"},
        {ENOSPC, "enospc"},
        {ECHILD, "echild"},
        {ENOTEMPTY, "enotempty"},
        {EIO, "eio"},
    }};
    for (const auto &[code, name] : NAMES) {
        if (code == number) {
            return name;
        }
    }
    return "unknown";
}

// 'o', or 'e' and the error's name.
ByteBuffer reply(LONG result) {
    ByteBuffer out;
    if (result == ERROR_SUCCESS) {
        out.push_back(std::byte{'o'});
        return out;
    }
    out.push_back(std::byte{'e'});
    put_text(out, errno_name(registry_errno(result)));
    return out;
}

// A reply preceded by its size, for listings.
void append_record(ByteBuffer &listing, const ByteBuffer &record) {
    put_int32(listing, static_cast<std::uint32_t>(record.size()));
    listing.insert(listing.end(), record.begin(), record.end());
}

// The predefined root keys and the 32-bit numbers win32reg names them by.
const std::array<std::pair<std::uint32_t, HKEY>, 7> &root_keys() {
    static const std::array<std::pair<std::uint32_t, HKEY>, 7> ROOTS{{
        {0x80000000, HKEY_CLASSES_ROOT},
        {0x80000001, HKEY_CURRENT_USER},
        {0x80000002, HKEY_LOCAL_MACHINE},
        {0x80000003, HKEY_USERS},
        {0x80000004, HKEY_PERFORMANCE_DATA},
        {0x80000005, HKEY_CURRENT_CONFIG},
        {0x80000006, HKEY_DYN_DATA},
    }};
    return ROOTS;
}

// The root key of a number; none (an invalid handle for the registry) for another number.
HKEY root_key(std::uint32_t number) {
    for (const auto &[key_number, key] : root_keys()) {
        if (key_number == number) {
            return key;
        }
    }
    return nullptr;
}

std::uint32_t root_number(HKEY key) {
    for (const auto &[key_number, root] : root_keys()) {
        if (root == key) {
            return key_number;
        }
    }
    return 0;
}

class RegistryDriver final : public PortDriver {
  public:
    RegistryDriver() = default;
    RegistryDriver(const RegistryDriver &) = delete;
    RegistryDriver &operator=(const RegistryDriver &) = delete;
    RegistryDriver(RegistryDriver &&) = delete;
    RegistryDriver &operator=(RegistryDriver &&) = delete;

    ~RegistryDriver() override { RegCloseKey(key_); }

    std::expected<void, DriverError> write(std::span<const std::byte>) override {
        return std::unexpected(DriverError{"einval"});
    }

    bool controllable() const noexcept override { return true; }

    std::optional<ByteBuffer> control(std::uint32_t operation, std::span<const std::byte> bytes, Word) override {
        using Handler = ByteBuffer (RegistryDriver::*)(std::span<const std::byte>);
        // Indexed by the operation: OTP's command numbers, then set_mode.
        static constexpr std::array<Handler, 10> HANDLERS{
            &RegistryDriver::get_current, &RegistryDriver::open_key,   &RegistryDriver::create_key,
            &RegistryDriver::sub_keys,    &RegistryDriver::get_value,  &RegistryDriver::values,
            &RegistryDriver::set_value,   &RegistryDriver::delete_key, &RegistryDriver::delete_value,
            &RegistryDriver::set_mode,
        };
        if (operation >= HANDLERS.size()) {
            return std::nullopt;
        }
        const std::scoped_lock lock(mutex_);
        return (this->*HANDLERS.at(operation))(bytes);
    }

  private:
    ByteBuffer get_current(std::span<const std::byte>) { return state(); }

    ByteBuffer open_key(std::span<const std::byte> bytes) { return change_key(false, bytes); }

    ByteBuffer create_key(std::span<const std::byte> bytes) { return change_key(true, bytes); }

    ByteBuffer get_value(std::span<const std::byte> bytes) { return value(c_string(bytes)); }

    ByteBuffer delete_key(std::span<const std::byte> bytes) {
        return reply(RegDeleteKeyA(key_, c_string(bytes).c_str()));
    }

    ByteBuffer delete_value(std::span<const std::byte> bytes) {
        return reply(RegDeleteValueA(key_, c_string(bytes).c_str()));
    }

    // The access to ask for: read always, write when the mode has a 'w'.
    ByteBuffer set_mode(std::span<const std::byte> bytes) {
        for (const auto byte : bytes) {
            access_ |= byte == std::byte{'w'} ? KEY_WRITE : KEY_READ;
        }
        return reply(ERROR_SUCCESS);
    }

    // 's', the root key and the current key's path.
    ByteBuffer state() const {
        ByteBuffer out{std::byte{'s'}};
        put_int32(out, root_number(root_));
        put_text(out, name_);
        return out;
    }

    // Open (or create) [Root(32 bits), Path, 0] as the current key; the root changes even when that fails.
    ByteBuffer change_key(bool create, std::span<const std::byte> bytes) {
        root_ = root_key(int32_at(bytes));
        const auto path = c_string(bytes.subspan(std::min<std::size_t>(4, bytes.size())));
        HKEY opened = nullptr;
        DWORD disposition = 0;
        std::array<char, 1> no_class{};
        const auto result = create ? RegCreateKeyExA(root_, path.c_str(), 0, no_class.data(), 0, access_, nullptr,
                                                     &opened, &disposition)
                                   : RegOpenKeyExA(root_, path.c_str(), 0, access_, &opened);
        if (result == ERROR_SUCCESS) {
            RegCloseKey(std::exchange(key_, opened));
            name_ = path;
        }
        return reply(result);
    }

    ByteBuffer sub_keys(std::span<const std::byte>) {
        ByteBuffer listing;
        for (DWORD index = 0;; ++index) {
            std::array<char, 256> name{};
            auto size = static_cast<DWORD>(name.size());
            const auto result = RegEnumKeyExA(key_, index, name.data(), &size, nullptr, nullptr, nullptr, nullptr);
            if (result != ERROR_SUCCESS) {
                append_record(listing, reply(result == ERROR_NO_MORE_ITEMS ? ERROR_SUCCESS : result));
                return listing;
            }
            ByteBuffer record{std::byte{'k'}};
            put_text(record, std::string_view(name.data(), size));
            append_record(listing, record);
        }
    }

    // 'v', the type, the name, 0 and the data; strings lose a trailing zero and DWORDs become big-endian.
    static ByteBuffer value_reply(DWORD type, std::string_view name, std::string data) {
        if ((type == REG_SZ || type == REG_EXPAND_SZ) && !data.empty() && data.back() == '\0') {
            data.pop_back();
        }
        ByteBuffer out{std::byte{'v'}};
        if ((type == REG_DWORD || type == REG_DWORD_BIG_ENDIAN) && data.size() >= 4) {
            DWORD number = 0;
            std::memcpy(&number, data.data(), sizeof(number));
            put_int32(out, REG_DWORD);
            put_text(out, name);
            out.push_back(std::byte{0});
            put_int32(out, number);
            return out;
        }
        put_int32(out, type);
        put_text(out, name);
        out.push_back(std::byte{0});
        put_text(out, data);
        return out;
    }

    ByteBuffer value(const std::string &name) {
        DWORD type = 0;
        DWORD size = 0;
        auto result = RegQueryValueExA(key_, name.c_str(), nullptr, &type, nullptr, &size);
        std::string data(size, '\0');
        if (result == ERROR_SUCCESS) {
            result = RegQueryValueExA(key_, name.c_str(), nullptr, &type, reinterpret_cast<BYTE *>(data.data()), &size);
            data.resize(size);
        }
        return result == ERROR_SUCCESS ? value_reply(type, name, std::move(data)) : reply(result);
    }

    ByteBuffer values(std::span<const std::byte>) {
        DWORD max_name = 0;
        DWORD max_value = 0;
        const auto info = RegQueryInfoKeyA(key_, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                                           &max_name, &max_value, nullptr, nullptr);
        ByteBuffer listing;
        if (info != ERROR_SUCCESS) {
            append_record(listing, reply(info));
            return listing;
        }
        for (DWORD index = 0;; ++index) {
            std::string name(max_name + 1, '\0');
            std::string data(max_value + 1, '\0');
            auto name_size = static_cast<DWORD>(name.size());
            auto data_size = static_cast<DWORD>(data.size());
            DWORD type = 0;
            const auto result = RegEnumValueA(key_, index, name.data(), &name_size, nullptr, &type,
                                              reinterpret_cast<BYTE *>(data.data()), &data_size);
            if (result != ERROR_SUCCESS) {
                append_record(listing, reply(result == ERROR_NO_MORE_ITEMS ? ERROR_SUCCESS : result));
                return listing;
            }
            data.resize(data_size);
            append_record(listing, value_reply(type, std::string_view(name.data(), name_size), std::move(data)));
        }
    }

    // [Type(32 bits), Name, 0, Data]; a DWORD's data is big-endian.
    ByteBuffer set_value(std::span<const std::byte> bytes) {
        const auto type = int32_at(bytes);
        const auto rest = bytes.subspan(std::min<std::size_t>(4, bytes.size()));
        const auto name = c_string(rest);
        auto data = rest.subspan(std::min(name.size() + 1, rest.size()));
        DWORD number = 0;
        if (type == REG_DWORD) {
            number = int32_at(data);
            data = std::as_bytes(std::span(&number, 1));
        }
        return reply(RegSetValueExA(key_, name.c_str(), 0, type, reinterpret_cast<const BYTE *>(data.data()),
                                    static_cast<DWORD>(data.size())));
    }

    // One command at a time.
    std::mutex mutex_;
    // The access asked for when opening keys.
    REGSAM access_ = KEY_READ;
    // The current key, its root and its path under the root.
    HKEY key_ = HKEY_CLASSES_ROOT;
    HKEY root_ = HKEY_CLASSES_ROOT;
    std::string name_;
};
} // namespace

std::unique_ptr<PortDriver> registry_driver() { return std::make_unique<RegistryDriver>(); }
} // namespace clause::runtime::detail
