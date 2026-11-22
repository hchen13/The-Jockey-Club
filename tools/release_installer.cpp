// Offline installer. Payload contract (UTF-8, no quoting):
// payload/manifest.tsv: TJC_INSTALL_MANIFEST 2\n
// sha256<TAB>decimal-size<TAB>replace|preserve|seed-speed<TAB>relative/path\n
// Build as a Windows subsystem executable; wmain also supports console builds.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cwctype>
#include <exception>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")

namespace {
constexpr wchar_t kTitle[] = L"The Jockey Club Installer";
constexpr wchar_t kManifest[] = L"manifest.tsv";
constexpr wchar_t kSpeed[] = L"TheJockeyClub\\learning\\TheJockeyClub-speed-learning-v4.dat";
constexpr wchar_t kActiveJournal[] = L".TheJockeyClub-install-active.tsv";
constexpr wchar_t kRestoreReady[] = L".TheJockeyClub-install-restore-ready.tsv";
constexpr std::size_t kMaxManifest = 4 * 1024 * 1024;
constexpr std::size_t kMaxEntries = 8192;

struct Failure { std::wstring message; };

[[noreturn]] void Fail(const std::wstring& text) { throw Failure{text}; }

std::wstring WindowsError(DWORD code) {
    wchar_t* buffer = nullptr;
    const DWORD count = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
        code, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    std::wstring message = count && buffer ? std::wstring(buffer, count) : L"Unknown Windows error";
    if (buffer) LocalFree(buffer);
    while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n')) message.pop_back();
    return message + L" (" + std::to_wstring(code) + L")";
}

[[noreturn]] void WinFail(const std::wstring& context, DWORD code = GetLastError()) {
    Fail(context + L": " + WindowsError(code));
}

class Handle {
public:
    Handle() = default;
    explicit Handle(HANDLE handle) : handle_(handle) {}
    ~Handle() { reset(); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept : handle_(std::exchange(other.handle_, INVALID_HANDLE_VALUE)) {}
    Handle& operator=(Handle&& other) noexcept {
        if (this != &other) { reset(); handle_ = std::exchange(other.handle_, INVALID_HANDLE_VALUE); }
        return *this;
    }
    HANDLE get() const { return handle_; }
    bool valid() const { return handle_ != INVALID_HANDLE_VALUE && handle_ != nullptr; }
    void reset() { if (valid()) CloseHandle(handle_); handle_ = INVALID_HANDLE_VALUE; }
private:
    HANDLE handle_ = INVALID_HANDLE_VALUE;
};

struct CaseLess {
    bool operator()(const std::wstring& a, const std::wstring& b) const {
        return CompareStringOrdinal(a.data(), static_cast<int>(a.size()), b.data(),
            static_cast<int>(b.size()), TRUE) == CSTR_LESS_THAN;
    }
};

bool Same(const std::wstring& a, const std::wstring& b) {
    return !CaseLess{}(a, b) && !CaseLess{}(b, a);
}

bool ContainsPath(const std::wstring& parent, const std::wstring& child) {
    if (Same(parent, child)) return true;
    const std::wstring prefix = parent.back() == L'\\' ? parent : parent + L'\\';
    return child.size() > prefix.size() && Same(prefix, child.substr(0, prefix.size()));
}

std::wstring Wide(const std::string& value) {
    if (value.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0);
    if (!size) Fail(L"The manifest contains invalid UTF-8.");
    std::wstring result(size, L'\0');
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), result.data(), size)) WinFail(L"Decode UTF-8");
    return result;
}

std::string Utf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (!size) return "Unable to encode installer message.";
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
    return result;
}

void Print(const std::wstring& message, bool error = false) {
    const std::string bytes = Utf8(message + L"\r\n");
    HANDLE output = GetStdHandle(error ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE);
    if (output && output != INVALID_HANDLE_VALUE) {
        DWORD mode = 0, written = 0;
        if (GetConsoleMode(output, &mode)) {
            const auto text = message + L"\r\n";
            WriteConsoleW(output, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
        } else {
            WriteFile(output, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
        }
    }
}

void CheckComponent(const std::wstring& value) {
    if (value.empty() || value == L"." || value == L".." || value.back() == L'.' || value.back() == L' ')
        Fail(L"Unsafe path component: " + value);
    for (wchar_t c : value)
        if (c < 32 || std::wstring(L"<>:\"/\\|?*").find(c) != std::wstring::npos)
            Fail(L"Unsafe path component: " + value);
    std::wstring stem = value.substr(0, value.find(L'.'));
    std::transform(stem.begin(), stem.end(), stem.begin(), [](wchar_t c) { return static_cast<wchar_t>(towupper(c)); });
    if (stem == L"CON" || stem == L"PRN" || stem == L"AUX" || stem == L"NUL" ||
        stem == L"CONIN$" || stem == L"CONOUT$" || stem == L"CLOCK$" ||
        (stem.size() == 4 && (stem.substr(0, 3) == L"COM" || stem.substr(0, 3) == L"LPT") &&
         ((stem[3] >= L'0' && stem[3] <= L'9') || stem[3] == L'\u00b9' || stem[3] == L'\u00b2' || stem[3] == L'\u00b3')))
        Fail(L"Reserved Windows path component: " + value);
}

void CheckComponents(const std::wstring& path, std::size_t begin) {
    while (begin < path.size()) {
        const auto end = path.find(L'\\', begin);
        CheckComponent(path.substr(begin, end == std::wstring::npos ? end : end - begin));
        if (end == std::wstring::npos) break;
        begin = end + 1;
    }
}

std::wstring FullPath(std::wstring input) {
    std::replace(input.begin(), input.end(), L'/', L'\\');
    if (input.rfind(L"\\\\?\\", 0) == 0) input.erase(0, 4);
    if (input.rfind(L"\\\\", 0) == 0 || input.find(L'\0') != std::wstring::npos)
        Fail(L"Choose a local drive folder; device and network paths are not supported.");
    std::vector<wchar_t> buffer(32768);
    const DWORD size = GetFullPathNameW(input.c_str(), static_cast<DWORD>(buffer.size()), buffer.data(), nullptr);
    if (!size || size >= buffer.size()) WinFail(L"Resolve folder path", size ? ERROR_FILENAME_EXCED_RANGE : GetLastError());
    std::wstring result(buffer.data(), size);
    while (result.size() > 3 && result.back() == L'\\') result.pop_back();
    if (result.size() < 3 || !iswalpha(result[0]) || result[1] != L':' || result[2] != L'\\')
        Fail(L"Choose a local drive folder.");
    CheckComponents(result, 3);
    const UINT drive = GetDriveTypeW(result.substr(0, 3).c_str());
    if (drive != DRIVE_FIXED && drive != DRIVE_REMOVABLE) Fail(L"Only local fixed or removable drives are supported.");
    return result;
}

std::wstring Extended(const std::wstring& path) {
    if (path.size() > 32700) Fail(L"Path is too long: " + path.substr(0, 160));
    return L"\\\\?\\" + path;
}

std::wstring Join(const std::wstring& root, const std::wstring& relative) {
    std::wstring result = root + (root.back() == L'\\' ? L"" : L"\\") + relative;
    if (!ContainsPath(root, result)) Fail(L"Path escaped its approved folder.");
    Extended(result);
    return result;
}

std::wstring Parent(const std::wstring& path) {
    const auto split = path.find_last_of(L'\\');
    return path.substr(0, split == 2 ? 3 : split);
}

DWORD Attributes(const std::wstring& path) {
    const DWORD value = GetFileAttributesW(Extended(path).c_str());
    if (value == INVALID_FILE_ATTRIBUTES) {
        const DWORD error = GetLastError();
        if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND) WinFail(L"Inspect " + path, error);
    } else if (value & FILE_ATTRIBUTE_REPARSE_POINT) {
        Fail(L"Reparse points are not allowed: " + path);
    }
    return value;
}

class DirectoryGuards {
public:
    // Retain non-share-write/delete handles to each existing ancestor. A checked
    // directory cannot subsequently be swapped for a junction during this run.
    bool check(const std::wstring& directory, bool create) {
        std::size_t end = 3;
        while (true) {
            const std::wstring part = directory.substr(0, end);
            if (!held_.count(part)) {
                DWORD attributes = Attributes(part);
                if (attributes == INVALID_FILE_ATTRIBUTES) {
                    if (!create) return false;
                    if (CreateDirectoryW(Extended(part).c_str(), nullptr)) created_.push_back(part);
                    else if (GetLastError() != ERROR_ALREADY_EXISTS) WinFail(L"Create directory " + part);
                    attributes = Attributes(part);
                }
                if (!(attributes & FILE_ATTRIBUTE_DIRECTORY)) Fail(L"A parent path is not a directory: " + part);
                Handle handle(CreateFileW(Extended(part).c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ,
                    nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
                if (!handle.valid()) WinFail(L"Lock directory against replacement " + part);
                BY_HANDLE_FILE_INFORMATION information{};
                if (!GetFileInformationByHandle(handle.get(), &information)) WinFail(L"Inspect locked directory " + part);
                if (!(information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
                    Fail(L"Unsafe directory: " + part);
                held_.emplace(part, std::move(handle));
            }
            if (end == directory.size()) return true;
            const auto next = directory.find(L'\\', end == 3 ? 3 : end + 1);
            end = next == std::wstring::npos ? directory.size() : next;
        }
    }
    void removeCreatedEmptyDirectories() {
        for (auto i = created_.rbegin(); i != created_.rend(); ++i) {
            held_.erase(*i);
            if (!RemoveDirectoryW(Extended(*i).c_str())) {
                const DWORD error = GetLastError();
                if (error != ERROR_DIR_NOT_EMPTY && error != ERROR_PATH_NOT_FOUND && error != ERROR_FILE_NOT_FOUND)
                    Print(L"Could not remove newly created empty directory: " + *i + L": " + WindowsError(error), true);
            }
        }
        created_.clear();
    }
    void release(const std::wstring& directory) { held_.erase(directory); }
private:
    std::map<std::wstring, Handle, CaseLess> held_;
    std::vector<std::wstring> created_;
};

Handle ReadLocked(const std::wstring& path, DirectoryGuards& guards) {
    if (!guards.check(Parent(path), false)) Fail(L"Required source folder does not exist: " + Parent(path));
    const DWORD attributes = Attributes(path);
    if (attributes == INVALID_FILE_ATTRIBUTES || attributes & FILE_ATTRIBUTE_DIRECTORY) Fail(L"Required regular file is missing: " + path);
    Handle handle(CreateFileW(Extended(path).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    if (!handle.valid()) WinFail(L"Open and lock " + path);
    BY_HANDLE_FILE_INFORMATION information{};
    if (!GetFileInformationByHandle(handle.get(), &information)) WinFail(L"Inspect " + path);
    if (information.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) Fail(L"Required source is not a regular file: " + path);
    return handle;
}

std::uint64_t FileSize(HANDLE file) {
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0) WinFail(L"Read source file size");
    return static_cast<std::uint64_t>(size.QuadPart);
}

void Rewind(HANDLE file) {
    LARGE_INTEGER zero{};
    if (!SetFilePointerEx(file, zero, nullptr, FILE_BEGIN)) WinFail(L"Rewind source file");
}

std::string ReadSmall(HANDLE file) {
    const auto size = FileSize(file);
    if (size > kMaxManifest) Fail(L"Install manifest exceeds the 4 MiB limit.");
    std::string result(static_cast<std::size_t>(size), '\0');
    DWORD read = 0;
    if (!ReadFile(file, result.data(), static_cast<DWORD>(result.size()), &read, nullptr) || read != result.size()) WinFail(L"Read install manifest");
    return result;
}

class Sha256 {
public:
    Sha256() {
        if (BCryptOpenAlgorithmProvider(&algorithm_, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) Fail(L"Open Windows SHA256 provider failed.");
    }
    ~Sha256() { if (algorithm_) BCryptCloseAlgorithmProvider(algorithm_, 0); }
    std::string file(HANDLE file) {
        DWORD bytes = 0, object_size = 0;
        if (BCryptGetProperty(algorithm_, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&object_size), sizeof(object_size), &bytes, 0) < 0)
            Fail(L"Read SHA256 provider properties failed.");
        std::vector<UCHAR> object(object_size);
        BCRYPT_HASH_HANDLE hash = nullptr;
        if (BCryptCreateHash(algorithm_, &hash, object.data(), object_size, nullptr, 0, 0) < 0) Fail(L"Create SHA256 state failed.");
        try {
            Rewind(file);
            std::array<UCHAR, 65536> buffer{};
            DWORD count = 0;
            do {
                if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &count, nullptr)) WinFail(L"Read payload for SHA256");
                if (count && BCryptHashData(hash, buffer.data(), count, 0) < 0) Fail(L"SHA256 update failed.");
            } while (count);
            std::array<UCHAR, 32> value{};
            if (BCryptFinishHash(hash, value.data(), static_cast<ULONG>(value.size()), 0) < 0) Fail(L"SHA256 finalization failed.");
            BCryptDestroyHash(hash);
            hash = nullptr;
            constexpr char digits[] = "0123456789abcdef";
            std::string result;
            for (UCHAR c : value) { result += digits[c >> 4]; result += digits[c & 15]; }
            return result;
        } catch (...) {
            if (hash) BCryptDestroyHash(hash);
            throw;
        }
    }
private:
    BCRYPT_ALG_HANDLE algorithm_ = nullptr;
};

enum class Mode { Replace, Preserve, SeedSpeed };
bool Under(const std::wstring& path, const std::wstring& directory) {
    return path.size() > directory.size() && path[directory.size()] == L'\\' && Same(path.substr(0, directory.size()), directory);
}

bool UserDataPath(const std::wstring& path) {
    for (const auto* directory : {L"TheJockeyClub\\config", L"TheJockeyClub\\courses", L"TheJockeyClub\\learning",
             L"TheJockeyClub\\data", L"TheJockeyClub\\recordings", L"TheJockeyClub\\logs",
             L"TheJockeyClub\\diagnostics", L"TheJockeyClub\\cache"})
        if (Under(path, directory)) return true;
    return false;
}

bool PayloadPath(const std::wstring& path) {
    return Same(path, L"TheJockeyClub.asi") || Same(path, L"TheJockeyClub\\README.txt") ||
        Same(path, L"TheJockeyClub\\Clubhouse.exe") || Under(path, L"lml\\TheJockeyClub Text") ||
        UserDataPath(path) || Under(path, L"TheJockeyClub\\tools\\Clubhouse");
}

struct Entry {
    std::wstring relative;
    std::string hash;
    std::uint64_t size = 0;
    Mode mode = Mode::Replace;
    Handle source;
    bool skip = false;
    bool legacy_seed = false;
    bool had_destination = false;
    std::uint64_t original_size = 0;
    std::string original_hash;
};

std::vector<Entry> ParseManifest(std::string text) {
    if (text.rfind("\xef\xbb\xbf", 0) == 0) text.erase(0, 3);
    std::vector<Entry> entries;
    std::set<std::wstring, CaseLess> paths;
    std::size_t begin = 0, line_number = 0;
    while (begin < text.size()) {
        const auto end = text.find('\n', begin);
        std::string line = text.substr(begin, end == std::string::npos ? end : end - begin);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        begin = end == std::string::npos ? text.size() : end + 1;
        ++line_number;
        if (line_number == 1) {
            if (line != "TJC_INSTALL_MANIFEST 2") Fail(L"Unsupported install manifest header; this installer requires layout 2.");
            continue;
        }
        if (line.empty()) Fail(L"Empty manifest row at line " + std::to_wstring(line_number));
        std::array<std::string, 4> fields;
        std::size_t field_begin = 0;
        for (std::size_t i = 0; i < 4; ++i) {
            const auto tab = line.find('\t', field_begin);
            if ((i < 3 && tab == std::string::npos) || (i == 3 && tab != std::string::npos)) Fail(L"Manifest row needs four TSV columns at line " + std::to_wstring(line_number));
            fields[i] = line.substr(field_begin, tab == std::string::npos ? tab : tab - field_begin);
            field_begin = tab == std::string::npos ? line.size() : tab + 1;
        }
        Entry item;
        item.hash = fields[0];
        if (item.hash.size() != 64 || item.hash.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) Fail(L"Invalid SHA256 at manifest line " + std::to_wstring(line_number));
        std::transform(item.hash.begin(), item.hash.end(), item.hash.begin(), [](char c) { return static_cast<char>(c >= 'A' && c <= 'F' ? c + ('a' - 'A') : c); });
        if (fields[1].empty()) Fail(L"Missing size at manifest line " + std::to_wstring(line_number));
        for (char c : fields[1]) {
            if (c < '0' || c > '9' || item.size > (std::numeric_limits<std::uint64_t>::max() - (c - '0')) / 10) Fail(L"Invalid size at manifest line " + std::to_wstring(line_number));
            item.size = item.size * 10 + (c - '0');
        }
        if (fields[2] == "replace") item.mode = Mode::Replace;
        else if (fields[2] == "preserve") item.mode = Mode::Preserve;
        else if (fields[2] == "seed-speed") item.mode = Mode::SeedSpeed;
        else Fail(L"Unknown install mode at manifest line " + std::to_wstring(line_number));
        item.relative = Wide(fields[3]);
        std::replace(item.relative.begin(), item.relative.end(), L'/', L'\\');
        if (item.relative.empty() || item.relative.back() == L'\\') Fail(L"Manifest paths must name files.");
        CheckComponents(item.relative, 0);
        if (!PayloadPath(item.relative)) Fail(L"Manifest destination is outside the explicit mod layout: " + item.relative);
        if (item.mode == Mode::Replace && UserDataPath(item.relative)) Fail(L"Player data must use preserve or seed-speed mode: " + item.relative);
        if (item.mode == Mode::SeedSpeed && !Same(item.relative, kSpeed)) Fail(L"seed-speed is only valid for the consolidated v4 speed seed.");
        if (!paths.insert(item.relative).second) Fail(L"Duplicate manifest destination: " + item.relative);
        entries.push_back(std::move(item));
        if (entries.size() > kMaxEntries) Fail(L"Manifest contains too many entries.");
    }
    if (!line_number || entries.empty()) Fail(L"Install manifest contains no files.");
    for (const auto& path : paths) {
        auto cursor = path.find(L'\\');
        while (cursor != std::wstring::npos) {
            if (paths.count(path.substr(0, cursor))) Fail(L"Manifest file conflicts with a parent directory: " + path);
            cursor = path.find(L'\\', cursor + 1);
        }
    }
    return entries;
}

void RequireGameStopped(const std::wstring& target) {
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (!snapshot.valid()) WinFail(L"Check whether RDR2 is running");
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (!Process32FirstW(snapshot.get(), &entry)) WinFail(L"Read running process list");
    do {
        if (Same(entry.szExeFile, L"RDR2.exe")) Fail(L"Red Dead Redemption 2 is running. Close the game before installing.");
        if (Same(entry.szExeFile, L"JockeyClub.exe") || Same(entry.szExeFile, L"Clubhouse.exe"))
            Fail(L"Clubhouse is running. Close its launcher and backend before installing or restoring.");
        if (Same(entry.szExeFile, L"python.exe") || Same(entry.szExeFile, L"pythonw.exe")) {
            Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID));
            if (process.valid()) {
                std::vector<wchar_t> name(32768); DWORD length = static_cast<DWORD>(name.size());
                if (QueryFullProcessImageNameW(process.get(), 0, name.data(), &length)) {
                    const std::wstring executable(name.data(), length);
                    if (ContainsPath(Join(target, L"JockeyClub"), executable) ||
                        ContainsPath(Join(target, L"TheJockeyClub\\tools\\Clubhouse"), executable))
                        Fail(L"A bundled Clubhouse backend is running (PID " + std::to_wstring(entry.th32ProcessID) + L"). Close Clubhouse before installing or restoring.");
                }
            }
        }
    } while (Process32NextW(snapshot.get(), &entry));
    if (GetLastError() != ERROR_NO_MORE_FILES) WinFail(L"Finish checking running processes");
}

bool SpeedExists(const std::wstring& target) {
    bool found = false;
    for (const wchar_t* name : {L"TheJockeyClub-speed-learning-v4.dat", L"TheJockeyClub-speed-learning-v3.dat", L"TheJockeyClub-speed-learning-v2.dat"}) {
        for (const auto& relative : {std::wstring(name), std::wstring(L"TheJockeyClub\\learning\\") + name}) {
            const DWORD attributes = Attributes(Join(target, relative));
            if (attributes == INVALID_FILE_ATTRIBUTES) continue;
            if (attributes & FILE_ATTRIBUTE_DIRECTORY) Fail(L"Learning history path is a directory, not a regular file: " + relative);
            found = true;
        }
    }
    return found;
}

bool PreserveNow(const Entry& item, const std::wstring& target, DirectoryGuards& guards) {
    const auto destination = Join(target, item.relative);
    const bool parent_exists = guards.check(Parent(destination), false);
    if (item.mode == Mode::SeedSpeed && SpeedExists(target)) return true;
    if (!parent_exists) return false;
    const DWORD attributes = Attributes(destination);
    if (attributes != INVALID_FILE_ATTRIBUTES && attributes & FILE_ATTRIBUTE_DIRECTORY)
        Fail(L"A file destination is occupied by a directory: " + item.relative);
    if (item.mode == Mode::Preserve && attributes != INVALID_FILE_ATTRIBUTES) return true;
    if (attributes != INVALID_FILE_ATTRIBUTES) {
        if (attributes & FILE_ATTRIBUTE_DIRECTORY) Fail(L"A file destination is occupied by a directory: " + item.relative);
        if (attributes & FILE_ATTRIBUTE_READONLY) Fail(L"A replacement destination is read-only: " + item.relative);
    }
    return false;
}

void CopyToTemporary(HANDLE source, HANDLE destination, const std::wstring& relative) {
    Rewind(source);
    std::array<unsigned char, 65536> buffer{};
    DWORD read = 0;
    do {
        if (!ReadFile(source, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr)) WinFail(L"Read payload " + relative);
        DWORD offset = 0;
        while (offset < read) {
            DWORD written = 0;
            if (!WriteFile(destination, buffer.data() + offset, read - offset, &written, nullptr) || !written) WinFail(L"Write temporary file for " + relative);
            offset += written;
        }
    } while (read);
    if (!FlushFileBuffers(destination)) WinFail(L"Flush temporary file for " + relative);
}

std::pair<std::wstring, Handle> ReserveTemporary(const std::wstring& directory, std::uint64_t& serial) {
    for (int attempt = 0; attempt < 16; ++attempt) {
        auto path = Join(directory, L".tjc-install-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(++serial) + L".tmp");
        Handle file(CreateFileW(Extended(path).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        if (file.valid()) return {std::move(path), std::move(file)};
        const DWORD error = GetLastError();
        if (error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS) WinFail(L"Create transaction staging file", error);
    }
    Fail(L"Could not reserve a transaction staging file.");
}

bool RemoveOwnedTemporary(const std::wstring& path) {
    if (path.empty() || DeleteFileW(Extended(path).c_str())) return true;
    const DWORD error = GetLastError();
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return true;
    Print(L"Could not remove this transaction's temporary file: " + path + L": " + WindowsError(error), true);
    return false;
}

struct Counts {
    std::size_t installed = 0;
    std::size_t preserved = 0;
    std::size_t rolled_back = 0;
    std::size_t rollback_failed = 0;
    std::size_t cleanup_failed = 0;
};

#include "release_installer_migration.inl"

std::wstring ExecutableDirectory() {
    std::vector<wchar_t> buffer(32768);
    const DWORD size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (!size || size >= buffer.size()) WinFail(L"Locate installer executable");
    return Parent(FullPath(std::wstring(buffer.data(), size)));
}

std::wstring ChooseDirectory() {
    IFileDialog* dialog = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
    if (FAILED(hr)) Fail(L"Could not open the Windows folder picker.");
    FILEOPENDIALOGOPTIONS options{};
    hr = dialog->GetOptions(&options);
    if (SUCCEEDED(hr)) hr = dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR | FOS_DONTADDTORECENT);
    if (SUCCEEDED(hr)) hr = dialog->SetTitle(L"Select the Red Dead Redemption 2 folder containing RDR2.exe");
    if (SUCCEEDED(hr)) hr = dialog->Show(nullptr);
    if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED)) { dialog->Release(); return {}; }
    IShellItem* item = nullptr;
    if (SUCCEEDED(hr)) hr = dialog->GetResult(&item);
    PWSTR path = nullptr;
    if (SUCCEEDED(hr)) hr = item->GetDisplayName(SIGDN_FILESYSPATH, &path);
    std::wstring result = path ? path : L"";
    if (path) CoTaskMemFree(path);
    if (item) item->Release();
    dialog->Release();
    if (FAILED(hr) || result.empty()) Fail(L"Could not read the selected folder.");
    return result;
}

int Run(int argc, wchar_t** argv) {
    bool cli = argc > 1, dry_run = false, help = false, recover_only = false, restore_legacy = false;
    std::wstring target;
    Counts counts;
    bool writes_started = false;
    HRESULT com = E_FAIL;
    try {
        for (int i = 1; i < argc; ++i) {
            const std::wstring option = argv[i];
            if (option == L"--target" && i + 1 < argc && target.empty()) target = argv[++i];
            else if (option == L"--dry-run" && !dry_run) dry_run = true;
            else if (option == L"--recover-only" && !recover_only) recover_only = true;
            else if (option == L"--restore-legacy" && !restore_legacy) restore_legacy = true;
            else if (option == L"--help" || option == L"-h") help = true;
            else Fail(L"Unknown or incomplete option: " + option);
        }
        if (help) { Print(L"Usage: installer.exe [--target <folder-containing-RDR2.exe> [--dry-run] [--recover-only | --restore-legacy]]\nNo arguments opens the folder picker. --target is non-interactive. --dry-run makes no target changes.\n--recover-only resolves an interrupted transaction without installing.\n--restore-legacy restores the retained first migration after verifying current files; later user changes cause a conflict."); return 0; }
        if (recover_only && restore_legacy) Fail(L"Choose either --recover-only or --restore-legacy.");
        if (cli && target.empty()) Fail(L"CLI operation requires --target <folder-containing-RDR2.exe>.");
        if (!cli) {
            com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
            if (FAILED(com)) Fail(L"Could not initialize the Windows folder picker.");
            target = ChooseDirectory();
            if (target.empty()) { CoUninitialize(); return 0; }
        }
        target = FullPath(target);
        const std::wstring source = ExecutableDirectory();
        if (ContainsPath(source, target) || ContainsPath(target, source)) Fail(L"The game folder and installer package folder must be separate; neither may contain the other.");
        DirectoryGuards guards;
        if (!guards.check(source, false) || !guards.check(target, false)) Fail(L"Installer and game folders must already exist.");
        const DWORD game_attributes = Attributes(Join(target, L"RDR2.exe"));
        if (game_attributes == INVALID_FILE_ATTRIBUTES || game_attributes & FILE_ATTRIBUTE_DIRECTORY) Fail(L"The selected folder does not contain a regular RDR2.exe file.");
        if (Attributes(Join(target, L"HorseSpeedLab.asi")) != INVALID_FILE_ATTRIBUTES)
            Fail(L"An older HorseSpeedLab.asi is present. Move that old mod out of the game folder before installing The Jockey Club. The installer will not remove it for you.");
        RequireGameStopped(target);
        ClubhouseLease clubhouse;
        clubhouse.acquire(target);
        Sha256 hashes;
        Handle install_lock;
        auto acquire_lock = [&] {
            if (install_lock.valid()) return;
            RequireGameStopped(target);
            const auto lock_path = Join(target, L".TheJockeyClub-install.lock");
            Attributes(lock_path);
            install_lock = Handle(CreateFileW(Extended(lock_path).c_str(), GENERIC_WRITE | DELETE, 0, nullptr, CREATE_NEW,
                FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
            if (!install_lock.valid()) WinFail(L"Reserve installation lock (another installation or an existing lock may be present)");
            writes_started = true;
        };
        const bool interrupted = Attributes(Join(target, kActiveJournal)) != INVALID_FILE_ATTRIBUTES;
        const bool restore_preparation = Attributes(Join(target, kRestoreReady)) != INVALID_FILE_ATTRIBUTES;
        if (interrupted && restore_preparation) Fail(L"Both active and restore-preparation journals exist. Retain both for inspection; no files were changed.");
        if (restore_preparation) {
            if (dry_run) { RecoverRestoreReady(target, guards, true); return 0; }
            acquire_lock(); RecoverRestoreReady(target, guards, false);
        }
        if (interrupted) {
            if (dry_run) {
                const auto text = ReadJournal(Join(target, kActiveJournal), guards);
                if (!IncompleteInitialHeader(text)) {
                    const auto journal = ParseJournal(text);
                    if (!journal.committed || journal.restore) VerifyRollback(journal, target, guards, hashes);
                }
                Print(L"DRY RUN OK. An interrupted transaction needs recovery; run --recover-only without --dry-run. No target changes were made.");
                return 0;
            }
            acquire_lock();
            RecoverActive(target, guards, hashes, counts);
            if (restore_legacy) { Print(L"Pending transaction recovered. Run --restore-legacy again to request the retained baseline explicitly."); return 0; }
        }
        if (recover_only) { Print(interrupted || restore_preparation ? L"Requested recovery completed." : L"No interrupted installation journal is present."); return 0; }
        if (restore_legacy) {
            // Validate first so a conflict even avoids creating the transient lock.
            if (dry_run) RestoreLegacy(target, guards, hashes, counts, true);
            else {
                const auto journal = ParseJournal(ReadJournal(Join(target, std::wstring(kBaseline) + L"\\journal.tsv"), guards));
                VerifyRollback(journal, target, guards, hashes);
                acquire_lock(); RestoreLegacy(target, guards, hashes, counts, false);
            }
            return 0;
        }
        const std::wstring payload = Join(source, L"payload");
        Handle manifest = ReadLocked(Join(payload, kManifest), guards);
        auto entries = ParseManifest(ReadSmall(manifest.get()));
        auto migration = PlanMigration(target, guards, hashes);
        if ((!migration.files.empty() || !migration.directories.empty()) &&
            Attributes(Join(target, std::wstring(kBaseline) + L"\\journal.tsv")) != INVALID_FILE_ATTRIBUTES)
            Fail(L"Legacy paths reappeared after the retained baseline. Resolve these paths before installing; a duplicate migration backup will not be created.");
        constexpr wchar_t main_config[] = L"TheJockeyClub\\config\\TheJockeyClub.ini";
        const MigrationFile* adoption = nullptr;
        if (!migration.destinations.count(main_config) && Attributes(Join(target, main_config)) == INVALID_FILE_ATTRIBUTES) {
            for (const auto& old : migration.files) if (Same(old.old_path, L"HorseSpeedLab.ini")) adoption = &old;
        }
        bool main_entry = false;
        std::uint64_t required_space = 0;
        std::size_t planned_preserved = 0;
        for (auto& entry : entries) {
            try {
                entry.source = ReadLocked(Join(payload, entry.relative), guards);
                if (FileSize(entry.source.get()) != entry.size) Fail(L"Payload size does not match manifest.");
                if (hashes.file(entry.source.get()) != entry.hash) Fail(L"Payload SHA256 does not match manifest.");
                if (Same(entry.relative, main_config)) {
                    main_entry = true;
                    if (adoption) {
                        entry.source = ReadLocked(Join(target, adoption->old_path), guards);
                        entry.hash = adoption->bytes.hash; entry.size = adoption->bytes.size;
                        entry.mode = Mode::Preserve; entry.legacy_seed = true;
                        Print(L"ADOPT LEGACY CONFIG HorseSpeedLab.ini -> " + std::wstring(main_config) + L"; original retained in config\\legacy.");
                    }
                }
                entry.skip = PreserveNow(entry, target, guards);
                if (entry.mode != Mode::Replace && migration.destinations.count(entry.relative)) entry.skip = true;
                if (entry.mode == Mode::Replace && !entry.skip) {
                    const auto planned = migration.destinations.find(entry.relative);
                    Fingerprint previous;
                    if (planned != migration.destinations.end()) {
                        previous = migration.files[planned->second].bytes; entry.had_destination = true;
                    }
                    else if (guards.check(Parent(Join(target, entry.relative)), false) && Attributes(Join(target, entry.relative)) != INVALID_FILE_ATTRIBUTES) {
                        auto current = ReadLocked(Join(target, entry.relative), guards);
                        previous = FingerprintOf(current.get(), hashes); entry.had_destination = true;
                    }
                    if (entry.had_destination) {
                        entry.original_size = previous.size; entry.original_hash = previous.hash;
                        entry.skip = Equal(previous, {entry.size, entry.hash});
                    }
                }
                if (entry.skip) ++planned_preserved;
                else {
                    if (required_space > std::numeric_limits<std::uint64_t>::max() - entry.size) Fail(L"Payload total size overflow.");
                    required_space += entry.size;
                }
            } catch (const Failure& failure) { Fail(L"Preflight failed for " + entry.relative + L": " + failure.message); }
        }
        if (adoption && !main_entry) {
            Entry entry; entry.relative = main_config; entry.mode = Mode::Preserve;
            entry.source = ReadLocked(Join(target, adoption->old_path), guards); entry.hash = adoption->bytes.hash;
            entry.size = adoption->bytes.size; entry.legacy_seed = true;
            if (required_space > std::numeric_limits<std::uint64_t>::max() - entry.size) Fail(L"Legacy config size overflow.");
            required_space += entry.size; entries.push_back(std::move(entry));
        }
        ValidateOverlayDependencies(entries, migration, target, guards);
        ULARGE_INTEGER available{};
        if (!GetDiskFreeSpaceExW(Extended(target).c_str(), &available, nullptr, nullptr)) WinFail(L"Check available disk space");
        if (available.QuadPart < required_space) Fail(L"Insufficient free space for the validated installation.");
        const std::wstring plan = L"Target: " + target + L"\nValidated files: " + std::to_wstring(entries.size()) +
            L"\nTo install/update: " + std::to_wstring(entries.size() - planned_preserved) +
            L"\nExisting or identical files preserved: " + std::to_wstring(planned_preserved) +
            L"\nLegacy files to migrate: " + std::to_wstring(migration.files.size());
        Print(plan);
        if (dry_run) { Print(L"DRY RUN OK. Installed: 0; preserved (planned): " + std::to_wstring(planned_preserved) + L". No target changes were made."); return 0; }
        if (!cli && MessageBoxW(nullptr, (plan + L"\n\nInstall these verified files and migrate the explicit legacy paths? Player files are preserved. A durable journal supports interrupted-operation recovery. The first legacy migration retains a guarded --restore-legacy baseline.").c_str(), kTitle, MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) { CoUninitialize(); return 0; }
        acquire_lock();
        InstallTransaction(entries, migration, target, guards, hashes, counts);
        const std::wstring result = L"Installation complete. Installed: " + std::to_wstring(counts.installed) + L"; preserved: " + std::to_wstring(counts.preserved) + L".\nThe game was not started." +
            (counts.cleanup_failed ? L"\nSome temporary backup files could not be removed; cleanup failures: " + std::to_wstring(counts.cleanup_failed) : L"");
        Print(result);
        if (!cli) MessageBoxW(nullptr, result.c_str(), kTitle, MB_OK | MB_ICONINFORMATION);
        if (SUCCEEDED(com)) CoUninitialize();
        return counts.cleanup_failed ? 1 : 0;
    } catch (const Failure& failure) {
        std::wstring message = failure.message + L"\nInstalled (remaining): " + std::to_wstring(counts.installed) + L"; preserved: " + std::to_wstring(counts.preserved) + L"; rolled back: " + std::to_wstring(counts.rolled_back) + L"; rollback failures: " + std::to_wstring(counts.rollback_failed) + L".";
        message += writes_started ? L"\nOperation stopped. Retain any active/preparation journal and install-state files; --recover-only reports or completes pending recovery." : L"\nPreflight stopped before installing or migrating any files.";
        Print(message, true);
        if (!cli) MessageBoxW(nullptr, message.c_str(), kTitle, MB_OK | MB_ICONERROR);
        if (SUCCEEDED(com)) CoUninitialize();
        return 1;
    } catch (...) {
        Print(L"Unexpected installer failure. Installed (remaining): " + std::to_wstring(counts.installed) + L"; preserved: " + std::to_wstring(counts.preserved) + L"; rolled back: " + std::to_wstring(counts.rolled_back) + L"; rollback failures: " + std::to_wstring(counts.rollback_failed) + L".", true);
        if (!cli) MessageBoxW(nullptr, L"An unexpected installer error occurred. Retain .TheJockeyClub-install-active.tsv and TheJockeyClub/install-state. Run --recover-only with the game and Clubhouse stopped.", kTitle, MB_OK | MB_ICONERROR);
        if (SUCCEEDED(com)) CoUninitialize();
        return 1;
    }
}
} // namespace

int wmain(int argc, wchar_t** argv) { return Run(argc, argv); }

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) { MessageBoxW(nullptr, L"Could not read installer arguments.", kTitle, MB_OK | MB_ICONERROR); return 1; }
    const int result = Run(argc, argv);
    LocalFree(argv);
    return result;
}
