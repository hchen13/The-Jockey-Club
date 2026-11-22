// Visible installed entry. The existing tray host owns all locks, state and
// backend lifetime; this process only forwards the invocation, then exits.
#include <windows.h>
#include <shellapi.h>
#include <string>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR arguments, int show) {
    bool quiet = false;
    int count = 0;
    if (auto values = CommandLineToArgvW(GetCommandLineW(), &count)) {
        for (int i = 1; i < count; ++i)
            if (std::wstring(values[i]) == L"--quiet") quiet = true;
        LocalFree(values);
    }
    const auto fail = [quiet](const std::wstring& message) {
        if (!quiet) MessageBoxW(nullptr, message.c_str(), L"The Jockey Club - Clubhouse", MB_OK | MB_ICONERROR);
        return 1;
    };
    std::wstring executable(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    if (!length || length >= executable.size()) return fail(L"Cannot locate the Clubhouse entry.");
    executable.resize(length);
    const auto separator = executable.find_last_of(L"\\/");
    if (separator == std::wstring::npos) return fail(L"Cannot locate the Clubhouse folder.");
    const std::wstring target = executable.substr(0, separator + 1) + L"tools\\Clubhouse\\JockeyClub.exe";
    const DWORD attributes = GetFileAttributesW(target.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY))
        return fail(L"Clubhouse files are missing. Keep Clubhouse.exe inside TheJockeyClub and retain the complete tools\\Clubhouse folder.\n\n" + target);

    // Preserve the original argument tail verbatim, including quotes and empty
    // arguments. Inherit the caller's working directory so relative --report
    // and --game-root arguments retain the existing launcher's meaning.
    std::wstring command = L"\"" + target + L"\"";
    if (arguments && *arguments) { command += L' '; command += arguments; }
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = static_cast<WORD>(show);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(target.c_str(), command.data(), nullptr, nullptr, FALSE,
                        BELOW_NORMAL_PRIORITY_CLASS, nullptr, nullptr, &startup, &process))
        return fail(L"Cannot start Clubhouse (Windows error " + std::to_wstring(GetLastError()) + L").\n\n" + target);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return 0; // The child remains JockeyClub.exe; no second resident host.
}
