#pragma once

#include "horse_attributes.h"
#include <windows.h>
#include <bcrypt.h>
#include <filesystem>
#include "runtime_paths.h"
#include <fstream>
#include <unordered_map>
#include <vector>
#include <sstream>
#include <iomanip>
#include <cstring>
#pragma comment(lib, "bcrypt.lib")

namespace horse_attributes {
inline std::string Sha256File(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file || file.tellg() <= 0 || file.tellg() > 16*1024*1024) return {};
    std::vector<unsigned char> data(static_cast<std::size_t>(file.tellg())); file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()))) return {};
    BCRYPT_ALG_HANDLE algorithm{};
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) return {};
    unsigned char digest[32]{};
    const auto status = BCryptHash(algorithm, nullptr, 0, data.data(), static_cast<ULONG>(data.size()), digest, sizeof(digest));
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (status < 0) return {};
    std::ostringstream hex; hex << std::uppercase << std::hex << std::setfill('0');
    for (auto byte : digest) hex << std::setw(2) << static_cast<unsigned int>(byte);
    return hex.str();
}
class BackupCatalog {
    std::unordered_map<Hash, Ranks> ranks_;
public:
    std::string status = "not loaded";
    std::optional<Ranks> Find(Hash model) const {
        const auto it=ranks_.find(model); return it==ranks_.end() ? std::nullopt : std::optional<Ranks>{it->second};
    }
    bool Load(const std::string& directory, const std::string& config_path = {}) {
        try { return LoadChecked(directory,config_path); }
        catch (...) { ranks_.clear(); status="backup rejected: parse/read failure"; return false; }
    }
private:
    bool LoadChecked(const std::string& directory,const std::string& config_path) {
        ranks_.clear(); status="backup missing or invalid";
        const std::string ini=config_path.empty() ? horse_races::runtime_paths::Resolve(directory).Config("HorseAttributeBackup.ini").string() : config_path;
        auto field=[&](const char* section,const std::string& key) {
            char value[2048]{};
            const auto length=GetPrivateProfileStringA(section,key.c_str(),"",value,sizeof(value),ini.c_str());
            return length < sizeof(value)-1 ? std::string(value) : std::string{};
        };
        const int count=GetPrivateProfileIntA("Sources","Count",0,ini.c_str());
        if (count < 1 || count > 16) return false;
        FILETIME process_created{}, exited{}, kernel{}, user{};
        if (!GetProcessTimes(GetCurrentProcess(),&process_created,&exited,&kernel,&user)) return false;
        for (int i=0;i<count;++i) {
            const std::filesystem::path relative=field("Sources","Path"+std::to_string(i));
            if (relative.empty() || relative.is_absolute() || relative.has_root_name()) return false;
            for (const auto& part:relative) if (part==".." || part==".") return false;
            const auto path=std::filesystem::path(directory)/relative;
            const auto expected=field("Sources","SHA256_"+std::to_string(i));
            if (expected=="MISSING") {
                if (std::filesystem::exists(path)) { status="backup rejected: competing replacement restored"; return false; }
                continue;
            }
            if (expected.size()!=64 || expected.find_first_not_of("0123456789ABCDEF")!=std::string::npos) return false;
            WIN32_FILE_ATTRIBUTE_DATA info{};
            // Disk edits made after this game started are not the game's loaded
            // metadata. Never silently substitute such a future configuration.
            if (!GetFileAttributesExW(path.c_str(),GetFileExInfoStandard,&info) ||
                CompareFileTime(&info.ftLastWriteTime,&process_created)>0 ||
                Sha256File(path)!=expected) {
                status="backup rejected: source changed, missing, or newer than game process"; return false;
            }
        }
        std::vector<char> sections(65536);
        std::unordered_map<Hash,Ranks> parsed;
        if (GetPrivateProfileSectionNamesA(sections.data(),static_cast<DWORD>(sections.size()),ini.c_str())>=sections.size()-2) return false;
        for (const char* section=sections.data(); *section; section+=std::strlen(section)+1) {
            const std::string name=section;
            if (!name.starts_with("Horse.")) continue;
            if (name.size()!=14 || name.substr(6).find_first_not_of("0123456789ABCDEFabcdef")!=std::string::npos) return false;
            const auto model=static_cast<Hash>(std::stoul(name.substr(6),nullptr,16));
            auto rank=[&](const char* key) { const auto value=field(section,key);
                return value.size()==1 && value[0]>='0' && value[0]<='9' ? value[0]-'0' : -1; };
            const Ranks r{rank("Speed"),rank("Acceleration")};
            if (!model || !r.Valid() || !parsed.emplace(model,r).second) return false;
        }
        // Repeat the snapshot's streamed-model exclusion at load. Adding a new
        // per-model YMT must not leave a stale breed base eligible for fallback.
        std::size_t inspected=0, omitted=0;
        const auto lml=std::filesystem::path(directory)/"lml";
        if (std::filesystem::exists(lml)) for (const auto& entry:std::filesystem::recursive_directory_iterator(lml)) {
            if (++inspected>100000) { status="backup rejected: model override scan limit"; return false; }
            if (!entry.is_regular_file()) continue;
            auto name=entry.path().filename().string();
            for (auto& c:name) if (c>='A' && c<='Z') c+= 'a'-'A';
            if (!name.starts_with("a_c_horse") || !name.ends_with(".ymt")) continue;
            name.resize(name.size()-4);
            Hash hash=0;
            for (unsigned char c:name) { hash+=c; hash+=hash<<10; hash^=hash>>6; }
            hash+=hash<<3; hash^=hash>>11; hash+=hash<<15;
            omitted+=parsed.erase(hash);
        }
        ranks_=std::move(parsed);
        status="source-verified backup models="+std::to_string(ranks_.size());
        if (omitted) status+="; excluded current streamed overrides="+std::to_string(omitted);
        return !ranks_.empty();
    }
};
} // namespace horse_attributes
