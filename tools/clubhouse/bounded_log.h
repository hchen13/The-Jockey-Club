// Small, best-effort log retention for the launcher. All I/O is on verified
// handles; a busy or unsafe log is left alone rather than blocking startup.
#pragma once
#include <windows.h>
#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace clubhouse_log {
namespace fs = std::filesystem;
constexpr unsigned long long kLimit = 1024 * 1024;
constexpr DWORD kTail = 256 * 1024;

inline bool PlainDirectory(const fs::path& path) {
    const DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) && !(a & FILE_ATTRIBUTE_REPARSE_POINT);
}

inline bool Verify(HANDLE handle, const fs::path& path) {
    BY_HANDLE_FILE_INFORMATION info{};
    if (GetFileType(handle) != FILE_TYPE_DISK || !GetFileInformationByHandle(handle, &info) ||
        (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) || info.nNumberOfLinks != 1)
        return false;
    std::wstring actual(32768, L'\0');
    const DWORD n = GetFinalPathNameByHandleW(handle, actual.data(), static_cast<DWORD>(actual.size()), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (!n || n >= actual.size()) return false;
    actual.resize(n);
    if (actual.rfind(L"\\\\?\\UNC\\", 0) == 0) actual = L"\\\\" + actual.substr(8);
    else if (actual.rfind(L"\\\\?\\", 0) == 0) actual.erase(0, 4);
    const auto expected = fs::absolute(path).lexically_normal().wstring();
    return _wcsicmp(actual.c_str(), expected.c_str()) == 0;
}

inline HANDLE Open(const fs::path& path, DWORD access, DWORD sharing, bool inherit) {
    if (!PlainDirectory(path.parent_path()) || !PlainDirectory(path.parent_path().parent_path()))
        return INVALID_HANDLE_VALUE;
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, inherit ? TRUE : FALSE};
    HANDLE handle = CreateFileW(path.c_str(), access, sharing, &security, OPEN_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle != INVALID_HANDLE_VALUE) {
        try {
            if (Verify(handle, path)) return handle;
        } catch (...) {}
        CloseHandle(handle); return INVALID_HANDLE_VALUE;
    }
    return handle;
}

// Keep the latest tail in this same exclusively opened file; no rename,
// unknown companion file, cross-process wait, or deletion is involved.
inline bool Trim(HANDLE handle, unsigned long long incoming, unsigned long long limit = kLimit, DWORD tailLimit = kTail) {
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(handle, &size) || size.QuadPart < 0) return false;
    if (static_cast<unsigned long long>(size.QuadPart) + incoming <= limit) return true;
    const auto keep = static_cast<DWORD>((std::min)(static_cast<unsigned long long>(size.QuadPart),
                                                     (std::min)(static_cast<unsigned long long>(tailLimit), limit > incoming ? limit - incoming : 0)));
    std::vector<char> data(keep);
    LARGE_INTEGER offset{}; offset.QuadPart = size.QuadPart - keep;
    DWORD actual = 0;
    if (!SetFilePointerEx(handle, offset, nullptr, FILE_BEGIN) || (keep && (!ReadFile(handle, data.data(), keep, &actual, nullptr) || actual != keep))) return false;
    offset.QuadPart = 0;
    if (!SetFilePointerEx(handle, offset, nullptr, FILE_BEGIN) || (keep && (!WriteFile(handle, data.data(), keep, &actual, nullptr) || actual != keep))) return false;
    return SetEndOfFile(handle) != FALSE;
}

inline void Append(const fs::path& path, const std::string& line) {
    HANDLE handle = Open(path, GENERIC_READ | GENERIC_WRITE, 0, false);
    if (handle == INVALID_HANDLE_VALUE) return;
    try {
        const DWORD length = static_cast<DWORD>((std::min)(line.size(), size_t{4096}));
        if (Trim(handle, length)) {
            LARGE_INTEGER zero{}; DWORD written = 0;
            if (SetFilePointerEx(handle, zero, nullptr, FILE_END)) WriteFile(handle, line.data(), length, &written, nullptr);
        }
    } catch (...) {}
    CloseHandle(handle);
}

inline void TrimBeforeStart(const fs::path& path) {
    HANDLE handle = Open(path, GENERIC_READ | GENERIC_WRITE, 0, false);
    if (handle == INVALID_HANDLE_VALUE) return;
    try { Trim(handle, 0); } catch (...) {}
    CloseHandle(handle);
}
} // namespace clubhouse_log
