#pragma once
#include <array>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>
#include <string_view>
#include <windows.h>

namespace horse_races::log_sink {
// Every line the mod writes goes through one handle, open for as long as the game runs.
//
// Opening and closing the file around each line cost 50-150 us of the game's own thread, on top of
// a file_size call to decide about rotation - and the mod wrote 96-102 lines inside one second more
// than once (code review, 2026-09-21, finding #7). WriteFile hands the bytes to the OS, which keeps
// them even if the game crashes afterwards; that is all the log is promised to do (R-TELE-05), so
// nothing is flushed to the disk per line.
//
// The script thread is the only one that writes, but the lock is kept: it costs nothing uncontended
// and a stray writer would otherwise interleave two lines.
inline constexpr std::uint64_t kRotateBytes = 16ull * 1024 * 1024;
inline constexpr std::uint64_t kRetryRotationMs = 5000;
inline constexpr std::size_t kMaxLineBytes = 64 * 1024;
inline constexpr int kLinesPerSecond = 2;     // Per call site, before folding.
inline constexpr std::size_t kSites = 64;     // How many call sites are watched at once.
// How much of a line names the place it came from, after its module tag. Twenty-four was short
// enough that a whole module came out as one key.
inline constexpr std::size_t kKeyChars = 48;

class Sink {
public:
    // Idempotent: the same path twice keeps the handle that is already open.
    void Open(std::string path) {
        std::lock_guard<std::mutex> hold(lock_);
        if (path == path_) return;
        CloseHandleLocked();
        path_ = std::move(path);
        retry_rotation_at_ = 0;
    }
    // `always` is for lines that must not be folded however often they come: what ended a race, what
    // a check refused, what a stage did, what was lent to whom (R-TELE-05/06).
    void Write(std::string_view text, bool always = false) {
        std::lock_guard<std::mutex> hold(lock_);
        if (path_.empty()) return;
        // Keep the active generation bounded even if a caller supplies an anomalously large line.
        if (text.size() > kMaxLineBytes) return;
        if (retry_rotation_at_ && GetTickCount64() < retry_rotation_at_) return;
        if (handle_ == INVALID_HANDLE_VALUE && !OpenHandleLocked()) return;
        SYSTEMTIME now{};
        GetLocalTime(&now);
        int dropped = 0;
        if (!always && folding_) {
            Site& site = SiteFor(text, now.wHour * 3600 + now.wMinute * 60 + now.wSecond);
            if (site.written >= kLinesPerSecond) {
                ++site.dropped;
                return;
            }
            ++site.written;
            dropped = site.dropped;
            site.dropped = 0;
        }
        // Moved aside before the line that finds it too big, so the line lands in the fresh log.
        if ((bytes_ >= kRotateBytes || text.size() + 128 > kRotateBytes - bytes_) && !RotateLocked()) {
            retry_rotation_at_ = GetTickCount64() + kRetryRotationMs;
            return;
        }
        retry_rotation_at_ = 0;
        PutLocked(now, dropped, text);
    }
    // For the crash handler: the OS keeps what WriteFile was given, the disk does not until this.
    void Flush() {
        std::lock_guard<std::mutex> hold(lock_);
        if (handle_ != INVALID_HANDLE_VALUE) FlushFileBuffers(handle_);
    }
    void Close() {
        std::lock_guard<std::mutex> hold(lock_);
        CloseHandleLocked();
    }
    void SetFolding(bool on) {
        std::lock_guard<std::mutex> hold(lock_);
        folding_ = on;
    }

private:
    struct Site {
        std::uint32_t key = 0;
        int second = -1, written = 0, dropped = 0;
    };
    bool OpenHandleLocked() {
        // FILE_APPEND_DATA without FILE_WRITE_DATA appends atomically; the shares let the log be read
        // while the game runs and renamed out from under the handle when it is rotated.
        handle_ = CreateFileA(path_.c_str(), FILE_APPEND_DATA,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (handle_ == INVALID_HANDLE_VALUE) return false;
        BY_HANDLE_FILE_INFORMATION info{};
        LARGE_INTEGER size{};
        if (!GetFileInformationByHandle(handle_, &info) ||
            (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) ||
            !GetFileSizeEx(handle_, &size) || size.QuadPart < 0) {
            CloseHandleLocked();
            return false;
        }
        bytes_ = static_cast<std::uint64_t>(size.QuadPart);
        return true;
    }
    void CloseHandleLocked() {
        if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_);
        handle_ = INVALID_HANDLE_VALUE;
        bytes_ = 0;
    }
    // Preserve both generations on failure; stop appending until a later retry succeeds.
    bool RotateLocked() {
        CloseHandleLocked();
        const std::string previous = path_ + ".1";
        const DWORD source = GetFileAttributesA(path_.c_str());
        const DWORD backup = GetFileAttributesA(previous.c_str());
        if (source == INVALID_FILE_ATTRIBUTES ||
            (source & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) ||
            (backup != INVALID_FILE_ATTRIBUTES &&
             (backup & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY))) ||
            (backup == INVALID_FILE_ATTRIBUTES && GetLastError() != ERROR_FILE_NOT_FOUND) ||
            !MoveFileExA(path_.c_str(), previous.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) ||
            !OpenHandleLocked()) return false;
        SYSTEMTIME now{};
        GetLocalTime(&now);
        PutLocked(now, 0, "log rotated: the previous log is now " + previous);
        return handle_ != INVALID_HANDLE_VALUE;
    }
    void PutLocked(const SYSTEMTIME& now, int dropped, std::string_view text) {
        char stamp[16];
        std::snprintf(stamp, sizeof stamp, "[%02d:%02d:%02d] ", now.wHour, now.wMinute, now.wSecond);
        line_.assign(stamp);
        if (dropped) {
            // Nothing is dropped silently: the next line from that place says how many went before it.
            line_ += '(';
            line_ += std::to_string(dropped);
            line_ += " more like this) ";
        }
        line_.append(text);
        line_ += '\n';
        DWORD written = 0;
        if (!WriteFile(handle_, line_.data(), static_cast<DWORD>(line_.size()), &written, nullptr)) {
            CloseHandleLocked();   // A lost handle (the file deleted under us) is picked up next line.
            return;
        }
        bytes_ += written;
    }
    // Where a line came from, near enough: the words it starts with, without the numbers that change
    // from one line to the next. Two places that start their lines the same way share one budget,
    // which is the point - it is repeated text that floods the log.
    //
    // The module's tag is skipped first. Every race line begins "[Race] ", which is seven of the
    // characters the key is made of, and at twenty-four the whole clearance module came out as ONE
    // key - "[Race] circuit exclusion" exactly - so the two lines a second were shared between the
    // routine audit line and the rare ones that say the guard did not do its job. The rare ones lost.
    static std::uint32_t KeyOf(std::string_view text) {
        if (!text.empty() && text.front() == '[') {
            const auto close = text.find("] ");
            if (close != std::string_view::npos) text.remove_prefix(close + 2);
        }
        std::uint32_t hash = 2166136261u;
        std::size_t taken = 0;
        for (char c : text) {
            if (taken >= kKeyChars || c == '\n') break;
            if (c >= '0' && c <= '9') continue;
            hash = (hash ^ static_cast<unsigned char>(c)) * 16777619u;
            ++taken;
        }
        return hash | 1u;   // 0 means "no site here".
    }
    Site& SiteFor(std::string_view text, int second) {
        const std::uint32_t key = KeyOf(text);
        Site& site = sites_[key % kSites];
        if (site.key != key || site.second != second) {
            // A new second, or a different place taking over this slot. Either way what has been
            // dropped so far is carried: the promise is that nothing goes missing in silence, and a
            // burst of failures - when the sink matters most - is exactly when several places write
            // at once and evict each other.
            site = {key, second, 0, site.dropped};
        }
        return site;
    }

    std::mutex lock_;
    std::string path_, line_;
    HANDLE handle_ = INVALID_HANDLE_VALUE;
    std::uint64_t bytes_ = 0;
    std::uint64_t retry_rotation_at_ = 0;
    bool folding_ = true;
    std::array<Site, kSites> sites_{};
};

inline Sink& Shared() {
    static Sink sink;
    return sink;
}
}  // namespace horse_races::log_sink
