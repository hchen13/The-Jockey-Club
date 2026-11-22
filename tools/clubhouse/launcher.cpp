// Native, application-local Clubhouse host. No console, service, registry writes,
// elevation, autostart or game-process handle. Children survive a launcher crash;
// a later launcher reattaches only after identity + control-token verification.
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>
#include <stdexcept>
#include <sstream>
#include "resource.h"
#include "bounded_log.h"

namespace fs = std::filesystem;
namespace {
constexpr wchar_t kClass[] = L"TheJockeyClub.Clubhouse.Launcher.v1";
constexpr UINT kTray = WM_APP + 1, kOpen = WM_APP + 2, kExit = WM_APP + 3;
constexpr UINT kOpenMenu = 100, kExitMenu = 101;
constexpr char kApplication[] = "the-jockey-club-race-dashboard";

std::string Utf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (!n) throw std::runtime_error("Invalid Unicode path");
    std::string result(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), n, nullptr, nullptr);
    return result;
}
std::wstring Wide(const std::string& value) {
    if (value.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (!n) throw std::runtime_error("Invalid UTF-8 metadata");
    std::wstring result(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), n);
    return result;
}
std::wstring Canonical(const fs::path& path) {
    HANDLE handle = CreateFileW(fs::absolute(path).c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) throw std::runtime_error("Game directory does not exist");
    std::wstring result(32768, L'\0');
    const DWORD n = GetFinalPathNameByHandleW(handle, result.data(), static_cast<DWORD>(result.size()), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    CloseHandle(handle);
    if (!n || n >= result.size()) throw std::runtime_error("Cannot resolve game directory");
    result.resize(n);
    if (result.rfind(L"\\\\?\\UNC\\", 0) == 0) result = L"\\\\" + result.substr(8);
    else if (result.rfind(L"\\\\?\\", 0) == 0) result.erase(0, 4);
    while (result.size() > 3 && result.back() == L'\\') result.pop_back();
    return result;
}
std::string Hex(const unsigned char* bytes, size_t size) {
    constexpr char chars[] = "0123456789abcdef";
    std::string result; result.reserve(size * 2);
    for (size_t i = 0; i < size; ++i) { result += chars[bytes[i] >> 4]; result += chars[bytes[i] & 15]; }
    return result;
}
std::string Hash(const std::wstring& path) {
    std::wstring lower = path;
    CharLowerBuffW(lower.data(), static_cast<DWORD>(lower.size()));
    auto bytes = Utf8(lower);
    unsigned char digest[32];
    if (BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, reinterpret_cast<PUCHAR>(bytes.data()),
                   static_cast<ULONG>(bytes.size()), digest, sizeof(digest)) < 0) throw std::runtime_error("Cannot hash installation path");
    return Hex(digest, sizeof(digest));
}
std::string RandomToken() {
    unsigned char bytes[32];
    if (BCryptGenRandom(nullptr, bytes, sizeof(bytes), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
        throw std::runtime_error("Cannot generate launcher session");
    return Hex(bytes, sizeof(bytes));
}
std::string Read(const fs::path& path) {
    HANDLE input = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (input == INVALID_HANDLE_VALUE) return {};
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(input, &size) || size.QuadPart > 1024 * 1024) { CloseHandle(input); throw std::runtime_error("Clubhouse metadata is unexpectedly large"); }
    std::string result(static_cast<size_t>(size.QuadPart), '\0'); DWORD got = 0;
    const BOOL read = ReadFile(input, result.data(), static_cast<DWORD>(result.size()), &got, nullptr);
    CloseHandle(input); if (!read) return {}; result.resize(got); return result;
}
void Write(const fs::path& path, const std::string& text) {
    if (!path.parent_path().empty()) fs::create_directories(path.parent_path());
    if(text.size()>1024*1024)throw std::runtime_error("Clubhouse metadata exceeds its capacity");
    fs::path temporary = path; temporary += L".launcher-tmp";
    // The installation lease serializes normal writers. A prior interrupted
    // output is preserved rather than generating another PID-specific file.
    const HANDLE output=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(output==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot create Clubhouse metadata staging file; interrupted data preserved");
    DWORD written=0;const bool complete=WriteFile(output,text.data(),static_cast<DWORD>(text.size()),&written,nullptr)&&
        written==text.size()&&FlushFileBuffers(output);
    const bool closed=CloseHandle(output)!=0;
    if(!complete||!closed)throw std::runtime_error("Cannot write Clubhouse data directory");
    for (int attempt = 0; ; ++attempt) {
        if (MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) break;
        const DWORD error = GetLastError();
        if (attempt >= 20 || (error != ERROR_SHARING_VIOLATION && error != ERROR_ACCESS_DENIED))
            throw std::runtime_error("Cannot publish Clubhouse metadata (Windows error " + std::to_string(error) + ")");
        Sleep(10);
    }
}
std::string QuoteJson(const std::string& value) {
    std::string result = "\"";
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') result += '\\';
        if (c == '\n') result += "\\n";
        else if (c == '\r') result += "\\r";
        else if (c == '\t') result += "\\t";
        else if (c < 32) result += ' ';
        else result += static_cast<char>(c);
    }
    return result + '"';
}
size_t ValueStart(const std::string& json, const std::string& key) {
    auto p = json.find('"' + key + '"'); if (p == std::string::npos) return p;
    p = json.find(':', p + key.size() + 2); if (p == std::string::npos) return p;
    return json.find_first_not_of(" \r\n\t", p + 1);
}
// Reads only protocol scalar properties; JSON is emitted by the bundled server.
std::string Field(const std::string& json, const std::string& key) {
    auto p = ValueStart(json, key); if (p == std::string::npos || json[p] != '"') return {};
    std::string result;
    for (++p; p < json.size(); ++p) {
        const char c = json[p]; if (c == '"') return result;
        if (c != '\\') { result += c; continue; }
        if (++p == json.size()) break;
        if (json[p] == '"' || json[p] == '\\' || json[p] == '/') result += json[p];
        else if (json[p] == 'n') result += '\n'; else if (json[p] == 'r') result += '\r'; else if (json[p] == 't') result += '\t';
        else return {}; // all metadata uses ensure_ascii=False, never \u escapes
    }
    return {};
}
DWORD Number(const std::string& json, const std::string& key) {
    const auto p = ValueStart(json, key); if (p == std::string::npos) return 0;
    try { return std::stoul(json.substr(p)); } catch (...) { return 0; }
}
std::wstring Arg(const std::wstring& value) {
    // CommandLineToArgvW/CRT quoting, including a trailing backslash.
    std::wstring result = L"\""; size_t slashes = 0;
    for (wchar_t c : value) {
        if (c == L'\\') { ++slashes; continue; }
        result.append(slashes * (c == L'"' ? 2 : 1), L'\\'); slashes = 0;
        if (c == L'"') result += L'\\'; result += c;
    }
    result.append(slashes * 2, L'\\'); return result + L'"';
}
fs::path LocalData() {
    std::wstring environment(32768, L'\0');
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", environment.data(), static_cast<DWORD>(environment.size()));
    if (length && length < environment.size()) { environment.resize(length); return fs::path(environment) / L"TheJockeyClub"; }
    PWSTR path = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &path)))
        throw std::runtime_error("Cannot find local application data");
    fs::path result(path); CoTaskMemFree(path); return result / L"TheJockeyClub";
}
bool GameDirectory(const fs::path& path) {
    return fs::is_regular_file(path / L"RDR2.exe") &&
           (fs::is_regular_file(path / L"TheJockeyClub.asi") ||
            fs::is_regular_file(path / L"TheJockeyClub/config/TheJockeyClub.ini") ||
            fs::is_regular_file(path / L"TheJockeyClub.ini") || fs::is_regular_file(path / L"HorseSpeedLab.ini"));
}

fs::path AdjacentGame(fs::path directory) {
    // Installed in <game>/TheJockeyClub/tools/Clubhouse, or a standalone
    // portable bundle beside RDR2.exe. The game-root identity never changes.
    while (!directory.empty()) {
        if (GameDirectory(directory)) return directory;
        const auto parent = directory.parent_path();
        if (parent == directory) break;
        directory = parent;
    }
    return {};
}

bool LegacyLayout(const fs::path& game) {
    std::error_code error;
    if (fs::exists(game / L"TheJockeyClub/config/TheJockeyClub.ini", error) || error) return false;
    const auto previous = game / L"TheJockeyClub.ini";
    if (fs::exists(previous, error)) return fs::is_regular_file(previous, error) && !error;
    if (error) return false;
    const auto ancient = game / L"HorseSpeedLab.ini";
    return fs::exists(ancient, error) && !error && fs::is_regular_file(ancient, error) && !error;
}

fs::path StateDirectory(const fs::path& game) {
    const auto legacy = game / L"TheJockeyClub-data/clubhouse";
    const auto consolidated = game / L"TheJockeyClub/data/clubhouse";
    if (LegacyLayout(game)) return legacy;
    if (!fs::exists(consolidated) && fs::exists(legacy))
        throw std::runtime_error("Legacy Clubhouse history has not been migrated; run the 0.32 installer before opening Clubhouse");
    return consolidated;
}
fs::path ChooseGame() {
    IFileOpenDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
        throw std::runtime_error("Cannot open game-directory picker");
    DWORD options = 0; dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    dialog->SetTitle(L"Choose Red Dead Redemption 2 (RDR2.exe and The Jockey Club)");
    fs::path result;
    if (SUCCEEDED(dialog->Show(nullptr))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) { result = path; CoTaskMemFree(path); }
            item->Release();
        }
    }
    dialog->Release(); return result;
}
struct HttpReply { DWORD status = 0; std::string body; };
HttpReply Request(int port, const wchar_t* path = L"/api/health", const std::string& token = {}) {
    HttpReply result;
    HINTERNET session = WinHttpOpen(L"JockeyClub/1", WINHTTP_ACCESS_TYPE_NO_PROXY, nullptr, nullptr, 0);
    if (!session) return result;
    WinHttpSetTimeouts(session, 400, 400, 600, 600);
    HINTERNET connection = WinHttpConnect(session, L"127.0.0.1", static_cast<INTERNET_PORT>(port), 0);
    HINTERNET request = connection ? WinHttpOpenRequest(connection, token.empty() ? L"GET" : L"POST", path,
                                                       nullptr, nullptr, nullptr, 0) : nullptr;
    if (request) {
        const auto headers = token.empty() ? std::wstring() : L"X-Jockey-Club-Control: " + Wide(token) + L"\r\nContent-Type: application/json\r\n";
        if (WinHttpSendRequest(request, headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
                               static_cast<DWORD>(headers.size()), token.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>("{}"),
                               token.empty() ? 0 : 2, token.empty() ? 0 : 2, 0) && WinHttpReceiveResponse(request, nullptr)) {
            DWORD size = sizeof(result.status);
            WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &result.status, &size, nullptr);
            char buffer[4096]; DWORD got = 0;
            while (result.body.size() < 1024 * 1024 && WinHttpReadData(request, buffer, sizeof(buffer), &got) && got)
                result.body.append(buffer, got);
        }
        WinHttpCloseHandle(request);
    }
    if (connection) WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session); return result;
}

struct Launcher {
    HWND window = nullptr;
    HANDLE mutex = nullptr, backend = nullptr;
    fs::path directory, game, payload, state, discovery, report;
    std::string identity, session, token;
    DWORD backendPid = 0;
    int port = 0, preferredPort = 8766;
    bool owned = false, ready = false, stopping = false, openWanted = true, quiet = false, trayAdded = false;
    ULONGLONG deadline = 0;
    NOTIFYICONDATAW tray{};

    void Trace(const std::string& message) noexcept {
        try {
            if (state.empty()) return;
            fs::create_directories(state);
            SYSTEMTIME time{}; GetLocalTime(&time);
            std::ostringstream line;
            line << time.wHour << ':' << time.wMinute << ':' << time.wSecond << " pid=" << GetCurrentProcessId() << ' ' << message << '\n';
            clubhouse_log::Append(state / L"launcher.log", line.str());
        } catch (...) {}
    }

    ~Launcher() {
        if (trayAdded) Shell_NotifyIconW(NIM_DELETE, &tray);
        if (backend) CloseHandle(backend);
        if (mutex) { ReleaseMutex(mutex); CloseHandle(mutex); }
    }
    void Tooltip(const wchar_t* text) {
        tray.uFlags = NIF_TIP; wcsncpy_s(tray.szTip, text, _TRUNCATE);
        if (trayAdded) Shell_NotifyIconW(NIM_MODIFY, &tray);
    }
    void Balloon(const wchar_t* text) {
        tray.uFlags = NIF_INFO;
        wcscpy_s(tray.szInfoTitle, L"The Jockey Club"); wcsncpy_s(tray.szInfo, text, _TRUNCATE); tray.dwInfoFlags = NIIF_INFO;
        Shell_NotifyIconW(NIM_MODIFY, &tray);
    }
    void AddTray() {
        tray.cbSize = sizeof(tray); tray.hWnd = window; tray.uID = 1;
        tray.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP; tray.uCallbackMessage = kTray;
        tray.hIcon = reinterpret_cast<HICON>(GetClassLongPtrW(window, GCLP_HICONSM));
        wcscpy_s(tray.szTip, L"The Jockey Club - Starting Clubhouse");
        trayAdded = Shell_NotifyIconW(NIM_ADD, &tray) != FALSE;
        if (!trayAdded) throw std::runtime_error("Cannot create Clubhouse tray icon");
        tray.uVersion = NOTIFYICON_VERSION_4; Shell_NotifyIconW(NIM_SETVERSION, &tray);
    }
    void Status(const char* status) {
        std::ostringstream json;
        json << "{\"application\":\"the-jockey-club-launcher\",\"pid\":" << GetCurrentProcessId()
             << ",\"backend_pid\":" << backendPid << ",\"port\":" << port << ",\"owned\":" << (owned ? "true" : "false")
             << ",\"console_window\":" << reinterpret_cast<uintptr_t>(GetConsoleWindow())
             << ",\"installation_id\":" << QuoteJson(identity) << ",\"session_id\":" << QuoteJson(session)
             << ",\"game_root\":" << QuoteJson(Utf8(game.wstring())) << ",\"payload\":" << QuoteJson(Utf8(payload.wstring()))
             << ",\"status\":" << QuoteJson(status) << "}";
        Write(state / L"launcher.json", json.str()); if (!report.empty()) Write(report, json.str());
    }
    bool Matches(const HttpReply& reply, const std::string& expectedSession = {}) const {
        return reply.status == 200 && Field(reply.body, "application") == kApplication &&
               Field(reply.body, "installation_id") == identity && Field(reply.body, "status") == "ready" &&
               (expectedSession.empty() || Field(reply.body, "session_id") == expectedSession);
    }
    void Attach(int candidatePort, const HttpReply& health) {
        port = candidatePort; backendPid = Number(health.body, "pid"); session = Field(health.body, "session_id");
        if (backend) CloseHandle(backend);
        backend = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, backendPid);
        token = Read(state / L"control.token");
        if (token.size() == 64) {
            const auto claim = Request(port, L"/api/launcher/claim", token);
            owned = claim.status == 200 && Field(claim.body, "session_id") == session;
        } else owned = false;
        Ready();
    }
    bool Reuse() {
        for (const auto& path : {discovery, state / L"server.json"}) {
            const auto data = Read(path);
            const int candidate = static_cast<int>(Number(data, "port"));
            if (!candidate || Field(data, "installation_id") != identity) continue;
            const auto health = Request(candidate);
            if (Matches(health)) { Attach(candidate, health); return true; }
        }
        const auto health = Request(preferredPort);
        if (Matches(health)) { Attach(preferredPort, health); return true; }
        return false;
    }
    void Start() {
        ready = false; stopping = false;
        if (Reuse()) return;
        const fs::path python = payload / L"python/pythonw.exe", script = payload / L"tools/race_analysis/dashboard_server.py";
        for (const auto& path : {python, script, payload / L"bin/race_analysis_export.exe",
                                 payload / L"bin/race_analysis_candidates.exe", payload / L"bin/race_settings_catalogue.exe"})
            if (!fs::is_regular_file(path)) throw std::runtime_error("Portable Clubhouse files are missing; restore the complete JockeyClub folder beside JockeyClub.exe");
        fs::create_directories(state);
        token = RandomToken(); session = RandomToken(); Write(state / L"control.token", token);
        const std::vector<std::wstring> arguments = {python.wstring(), L"-I", L"-B", script.wstring(), L"--game-root", game.wstring(),
            L"--state", state.wstring(), L"--port", std::to_wstring(preferredPort), L"--port-fallback", L"--control-token-file",
            (state / L"control.token").wstring(), L"--session-id", Wide(session)};
        std::wstring command;
        for (const auto& argument : arguments) { if (!command.empty()) command += L' '; command += Arg(argument); }
        SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
        const auto logPath = state / L"backend-startup.log";
        HANDLE log = INVALID_HANDLE_VALUE;
        try {
            clubhouse_log::TrimBeforeStart(logPath);
            log = clubhouse_log::Open(logPath, FILE_APPEND_DATA | FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, true);
        } catch (...) {}
        // Logging is optional: a locked or unsafe file must not prevent the
        // backend from starting, and a pipe must not tie it to this process.
        if (log == INVALID_HANDLE_VALUE)
            log = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING, 0, nullptr);
        HANDLE input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING, 0, nullptr);
        if (log == INVALID_HANDLE_VALUE || input == INVALID_HANDLE_VALUE) {
            if (log != INVALID_HANDLE_VALUE) CloseHandle(log); if (input != INVALID_HANDLE_VALUE) CloseHandle(input);
            throw std::runtime_error("Cannot open Clubhouse startup log");
        }
        STARTUPINFOW startup{}; startup.cb = sizeof(startup); startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
        startup.wShowWindow = SW_HIDE; startup.hStdOutput = log; startup.hStdError = log; startup.hStdInput = input;
        PROCESS_INFORMATION process{};
        const BOOL created = CreateProcessW(python.c_str(), command.data(), nullptr, nullptr, TRUE,
                                             CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS, nullptr, payload.c_str(), &startup, &process);
        const DWORD error = GetLastError(); CloseHandle(log); CloseHandle(input);
        if (!created) throw std::runtime_error("Cannot start bundled Python (Windows error " + std::to_string(error) + ")");
        if (backend) CloseHandle(backend);
        backend = process.hProcess; backendPid = process.dwProcessId; CloseHandle(process.hThread);
        owned = true; deadline = GetTickCount64() + 45000;
        Tooltip(L"The Jockey Club - Starting Clubhouse"); Status("starting"); SetTimer(window, 1, 250, nullptr);
    }
    void OpenBrowser() {
        const std::wstring url = L"http://127.0.0.1:" + std::to_wstring(port) + L"/";
        const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
        if (result <= 32) throw std::runtime_error("Windows could not open the default browser");
    }
    void Ready() {
        ready = true; Tooltip(L"The Jockey Club - Clubhouse (collecting race records)");
        SetTimer(window, 1, 5000, nullptr); Status("ready");
        Trace("ready backend=" + std::to_string(backendPid) + " port=" + std::to_string(port) + " owned=" + std::to_string(owned));
        if (openWanted) { openWanted = false; OpenBrowser(); }
    }
    void Open(bool browser) {
        if (stopping) return;
        openWanted = browser;
        if (ready && Matches(Request(port), session)) { if (browser) OpenBrowser(); openWanted = false; return; }
        if (backend && WaitForSingleObject(backend, 0) == WAIT_TIMEOUT) {
            if (ready) throw std::runtime_error("Clubhouse is still running but not responding; check dashboard.log before restarting");
            return;
        }
        Start();
    }
    void Exit() {
        Trace("exit requested owned=" + std::to_string(owned) + " stopping=" + std::to_string(stopping));
        if (stopping) return;
        if (!owned || !backend || WaitForSingleObject(backend, 0) == WAIT_OBJECT_0) { DestroyWindow(window); return; }
        const auto health = Request(port);
        if (!Matches(health, session) || Number(health.body, "pid") != backendPid)
            throw std::runtime_error("Backend identity changed or is not responding. Clubhouse has not stopped another process.");
        const auto stopped = Request(port, L"/api/launcher/shutdown", token);
        Trace("shutdown response=" + std::to_string(stopped.status));
        if (stopped.status != 200) throw std::runtime_error("Backend did not accept this launcher's shutdown token");
        stopping = true; ready = false; Status("stopping");
        Tooltip(L"The Jockey Club - Finishing analysis and saving records"); SetTimer(window, 1, 250, nullptr);
    }
    void Tick() {
        if (stopping) {
            if (!backend || WaitForSingleObject(backend, 0) == WAIT_OBJECT_0) { Status("stopped"); DestroyWindow(window); }
            return;
        }
        if (ready) {
            if (backend && WaitForSingleObject(backend, 0) == WAIT_OBJECT_0) {
                ready = false; owned = false; Status("backend-stopped"); KillTimer(window, 1);
                Tooltip(L"The Jockey Club - Backend stopped (Open Clubhouse to restart)");
                Balloon(L"Clubhouse stopped. Use Open Clubhouse to reopen it; saved race records remain in the data folder.");
            }
            return;
        }
        const auto data = Read(state / L"server.json");
        if (Field(data, "session_id") == session && Number(data, "pid") == backendPid) {
            const int candidate = static_cast<int>(Number(data, "port"));
            if (candidate && Matches(Request(candidate), session)) { port = candidate; Ready(); return; }
        }
        if (backend && WaitForSingleObject(backend, 0) == WAIT_OBJECT_0) {
            if (Reuse()) return; // a concurrently started developer backend won the installation lease
            throw std::runtime_error("Clubhouse could not start. See " + Utf8((state / L"backend-startup.log").wstring()));
        }
        if (GetTickCount64() > deadline)
            throw std::runtime_error("Clubhouse is still starting. See " + Utf8((state / L"backend-startup.log").wstring()) + "; reopen to attach when ready");
    }
    void Error(const std::exception& error) {
        Trace(std::string("error: ") + error.what());
        const auto message = Wide(error.what()) + L"\n\nGame: " + game.wstring();
        if (!quiet) MessageBoxW(window, message.c_str(), L"The Jockey Club - Clubhouse", MB_OK | MB_ICONERROR);
        if (!ready && !stopping) { Status("error"); DestroyWindow(window); }
    }
};
LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto app = reinterpret_cast<Launcher*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        app = static_cast<Launcher*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }
    if (!app) return DefWindowProcW(window, message, wparam, lparam);
    try {
        if (message == kOpen) { app->Open(wparam != 0); return 1; }
        if (message == kExit || message == WM_CLOSE) { app->Exit(); return 1; }
        if (message == WM_TIMER) { app->Tick(); return 0; }
        if (message == WM_COMMAND) {
            if (LOWORD(wparam) == kOpenMenu) app->Open(true);
            else if (LOWORD(wparam) == kExitMenu) app->Exit();
            return 0;
        }
        if (message == kTray) {
            const auto event = LOWORD(lparam);
            if (event == NIN_SELECT || event == NIN_KEYSELECT || event == WM_LBUTTONDBLCLK) app->Open(true);
            if (event == WM_CONTEXTMENU || event == WM_RBUTTONUP) {
                POINT point{}; GetCursorPos(&point); HMENU menu = CreatePopupMenu();
                AppendMenuW(menu, MF_STRING | (app->stopping ? MF_GRAYED : 0), kOpenMenu, L"Open Clubhouse");
                AppendMenuW(menu, MF_STRING | (app->stopping ? MF_GRAYED : 0), kExitMenu, L"Exit Clubhouse");
                SetForegroundWindow(window); TrackPopupMenu(menu, TPM_RIGHTBUTTON, point.x, point.y, 0, window, nullptr);
                DestroyMenu(menu); PostMessageW(window, WM_NULL, 0, 0);
            }
            return 0;
        }
        if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
        static const UINT taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
        if (message == taskbarCreated) { app->trayAdded = false; app->AddTray(); return 0; }
    } catch (const std::exception& error) { app->Error(error); return 0; }
    return DefWindowProcW(window, message, wparam, lparam);
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    Launcher app;
    try {
        std::wstring executable(32768, L'\0');
        executable.resize(GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size())));
        app.directory = fs::path(executable).parent_path(); app.payload = app.directory / L"JockeyClub";
        bool exitOnly = false;
        int count = 0; LPWSTR* values = CommandLineToArgvW(GetCommandLineW(), &count);
        for (int i = 1; i < count; ++i) {
            const std::wstring value(values[i]);
            if (value == L"--no-open") app.openWanted = false;
            else if (value == L"--quiet") app.quiet = true;
            else if (value == L"--exit") exitOnly = true;
            else if ((value == L"--game-root" || value == L"--port" || value == L"--report") && i + 1 < count) {
                const std::wstring argument(values[++i]);
                if (value == L"--game-root") app.game = argument;
                else if (value == L"--report") app.report = fs::absolute(argument);
                else { app.preferredPort = std::stoi(argument); if (app.preferredPort < 1 || app.preferredPort > 65535) throw std::runtime_error("Invalid port"); }
            } else throw std::runtime_error("Unknown or incomplete Clubhouse launcher argument");
        }
        LocalFree(values);
        const auto remembered = LocalData() / (L"launcher-" + Wide(Hash(Canonical(app.directory))) + L".txt");
        if (app.game.empty()) app.game = AdjacentGame(app.directory);
        if (app.game.empty()) { const auto path = Read(remembered); if (!path.empty() && GameDirectory(Wide(path))) app.game = Wide(path); }
        if (app.game.empty()) { if (exitOnly || app.quiet) return 2; app.game = ChooseGame(); if (app.game.empty()) return 0; }
        if (!GameDirectory(app.game)) throw std::runtime_error("Choose the game folder containing RDR2.exe and The Jockey Club");
        app.game = Canonical(app.game); app.identity = Hash(app.game.wstring());
        app.state = StateDirectory(app.game);
        app.discovery = LocalData() / L"instances" / (Wide(app.identity) + L".json");
        const std::wstring mutexName = L"Local\\TheJockeyClub-Launcher-" + Wide(app.identity);
        app.mutex = CreateMutexW(nullptr, FALSE, mutexName.c_str());
        if (!app.mutex) throw std::runtime_error("Cannot create Clubhouse launcher mutex");
        if (WaitForSingleObject(app.mutex, 0) == WAIT_TIMEOUT) {
            HWND owner = nullptr;
            for (int attempt = 0; attempt < 200 && !owner; ++attempt) {
                owner = FindWindowW(kClass, Wide(app.identity).c_str()); if (!owner) Sleep(100);
            }
            if (!owner) throw std::runtime_error("Clubhouse launcher is busy starting; try opening it again");
            DWORD_PTR ignored = 0;
            if (!SendMessageTimeoutW(owner, exitOnly ? kExit : kOpen, app.openWanted ? 1 : 0, 0,
                                      SMTO_ABORTIFHUNG, 10000, &ignored) || ignored != 1)
                throw std::runtime_error("Existing Clubhouse launcher could not complete the command; see " + Utf8((app.state / L"launcher.log").wstring()));
            if (!app.report.empty()) { const auto status = Read(app.state / L"launcher.json"); if (!status.empty()) Write(app.report, status); }
            return 0;
        }
        if (exitOnly) return 2;
        Write(remembered, Utf8(app.game.wstring()));
        WNDCLASSEXW type{}; type.cbSize = sizeof(type); type.hInstance = instance; type.lpfnWndProc = WindowProc; type.lpszClassName = kClass;
        type.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_JOCKEY_CLUB));
        type.hIconSm = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_JOCKEY_CLUB), IMAGE_ICON,
            GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED));
        if (!RegisterClassExW(&type) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) throw std::runtime_error("Cannot register Clubhouse tray window");
        app.window = CreateWindowExW(WS_EX_TOOLWINDOW, kClass, Wide(app.identity).c_str(), WS_OVERLAPPED,
                                      0, 0, 0, 0, nullptr, nullptr, instance, &app);
        if (!app.window) throw std::runtime_error("Cannot create Clubhouse tray window");
        app.AddTray(); app.Start();
        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
    } catch (const std::exception& error) {
        app.Trace(std::string("error: ") + error.what());
        if (!app.quiet) MessageBoxW(nullptr, Wide(error.what()).c_str(), L"The Jockey Club - Clubhouse", MB_OK | MB_ICONERROR);
        return 1;
    }
    CoUninitialize(); return 0;
}
