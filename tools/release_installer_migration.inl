// Included inside release_installer.cpp's anonymous namespace. All persisted
// paths are game-relative. The append-only journal is flushed before each
// mutation; rollback verifies bytes and never writes through an existing file.
constexpr wchar_t kBaseline[] = L"TheJockeyClub\\install-state\\legacy-0.32.0";
constexpr wchar_t kUpdate[] = L"TheJockeyClub\\install-state\\update";
constexpr std::size_t kMaxMigrationFiles = 200000;

const std::map<std::wstring, std::wstring, CaseLess>& LegacyFiles() {
    static const auto result = [] {
        std::map<std::wstring, std::wstring, CaseLess> files;
        for (const auto* name : {L"TheJockeyClub.ini", L"ArthurCamp.ini", L"ArthurCamp.sweep", L"HorseAttributeBackup.ini",
                L"AppaloosaRaceRoutes.ini", L"ArabianRaceRoutes.ini", L"ArthurCampDuelRoutes.ini", L"ArthurCampRaceRoutes.ini",
                L"BillDuelRaceRoutes.ini", L"BlackwaterRaceRoutes.ini", L"CharlesRaceRoutes.ini", L"HorseRaceRoutes.ini",
                L"JavierDuelRaceRoutes.ini", L"JohnStakesRaceRoutes.ini", L"MacfarlaneWfaRoutes.ini", L"NewAustinRaceRoutes.ini",
                L"PaintRaceRoutes.ini", L"PearsonRaceRoutes.ini", L"RhodesHandicapRoutes.ini", L"SadieRaceRoutes.ini",
                L"StandardbredRaceRoutes.ini", L"StrawberryWfaRoutes.ini", L"TennesseeRaceRoutes.ini", L"VeteranCabinRaceRoutes.ini"})
            files.emplace(name, std::wstring(L"TheJockeyClub\\config\\") + name);
        for (const auto* name : {L"HorseSpeedLab.ini", L"HorseValidation.ini"})
            files.emplace(name, std::wstring(L"TheJockeyClub\\config\\legacy\\") + name);
        for (const auto* name : {L"TheJockeyClub-speed-learning-v4.dat", L"TheJockeyClub-speed-learning-v3.dat", L"TheJockeyClub-speed-learning-v2.dat", L"TheJockeyClub-odds.txt"})
            files.emplace(name, std::wstring(L"TheJockeyClub\\learning\\") + name);
        files.emplace(L"TheJockeyClub-speed-learning.dat", L"TheJockeyClub\\learning\\legacy\\TheJockeyClub-speed-learning.dat");
        files.emplace(L"TheJockeyClub-progress.ini", L"TheJockeyClub\\data\\TheJockeyClub-progress.ini");
        for (const auto* name : {L"JockeyClub.exe", L"Clubhouse-README.txt"})
            files.emplace(name, std::wstring(L"TheJockeyClub\\tools\\Clubhouse\\") + name);
        for (const auto* name : {L"TheJockeyClub-survey.txt", L"TheJockeyClub-aim.txt", L"TheJockeyClub-positions.txt"})
            files.emplace(name, std::wstring(L"TheJockeyClub\\diagnostics\\") + name);
        for (int version = 1; version <= 5; ++version) {
            const auto name = L"TheJockeyClub-snow-history-v" + std::to_wstring(version) + L".dat";
            for (const auto* suffix : {L"", L".tmp", L".migrating", L".migrated"})
                files.emplace(name + suffix, L"TheJockeyClub\\cache\\legacy\\" + name + suffix);
        }
        return files;
    }();
    return result;
}

const std::map<std::wstring, std::wstring, CaseLess>& LegacyDirectories() {
    static const std::map<std::wstring, std::wstring, CaseLess> result{
        {L"TheJockeyClub-courses", L"TheJockeyClub\\courses"},
        {L"TheJockeyClubData", L"TheJockeyClub\\data\\archives"},
        {L"TheJockeyClub-data", L"TheJockeyClub\\data"},
        {L"TheJockeyClub-motion", L"TheJockeyClub\\recordings"},
        {L"JockeyClub", L"TheJockeyClub\\tools\\Clubhouse\\JockeyClub"},
    };
    return result;
}

bool Digits(const std::wstring& text) {
    return !text.empty() && text.find_first_not_of(L"0123456789") == std::wstring::npos;
}

bool StampedDiagnostic(const std::wstring& name, const std::wstring& prefix) {
    // scene_survey::Stamp is exactly YYYY-MM-DD HH:MM:SS, with spaces and
    // colons replaced by hyphens. A broad TheJockeyClub* root glob is forbidden.
    if (name.size() != prefix.size() + 19 + 4 || !Same(name.substr(0, prefix.size()), prefix) ||
        !Same(name.substr(name.size() - 4), L".txt")) return false;
    const auto stamp = name.substr(prefix.size(), 19);
    for (std::size_t i = 0; i < stamp.size(); ++i) {
        const bool separator = i == 4 || i == 7 || i == 10 || i == 13 || i == 16;
        if (separator ? stamp[i] != L'-' : stamp[i] < L'0' || stamp[i] > L'9') return false;
    }
    return true;
}

std::wstring LegacyDestination(const std::wstring& relative) {
    const auto exact = LegacyFiles().find(relative);
    if (exact != LegacyFiles().end()) return exact->second;
    for (const auto& directory : LegacyDirectories())
        if (Same(relative, directory.first) || Under(relative, directory.first))
            return directory.second + relative.substr(directory.first.size());
    if (relative.find(L'\\') != std::wstring::npos) return {};
    for (const auto* name : {L"TheJockeyClub.log", L"TheJockeyClub-fault.log", L"TheJockeyClubHorseLedger.log"}) {
        const std::wstring base(name);
        if (Same(relative, base) || (relative.size() > base.size() + 1 && Same(relative.substr(0, base.size() + 1), base + L".") &&
                Digits(relative.substr(base.size() + 1))))
            return L"TheJockeyClub\\logs\\" + relative;
    }
    if (StampedDiagnostic(relative, L"TheJockeyClub-survey-") || StampedDiagnostic(relative, L"TheJockeyClub-record-"))
        return L"TheJockeyClub\\diagnostics\\" + relative;
    return {};
}

std::vector<std::wstring> Children(const std::wstring& directory) {
    std::vector<std::wstring> result;
    WIN32_FIND_DATAW found{};
    HANDLE handle = FindFirstFileW(Extended(Join(directory, L"*")).c_str(), &found);
    if (handle == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return result;
        WinFail(L"Enumerate migration directory " + directory, error);
    }
    DWORD error = ERROR_SUCCESS;
    do {
        const std::wstring name(found.cFileName);
        if (name == L"." || name == L"..") continue;
        CheckComponent(name);
        result.push_back(name);
    } while (FindNextFileW(handle, &found));
    error = GetLastError(); FindClose(handle);
    if (error != ERROR_NO_MORE_FILES) WinFail(L"Finish migration directory enumeration", error);
    std::sort(result.begin(), result.end(), CaseLess{});
    return result;
}

struct Fingerprint { std::uint64_t size = 0; std::string hash; };
bool Equal(const Fingerprint& a, const Fingerprint& b) { return a.size == b.size && a.hash == b.hash; }
Fingerprint FingerprintOf(HANDLE file, Sha256& hashes) { return {FileSize(file), hashes.file(file)}; }

struct MigrationFile {
    std::wstring old_path, new_path;
    Fingerprint bytes;
    Handle source;
    bool duplicate = false;
};
struct Migration {
    std::vector<MigrationFile> files;
    std::vector<std::pair<std::wstring, std::wstring>> directories;
    std::map<std::wstring, std::size_t, CaseLess> destinations;
};

Migration PlanMigration(const std::wstring& target, DirectoryGuards& guards, Sha256& hashes) {
    Migration plan;
    std::vector<std::pair<std::wstring, std::wstring>> pending;
    for (const auto& item : LegacyDirectories()) pending.push_back(item);
    auto add_file = [&](const std::wstring& old_path, const std::wstring& new_path) {
        if (plan.files.size() == kMaxMigrationFiles) Fail(L"Migration file limit exceeded; no files were changed.");
        MigrationFile file;
        file.old_path = old_path; file.new_path = new_path;
        file.source = ReadLocked(Join(target, old_path), guards);
        file.bytes = FingerprintOf(file.source.get(), hashes);
        const auto existing = plan.destinations.find(new_path);
        if (existing != plan.destinations.end()) {
            if (!Equal(plan.files[existing->second].bytes, file.bytes))
                Fail(L"Migration conflict: " + old_path + L" and " + plan.files[existing->second].old_path + L" map to " + new_path + L" with different bytes. Resolve these exact paths manually.");
            file.duplicate = true;
        }
        const auto destination = Join(target, new_path);
        if (guards.check(Parent(destination), false) && Attributes(destination) != INVALID_FILE_ATTRIBUTES) {
            auto original = ReadLocked(destination, guards);
            if (!Equal(file.bytes, FingerprintOf(original.get(), hashes)))
                Fail(L"Migration conflict: " + old_path + L" differs from " + new_path + L". Neither file was changed; resolve these exact paths manually.");
            file.duplicate = true;
        }
        plan.destinations.emplace(new_path, plan.files.size());
        plan.files.push_back(std::move(file));
    };
    for (const auto& item : LegacyFiles()) {
        if (Attributes(Join(target, item.first)) != INVALID_FILE_ATTRIBUTES) add_file(item.first, item.second);
    }
    // Enumerate root names once, accepting only exact named log rotations and
    // the native timestamp grammar. Other root files are never inferred.
    for (const auto& name : Children(target)) {
        if (LegacyFiles().count(name) || LegacyDirectories().count(name)) continue;
        const auto destination = LegacyDestination(name);
        if (!destination.empty()) add_file(name, destination);
    }
    while (!pending.empty()) {
        auto directory = std::move(pending.back()); pending.pop_back();
        const auto path = Join(target, directory.first);
        const DWORD attributes = Attributes(path);
        if (attributes == INVALID_FILE_ATTRIBUTES) continue;
        if (!(attributes & FILE_ATTRIBUTE_DIRECTORY)) Fail(L"Legacy directory path is occupied by a file: " + directory.first);
        guards.check(path, false);
        guards.check(Join(target, directory.second), false);
        plan.directories.push_back(directory);
        for (const auto& child : Children(path)) {
            const auto old_path = directory.first + L"\\" + child;
            const auto new_path = directory.second + L"\\" + child;
            const DWORD child_attributes = Attributes(Join(target, old_path));
            if (child_attributes & FILE_ATTRIBUTE_DIRECTORY) pending.emplace_back(old_path, new_path);
            else add_file(old_path, new_path);
        }
    }
    for (const auto& file : plan.files) {
        auto parent = file.new_path.find(L'\\');
        while (parent != std::wstring::npos) {
            if (plan.destinations.count(file.new_path.substr(0, parent))) Fail(L"Migration file/directory conflict: " + file.new_path);
            parent = file.new_path.find(L'\\', parent + 1);
        }
    }
    for (const auto& directory : plan.directories)
        if (plan.destinations.count(directory.second)) Fail(L"Migration file/directory conflict: " + directory.second);
    return plan;
}

std::string IniTrim(const std::string& value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    return value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
}

std::string OverlayBase(HANDLE source) {
    Rewind(source);
    const auto bytes = ReadSmall(source);
    std::string section;
    for (std::size_t begin = 0; begin < bytes.size();) {
        const auto end = bytes.find('\n', begin);
        const auto line = IniTrim(bytes.substr(begin, end == std::string::npos ? end : end - begin));
        begin = end == std::string::npos ? bytes.size() : end + 1;
        if (line.empty() || line.front() == ';' || line.front() == '#') continue;
        if (line.front() == '[') { section = line; continue; }
        if (section != "[Overlay]") continue;
        const auto equals = line.find('=');
        if (equals != std::string::npos && IniTrim(line.substr(0, equals)) == "Base") return IniTrim(line.substr(equals + 1));
    }
    return {};
}

void ValidateOverlayDependencies(const std::vector<Entry>& entries, const Migration& migration,
        const std::wstring& target, DirectoryGuards& guards) {
    std::map<std::wstring, const Entry*, CaseLess> payload;
    for (const auto& entry : entries) payload.emplace(entry.relative, &entry);
    std::vector<std::wstring> pending;
    for (const auto& item : LegacyFiles())
        if (item.first.size() >= 10 && Same(item.first.substr(item.first.size() - 10), L"Routes.ini")) pending.push_back(item.second);
    std::set<std::wstring, CaseLess> checked;
    while (!pending.empty()) {
        const auto relative = pending.back(); pending.pop_back();
        if (!checked.insert(relative).second) continue;
        if (checked.size() > 1024) Fail(L"Route overlay dependency limit exceeded.");
        Handle existing;
        HANDLE source = INVALID_HANDLE_VALUE;
        const auto destination = Join(target, relative);
        if (guards.check(Parent(destination), false) && Attributes(destination) != INVALID_FILE_ATTRIBUTES) {
            existing = ReadLocked(destination, guards); source = existing.get();
        } else {
            const auto old = migration.destinations.find(relative);
            const auto seed = payload.find(relative);
            if (old != migration.destinations.end()) source = migration.files[old->second].source.get();
            else if (seed != payload.end()) source = seed->second->source.get();
        }
        if (source == INVALID_HANDLE_VALUE) continue;
        const auto name = OverlayBase(source);
        if (name.empty()) continue;
        const auto base = Wide(name); CheckComponent(base);
        const auto required = L"TheJockeyClub\\config\\" + base;
        bool exists = migration.destinations.count(required) || payload.count(required);
        const auto path = Join(target, required);
        if (guards.check(Parent(path), false) && Attributes(path) != INVALID_FILE_ATTRIBUTES) {
            auto file = ReadLocked(path, guards); exists = true;
        }
        if (!exists) Fail(L"Route overlay dependency conflict: " + relative + L" requires Base=" + base +
            L". Required sibling " + required + L" is absent from the approved migration, existing config and payload. Move this exact custom dependency into config explicitly before retrying; no arbitrary root INI was moved.");
        pending.push_back(required);
    }
}

// The backend's named installation mutex also covers developer Python
// backends outside the portable tree, including a detached active backend.
// Holding both mutexes prevents a launcher/backend from starting mid-migration.
class ClubhouseLease {
public:
    void acquire(const std::wstring& target) {
        Handle directory(CreateFileW(Extended(target).c_str(), FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr));
        if (!directory.valid()) WinFail(L"Open installation identity directory");
        std::vector<wchar_t> canonical(32768);
        const DWORD count = GetFinalPathNameByHandleW(directory.get(), canonical.data(), static_cast<DWORD>(canonical.size()), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
        if (!count || count >= canonical.size()) WinFail(L"Resolve canonical installation identity");
        auto normalized = FullPath(std::wstring(canonical.data(), count));
        std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
        const auto bytes = Utf8(normalized);
        BCRYPT_ALG_HANDLE algorithm = nullptr; BCRYPT_HASH_HANDLE hash = nullptr;
        std::array<UCHAR, 32> digest{};
        if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) Fail(L"Open installation identity hash provider failed.");
        const NTSTATUS status = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0);
        const bool ok = status >= 0 && BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data())), static_cast<ULONG>(bytes.size()), 0) >= 0 &&
            BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) >= 0;
        if (hash) BCryptDestroyHash(hash); BCryptCloseAlgorithmProvider(algorithm, 0);
        if (!ok) Fail(L"Hash installation identity failed.");
        std::wstring identity; constexpr wchar_t hex[] = L"0123456789abcdef";
        for (auto value : digest) { identity += hex[value >> 4]; identity += hex[value & 15]; }
        for (const auto* kind : {L"Launcher", L"Backend"}) {
            const auto name = L"Local\\TheJockeyClub-" + std::wstring(kind) + L"-" + identity;
            Handle mutex(CreateMutexW(nullptr, FALSE, name.c_str()));
            if (!mutex.valid()) WinFail(L"Check Clubhouse installation lease");
            const DWORD result = WaitForSingleObject(mutex.get(), 0);
            if (result != WAIT_OBJECT_0 && result != WAIT_ABANDONED)
                Fail(L"Clubhouse launcher or backend is active for this installation. Close Clubhouse before installing or recovering.");
            owned_.push_back(std::move(mutex));
        }
    }
    ~ClubhouseLease() { for (auto& mutex : owned_) ReleaseMutex(mutex.get()); }
private:
    std::vector<Handle> owned_;
};

struct JournalOp {
    char kind = 0;
    std::wstring source, destination;
    Fingerprint bytes;
};

struct JournalData {
    std::wstring state;
    std::vector<JournalOp> operations;
    std::set<std::size_t> undone;
    bool committed = false, restore = false;
};

bool OwnedFile(const std::wstring& relative, const std::wstring& state) {
    const auto prefix = state + L"\\files\\";
    if (relative.size() <= prefix.size() || !Same(relative.substr(0, prefix.size()), prefix)) return false;
    const auto name = relative.substr(prefix.size());
    const auto dot = name.find(L'.');
    return dot != std::wstring::npos && Digits(name.substr(0, dot)) &&
        (name.substr(dot) == L".stage" || name.substr(dot) == L".old" || name.substr(dot) == L".duplicate");
}

bool ApprovedDirectory(const std::wstring& relative, const std::wstring& state) {
    if (Same(relative, L"TheJockeyClub") || Same(relative, L"TheJockeyClub\\tools") ||
        Same(relative, L"TheJockeyClub\\install-state") || Same(relative, state) || Same(relative, state + L"\\files") ||
        Same(relative, L"lml") || Same(relative, L"lml\\TheJockeyClub Text")) return true;
    return PayloadPath(relative + L"\\_directory_check");
}

void CheckJournalOp(const JournalOp& op, const std::wstring& state) {
    CheckComponents(op.source, 0);
    if (op.source.empty()) Fail(L"Empty journal path.");
    if (op.kind == 'D') {
        if (!ApprovedDirectory(op.source, state)) Fail(L"Journal directory escaped the mod layout: " + op.source);
    } else if (op.kind == 'E') {
        bool allowed = false;
        for (const auto& directory : LegacyDirectories()) allowed = allowed || Same(op.source, directory.first) || Under(op.source, directory.first);
        if (!allowed) Fail(L"Journal old directory is outside the migration allowlist: " + op.source);
    } else if (op.kind == 'C') {
        if (!OwnedFile(op.source, state) || op.source.substr(op.source.size() - 6) != L".stage") Fail(L"Journal staging path is invalid: " + op.source);
    } else if (op.kind == 'M') {
        CheckComponents(op.destination, 0);
        const auto legacy = LegacyDestination(op.source);
        const bool allowed = (!legacy.empty() && (Same(legacy, op.destination) || OwnedFile(op.destination, state))) ||
            (PayloadPath(op.source) && OwnedFile(op.destination, state)) ||
            (OwnedFile(op.source, state) && PayloadPath(op.destination));
        if (!allowed || Same(op.source, op.destination)) Fail(L"Journal move is outside the migration allowlist: " + op.source + L" -> " + op.destination);
    } else Fail(L"Unknown journal operation.");
}

void WriteDurable(HANDLE file, const std::string& bytes) {
    DWORD written = 0;
    if (bytes.size() > MAXDWORD || !WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) || written != bytes.size())
        WinFail(L"Write installation recovery journal");
    if (!FlushFileBuffers(file)) WinFail(L"Flush installation recovery journal");
}

std::string JournalText(const JournalOp& op) {
    std::string result(1, op.kind);
    if (op.kind == 'M') result += "\t" + std::to_string(op.bytes.size) + "\t" + op.bytes.hash;
    result += "\t" + Utf8(op.source);
    if (op.kind == 'M') result += "\t" + Utf8(op.destination);
    return result + "\n";
}

std::uint64_t Decimal(const std::string& value) {
    if (value.empty()) Fail(L"Empty journal number.");
    std::uint64_t result = 0;
    for (char c : value) {
        if (c < '0' || c > '9' || result > (std::numeric_limits<std::uint64_t>::max() - (c - '0')) / 10) Fail(L"Invalid journal number.");
        result = result * 10 + (c - '0');
    }
    return result;
}

JournalData ParseJournal(const std::string& text) {
    JournalData result;
    std::size_t begin = 0, line_number = 0;
    while (begin < text.size()) {
        const auto end = text.find('\n', begin);
        // A torn final append cannot have authorized its mutation: operations
        // run only after the complete newline and FlushFileBuffers succeeded.
        if (end == std::string::npos) break;
        const auto line = text.substr(begin, end - begin); begin = end + 1;
        std::vector<std::string> fields; std::size_t cursor = 0;
        do {
            const auto tab = line.find('\t', cursor);
            fields.push_back(line.substr(cursor, tab == std::string::npos ? tab : tab - cursor));
            if (tab == std::string::npos) break;
            cursor = tab + 1;
        } while (true);
        ++line_number;
        if (line_number == 1) {
            if (line != "TJC_INSTALL_JOURNAL 1") Fail(L"Unsupported installation recovery journal.");
        } else if (line_number == 2) {
            if (fields.size() != 2 || fields[0] != "STATE") Fail(L"Missing recovery journal state.");
            result.state = Wide(fields[1]);
            if (!Same(result.state, kBaseline) && !Same(result.state, kUpdate)) Fail(L"Unrecognized recovery journal state directory.");
        } else if (line == "COMMIT") result.committed = true;
        else if (line == "RESTORE") result.restore = true;
        else if (fields.size() == 2 && fields[0] == "R") {
            const auto index = Decimal(fields[1]);
            if (index >= result.operations.size()) Fail(L"Invalid rollback checkpoint.");
            result.undone.insert(static_cast<std::size_t>(index));
        } else {
            if (result.committed || !result.undone.empty()) Fail(L"Operation follows a completed journal transaction.");
            JournalOp op;
            if (fields.size() == 5 && fields[0] == "M") {
                op.kind = 'M'; op.bytes.size = Decimal(fields[1]); op.bytes.hash = fields[2];
                if (op.bytes.hash.size() != 64 || op.bytes.hash.find_first_not_of("0123456789abcdef") != std::string::npos) Fail(L"Invalid recovery SHA256.");
                op.source = Wide(fields[3]); op.destination = Wide(fields[4]);
            } else if (fields.size() == 2 && (fields[0] == "D" || fields[0] == "E" || fields[0] == "C")) {
                op.kind = fields[0][0]; op.source = Wide(fields[1]);
            } else Fail(L"Malformed recovery journal row.");
            CheckJournalOp(op, result.state); result.operations.push_back(std::move(op));
            if (result.operations.size() > kMaxMigrationFiles * 8) Fail(L"Recovery journal operation limit exceeded.");
        }
    }
    if (line_number < 2 || result.state.empty()) Fail(L"Incomplete recovery journal header. No payload move has been authorized; retain the journal for inspection.");
    return result;
}

std::string ReadJournal(const std::wstring& path, DirectoryGuards& guards) {
    auto file = ReadLocked(path, guards);
    const auto size = FileSize(file.get());
    if (size > 64 * 1024 * 1024) Fail(L"Recovery journal exceeds its 64 MiB bound.");
    std::string text(static_cast<std::size_t>(size), '\0'); DWORD count = 0;
    if (!ReadFile(file.get(), text.data(), static_cast<DWORD>(text.size()), &count, nullptr) || count != size) WinFail(L"Read recovery journal");
    return text;
}

void RenameVerified(const std::wstring& target, const std::wstring& source, const std::wstring& destination,
        const Fingerprint& expected, DirectoryGuards& guards, Sha256& hashes) {
    const auto from = Join(target, source), to = Join(target, destination);
    guards.check(Parent(from), false); guards.check(Parent(to), false);
    if (Attributes(to) != INVALID_FILE_ATTRIBUTES) Fail(L"Refusing to overwrite an existing migration/recovery path: " + destination);
    Handle file(CreateFileW(Extended(from).c_str(), GENERIC_READ | DELETE, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    if (!file.valid()) WinFail(L"Lock exact file for atomic rename " + source);
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(file.get(), &info)) WinFail(L"Inspect file to rename " + source);
    if (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) Fail(L"Unsafe file to rename: " + source);
    if (!Equal(FingerprintOf(file.get(), hashes), expected)) Fail(L"File bytes changed; refusing migration/recovery: " + source);
    const auto name = Extended(to);
    std::vector<unsigned char> buffer(sizeof(FILE_RENAME_INFO) + name.size() * sizeof(wchar_t));
    auto* rename = reinterpret_cast<FILE_RENAME_INFO*>(buffer.data());
    rename->ReplaceIfExists = FALSE; rename->RootDirectory = nullptr;
    rename->FileNameLength = static_cast<DWORD>(name.size() * sizeof(wchar_t));
    std::copy(name.begin(), name.end(), rename->FileName);
    if (!SetFileInformationByHandle(file.get(), FileRenameInfo, rename, static_cast<DWORD>(buffer.size())))
        WinFail(L"Atomically rename " + source + L" -> " + destination);
}

class Transaction {
public:
    Transaction(const std::wstring& target, const std::wstring& state, DirectoryGuards& guards, Sha256& hashes)
        : target_(target), guards_(guards), hashes_(hashes) {
        data.state = state;
        journal_ = Handle(CreateFileW(Extended(Join(target_, kActiveJournal)).c_str(), GENERIC_WRITE, FILE_SHARE_READ,
            nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        if (!journal_.valid()) WinFail(L"Create recovery journal (an interrupted installation may need recovery)");
        WriteDurable(journal_.get(), "TJC_INSTALL_JOURNAL 1\nSTATE\t" + Utf8(state) + "\n");
    }
    void record(JournalOp op) {
        CheckJournalOp(op, data.state);
        WriteDurable(journal_.get(), JournalText(op)); data.operations.push_back(std::move(op));
    }
    void directory(const std::wstring& relative) {
        if (relative.empty()) return;
        const auto path = Join(target_, relative); const DWORD attributes = Attributes(path);
        if (attributes != INVALID_FILE_ATTRIBUTES) {
            if (!(attributes & FILE_ATTRIBUTE_DIRECTORY)) Fail(L"Directory is occupied by a file: " + relative);
            guards_.check(path, false); return;
        }
        const auto split = relative.find_last_of(L'\\');
        if (split != std::wstring::npos) directory(relative.substr(0, split));
        record({'D', relative, {}, {}});
        if (!CreateDirectoryW(Extended(path).c_str(), nullptr)) WinFail(L"Create transaction directory " + relative);
        guards_.check(path, false);
    }
    std::wstring reserve(const wchar_t* suffix) {
        directory(data.state + L"\\files");
        return data.state + L"\\files\\" + std::to_wstring(++serial_) + suffix;
    }
    std::wstring stage(Entry& entry) {
        const auto relative = reserve(L".stage"); record({'C', relative, {}, {}});
        Handle output(CreateFileW(Extended(Join(target_, relative)).c_str(), GENERIC_READ | GENERIC_WRITE, 0,
            nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        if (!output.valid()) WinFail(L"Create payload staging file");
        CopyToTemporary(entry.source.get(), output.get(), entry.relative);
        if (!Equal(FingerprintOf(output.get(), hashes_), Fingerprint{entry.size, entry.hash})) Fail(L"Staged payload did not match manifest: " + entry.relative);
        return relative;
    }
    void move(const std::wstring& from, const std::wstring& to, const Fingerprint& bytes) {
        const auto split = to.find_last_of(L'\\');
        if (split != std::wstring::npos) directory(to.substr(0, split));
        record({'M', from, to, bytes}); RenameVerified(target_, from, to, bytes, guards_, hashes_);
    }
    void removeDirectory(const std::wstring& relative) {
        record({'E', relative, {}, {}}); guards_.release(Join(target_, relative));
        if (!RemoveDirectoryW(Extended(Join(target_, relative)).c_str())) WinFail(L"Remove migrated empty directory " + relative);
    }
    void commit() { WriteDurable(journal_.get(), "COMMIT\n"); data.committed = true; }
    void close() { journal_.reset(); }
    JournalData data;
private:
    std::wstring target_;
    DirectoryGuards& guards_; Sha256& hashes_;
    Handle journal_; std::uint64_t serial_ = 0;
};

struct VirtualFile { int kind = 0; Fingerprint bytes; }; // 0 absent, 1 file, 2 directory

void VerifyRollback(const JournalData& journal, const std::wstring& target, DirectoryGuards& guards, Sha256& hashes) {
    std::map<std::wstring, VirtualFile, CaseLess> files;
    auto get = [&](const std::wstring& relative) -> VirtualFile& {
        const auto found = files.find(relative);
        if (found != files.end()) return found->second;
        VirtualFile value;
        const auto path = Join(target, relative);
        if (guards.check(Parent(path), false)) {
            const DWORD attributes = Attributes(path);
            if (attributes != INVALID_FILE_ATTRIBUTES) {
                if (attributes & FILE_ATTRIBUTE_DIRECTORY) value.kind = 2;
                else { auto file = ReadLocked(path, guards); value.kind = 1; value.bytes = FingerprintOf(file.get(), hashes); }
            }
        }
        return files.emplace(relative, std::move(value)).first->second;
    };
    for (std::size_t index = journal.operations.size(); index-- > 0;) {
        if (journal.undone.count(index)) continue;
        const auto& op = journal.operations[index];
        if (op.kind == 'M') {
            auto& source = get(op.source); auto& destination = get(op.destination);
            if (source.kind == 1 && Equal(source.bytes, op.bytes) && !destination.kind) continue; // Not performed, or just undone before a crash.
            if (source.kind || destination.kind != 1 || !Equal(destination.bytes, op.bytes))
                Fail(L"Recovery conflict: " + op.source + L" <-> " + op.destination + L". Current bytes differ from the recorded installation; no recovery changes were made.");
            source = destination; destination = {};
        } else if (op.kind == 'C') {
            auto& source = get(op.source);
            if (source.kind == 2) Fail(L"Recovery staging path became a directory: " + op.source);
            source = {};
        } else if (op.kind == 'E') {
            auto& source = get(op.source);
            if (source.kind == 1) Fail(L"Recovery directory is occupied by a file: " + op.source);
            source.kind = 2;
        } else {
            auto& source = get(op.source);
            if (source.kind == 1) Fail(L"Recovery-created directory is occupied by a file: " + op.source);
            if (source.kind == 2) {
                const auto path = Join(target, op.source);
                if (Attributes(path) != INVALID_FILE_ATTRIBUTES) {
                    guards.check(path, false);
                    for (const auto& child : Children(path)) {
                        const auto relative = op.source + L"\\" + child;
                        if (Same(relative, journal.state + L"\\journal.tsv")) continue;
                        if (get(relative).kind)
                            Fail(L"Recovery would remove a directory containing a later/untracked file: " + relative + L". Keep this file and resolve the directory explicitly.");
                    }
                }
            }
            source = {};
        }
    }
}

Handle AppendJournal(const std::wstring& target, std::size_t complete_bytes) {
    Handle file(CreateFileW(Extended(Join(target, kActiveJournal)).c_str(), GENERIC_WRITE, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (!file.valid()) WinFail(L"Open recovery journal for checkpoints");
    LARGE_INTEGER end{}; end.QuadPart = static_cast<LONGLONG>(complete_bytes);
    if (!SetFilePointerEx(file.get(), end, nullptr, FILE_BEGIN) || !SetEndOfFile(file.get())) WinFail(L"Discard incomplete journal tail");
    return file;
}

void CheckStoppedPeriodically(const std::wstring& target) {
    static ULONGLONG next_check = 0;
    const auto now = GetTickCount64();
    if (now >= next_check) { RequireGameStopped(target); next_check = now + 250; }
}

void RollbackJournal(JournalData& journal, const std::string& text, const std::wstring& target,
        DirectoryGuards& guards, Sha256& hashes, Counts& counts) {
    RequireGameStopped(target);
    VerifyRollback(journal, target, guards, hashes);
    auto append = AppendJournal(target, text.find_last_of('\n') + 1);
    if (journal.restore) {
        const auto retained = Join(target, journal.state + L"\\journal.tsv");
        if (Attributes(retained) != INVALID_FILE_ATTRIBUTES && !DeleteFileW(Extended(retained).c_str()))
            WinFail(L"Remove copied restoration journal");
    }
    Print(L"ROLLBACK: restoring verified changes in reverse order.");
    for (std::size_t index = journal.operations.size(); index-- > 0;) {
        if (journal.undone.count(index)) continue;
        CheckStoppedPeriodically(target);
        const auto& op = journal.operations[index];
        const auto source = Join(target, op.source);
        if (op.kind == 'M') {
            const DWORD attributes = Attributes(source);
            if (attributes != INVALID_FILE_ATTRIBUTES) {
                auto file = ReadLocked(source, guards);
                if (!Equal(FingerprintOf(file.get(), hashes), op.bytes)) Fail(L"Recovery source changed: " + op.source);
            } else {
                RenameVerified(target, op.destination, op.source, op.bytes, guards, hashes);
                ++counts.rolled_back;
                Print(L"ROLLED BACK " + op.destination + L" -> " + op.source);
            }
        } else if (op.kind == 'C') {
            if (Attributes(source) != INVALID_FILE_ATTRIBUTES && !DeleteFileW(Extended(source).c_str())) WinFail(L"Remove owned staging file " + op.source);
        } else if (op.kind == 'E') {
            if (Attributes(source) == INVALID_FILE_ATTRIBUTES && !CreateDirectoryW(Extended(source).c_str(), nullptr)) WinFail(L"Restore old directory " + op.source);
            guards.check(source, false);
        } else {
            guards.release(source);
            if (Attributes(source) != INVALID_FILE_ATTRIBUTES && !RemoveDirectoryW(Extended(source).c_str())) WinFail(L"Remove new empty directory " + op.source);
        }
        WriteDurable(append.get(), "R\t" + std::to_string(index) + "\n");
        journal.undone.insert(index);
    }
    append.reset();
    if (!DeleteFileW(Extended(Join(target, kActiveJournal)).c_str())) WinFail(L"Remove completed recovery journal");
    counts.installed = 0;
    Print(L"Recovery complete. Recorded files and legacy paths restored.");
}

void FinalizeJournal(const JournalData& journal, const std::wstring& target, DirectoryGuards& guards, Sha256& hashes) {
    RequireGameStopped(target);
    if (Same(journal.state, kBaseline)) {
        const auto destination = Join(target, journal.state + L"\\journal.tsv");
        if (Attributes(destination) != INVALID_FILE_ATTRIBUTES) Fail(L"A retained migration journal already exists; refusing to replace it.");
        if (!MoveFileExW(Extended(Join(target, kActiveJournal)).c_str(), Extended(destination).c_str(), MOVEFILE_WRITE_THROUGH))
            WinFail(L"Retain verified legacy restoration journal");
        Print(L"Legacy restoration baseline retained: " + journal.state + L"\\journal.tsv");
        return;
    }
    std::map<std::wstring, Fingerprint, CaseLess> leftovers;
    for (const auto& op : journal.operations) {
        if (op.kind == 'M' && OwnedFile(op.destination, journal.state)) leftovers[op.destination] = op.bytes;
    }
    for (const auto& item : leftovers) {
        const auto path = Join(target, item.first);
        if (Attributes(path) == INVALID_FILE_ATTRIBUTES) continue;
        auto file = ReadLocked(path, guards);
        if (!Equal(FingerprintOf(file.get(), hashes), item.second)) Fail(L"Committed transaction backup changed; cleanup stopped: " + item.first);
        file.reset();
        if (!DeleteFileW(Extended(path).c_str())) WinFail(L"Remove committed update backup " + item.first);
    }
    for (auto i = journal.operations.rbegin(); i != journal.operations.rend(); ++i) {
        if (i->kind != 'D' || (!Same(i->source, journal.state) && !Under(i->source, journal.state))) continue;
        const auto path = Join(target, i->source); guards.release(path);
        if (Attributes(path) != INVALID_FILE_ATTRIBUTES && !RemoveDirectoryW(Extended(path).c_str())) WinFail(L"Remove completed update staging directory " + i->source);
    }
    if (!DeleteFileW(Extended(Join(target, kActiveJournal)).c_str())) WinFail(L"Remove completed installation journal");
}

bool IncompleteInitialHeader(const std::string& text) {
    for (const auto* state : {kBaseline, kUpdate}) {
        const auto header = "TJC_INSTALL_JOURNAL 1\nSTATE\t" + Utf8(state) + "\n";
        if (text.size() < header.size() && header.compare(0, text.size(), text) == 0) return true;
    }
    return false;
}

void RecoverRestoreReady(const std::wstring& target, DirectoryGuards& guards, bool dry_run) {
    // Scratch has never authorized a restore mutation. Only an exact prefix
    // of the retained plan is ours to discard after an interrupted write.
    const auto baseline = ReadJournal(Join(target, std::wstring(kBaseline) + L"\\journal.tsv"), guards);
    const auto parsed = ParseJournal(baseline);
    if (!parsed.committed || parsed.restore || !parsed.undone.empty() || !Same(parsed.state, kBaseline)) Fail(L"Invalid baseline for interrupted restore preparation.");
    const auto ready = ReadJournal(Join(target, kRestoreReady), guards);
    const auto expected = baseline + "RESTORE\n";
    if (ready.size() > expected.size() || expected.compare(0, ready.size(), ready) != 0)
        Fail(L"Restore preparation scratch differs from the retained baseline; no files were changed: " + std::wstring(kRestoreReady));
    if (dry_run) { Print(L"DRY RUN OK. Interrupted restore preparation can be discarded; no changes made."); return; }
    RequireGameStopped(target);
    if (!DeleteFileW(Extended(Join(target, kRestoreReady)).c_str())) WinFail(L"Discard interrupted restore preparation");
    Print(L"Interrupted restore preparation discarded. Installed and legacy data were unchanged.");
}

void RecoverActive(const std::wstring& target, DirectoryGuards& guards, Sha256& hashes, Counts& counts) {
    const auto text = ReadJournal(Join(target, kActiveJournal), guards);
    if (IncompleteInitialHeader(text)) {
        RequireGameStopped(target);
        if (!DeleteFileW(Extended(Join(target, kActiveJournal)).c_str())) WinFail(L"Remove interrupted initial recovery header");
        Print(L"Interrupted initial journal removed. No payload operation had been authorized.");
        return;
    }
    auto journal = ParseJournal(text);
    if (journal.committed && !journal.restore) FinalizeJournal(journal, target, guards, hashes);
    else RollbackJournal(journal, text, target, guards, hashes, counts);
}

void RestoreLegacy(const std::wstring& target, DirectoryGuards& guards, Sha256& hashes, Counts& counts, bool dry_run) {
    const auto retained = Join(target, std::wstring(kBaseline) + L"\\journal.tsv");
    const auto original = ReadJournal(retained, guards);
    if (original.size() < 7 || original.compare(original.size() - 7, 7, "COMMIT\n") != 0)
        Fail(L"Retained migration journal does not end with a complete commit record.");
    auto journal = ParseJournal(original);
    if (!journal.committed || journal.restore || !Same(journal.state, kBaseline) || !journal.undone.empty()) Fail(L"The retained legacy baseline is not a completed migration.");
    VerifyRollback(journal, target, guards, hashes);
    if (dry_run) { Print(L"DRY RUN OK. Legacy restoration verified; no changes made."); return; }
    RequireGameStopped(target);
    auto text = original + "RESTORE\n";
    Handle ready(CreateFileW(Extended(Join(target, kRestoreReady)).c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
        CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (!ready.valid()) WinFail(L"Create explicit restoration preparation");
    WriteDurable(ready.get(), text); ready.reset();
    if (Attributes(Join(target, kActiveJournal)) != INVALID_FILE_ATTRIBUTES) Fail(L"Another active recovery journal appeared; restoration was not started.");
    if (!MoveFileExW(Extended(Join(target, kRestoreReady)).c_str(), Extended(Join(target, kActiveJournal)).c_str(), MOVEFILE_WRITE_THROUGH))
        WinFail(L"Atomically publish the complete restoration plan");
    journal.restore = true;
    RollbackJournal(journal, text, target, guards, hashes, counts);
}

void InstallTransaction(std::vector<Entry>& entries, Migration& migration, const std::wstring& target,
        DirectoryGuards& guards, Sha256& hashes, Counts& counts) {
    const bool has_legacy = !migration.files.empty() || !migration.directories.empty();
    const auto baseline = Join(target, std::wstring(kBaseline) + L"\\journal.tsv");
    if (has_legacy && Attributes(baseline) != INVALID_FILE_ATTRIBUTES)
        Fail(L"Legacy paths reappeared after the retained migration baseline. Resolve them before another migration; no second backup will be created.");
    const std::wstring state = has_legacy ? kBaseline : kUpdate;
    if (Attributes(Join(target, state)) != INVALID_FILE_ATTRIBUTES)
        Fail(L"An unrecognized transaction directory already exists: " + state + L". Retain it and use --recover-only if an active journal exists.");
    Transaction transaction(target, state, guards, hashes);
    try {
        transaction.directory(state);
        std::vector<std::pair<Entry*, std::wstring>> staged;
        // Stage and hash every payload before the first legacy or payload move.
        for (auto& entry : entries) {
            CheckStoppedPeriodically(target);
            if (entry.skip) { ++counts.preserved; Print(L"PRESERVED " + entry.relative); continue; }
            staged.emplace_back(&entry, transaction.stage(entry));
            if (entry.legacy_seed) entry.source.reset();
        }
        // Preserve empty directories as well as all unknown child files.
        for (const auto& directory : migration.directories) transaction.directory(directory.second);
        for (auto& file : migration.files) file.source.reset();
        for (auto& file : migration.files) {
            CheckStoppedPeriodically(target);
            file.source.reset();
            const auto destination = file.duplicate ? transaction.reserve(L".duplicate") : file.new_path;
            transaction.move(file.old_path, destination, file.bytes);
            Print(L"MIGRATED " + file.old_path + L" -> " + file.new_path);
        }
        for (auto i = migration.directories.rbegin(); i != migration.directories.rend(); ++i)
            transaction.removeDirectory(i->first);
        for (const auto& item : staged) {
            CheckStoppedPeriodically(target);
            auto& entry = *item.first;
            if (entry.mode != Mode::Replace && PreserveNow(entry, target, guards))
                Fail(L"A preserved destination appeared after preflight: " + entry.relative);
            const auto destination = Join(target, entry.relative);
            if (Attributes(destination) != INVALID_FILE_ATTRIBUTES) {
                if (!entry.had_destination) Fail(L"A destination appeared after preflight; refusing to overwrite it: " + entry.relative);
                auto old = ReadLocked(destination, guards); const auto bytes = FingerprintOf(old.get(), hashes); old.reset();
                if (!Equal(bytes, {entry.original_size, entry.original_hash})) Fail(L"Replacement bytes changed after preflight: " + entry.relative);
                transaction.move(entry.relative, transaction.reserve(L".old"), bytes);
            } else if (entry.had_destination) Fail(L"A replacement destination disappeared after preflight: " + entry.relative);
            transaction.move(item.second, entry.relative, {entry.size, entry.hash});
            ++counts.installed; Print(L"INSTALLED " + entry.relative);
        }
        RequireGameStopped(target);
        transaction.commit(); transaction.close();
        FinalizeJournal(transaction.data, target, guards, hashes);
    } catch (...) {
        const auto failure = std::current_exception();
        transaction.close();
        for (auto& file : migration.files) file.source.reset();
        if (transaction.data.committed) {
            ++counts.cleanup_failed;
            Fail(L"Installation committed; final journal cleanup was interrupted. Run --recover-only before installing again. Retain " + std::wstring(kActiveJournal));
        }
        try { RecoverActive(target, guards, hashes, counts); }
        catch (const Failure& error) {
            ++counts.rollback_failed;
            Fail(L"Installation stopped; recovery remains pending. No unverified file was overwritten. Run --recover-only after resolving: " + error.message);
        }
        std::rethrow_exception(failure);
    }
}
