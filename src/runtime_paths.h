#pragma once
#include <filesystem>
#include <map>
#include <mutex>
#include <string>

// All inputs are the real directory containing RDR2.exe / this ASI. Never
// substitute the mod's data directory for the game root used by LML or saves.
namespace horse_races::runtime_paths {
namespace fs=std::filesystem;
inline bool EnsureDirectory(const fs::path& directory) {
    std::error_code error;fs::create_directories(directory,error);return !error;
}
struct Paths {
    fs::path game_root;
    bool legacy=false;
    bool horse_speed_lab=false;
    fs::path ModRoot()const{return game_root/"TheJockeyClub";}
    fs::path ConfigDirectory()const{return legacy?game_root:ModRoot()/"config";}
    fs::path Config(const fs::path& name)const{return ConfigDirectory()/name;}
    fs::path MainConfig()const{return Config(horse_speed_lab?"HorseSpeedLab.ini":"TheJockeyClub.ini");}
    fs::path Courses()const{return legacy?game_root/"TheJockeyClub-courses":ModRoot()/"courses";}
    fs::path LearningDirectory()const{return legacy?game_root:ModRoot()/"learning";}
    fs::path Learning(const fs::path& name)const{return LearningDirectory()/name;}
    fs::path Archives()const{return legacy?game_root/"TheJockeyClubData":ModRoot()/"data"/"archives";}
    fs::path Data()const{return legacy?game_root/"TheJockeyClub-data":ModRoot()/"data";}
    fs::path Recordings()const{return legacy?game_root/"TheJockeyClub-motion":ModRoot()/"recordings";}
    fs::path Logs()const{return legacy?game_root:ModRoot()/"logs";}
    fs::path Log(const fs::path& name)const{return Logs()/name;}
    fs::path Diagnostics()const{return legacy?game_root:ModRoot()/"diagnostics";}
    fs::path Diagnostic(const fs::path& name)const{return Diagnostics()/name;}
    bool PrepareRuntimeDirectories()const {
        bool ready=true;
        for(const auto& directory:{ConfigDirectory(),Courses(),LearningDirectory(),
            Archives(),Data(),Recordings(),Logs(),Diagnostics()})
            ready=EnsureDirectory(directory)&&ready;
        return ready;
    }
};
inline Paths Detect(const fs::path& game_root) {
    Paths paths{game_root.lexically_normal()};
    std::error_code error;
    // An unreadable/new-but-invalid config must not expose a different old
    // profile. Only an absent new main INI permits the whole legacy layout.
    if(fs::exists(paths.MainConfig(),error)||error)return paths;
    const auto previous=paths.game_root/"TheJockeyClub.ini";
    if(fs::exists(previous,error)){
        paths.legacy=fs::is_regular_file(previous,error)&&!error;return paths;
    }
    if(error)return paths;
    const auto ancient=paths.game_root/"HorseSpeedLab.ini";
    if(fs::exists(ancient,error)&&!error&&fs::is_regular_file(ancient,error)&&!error){
        paths.legacy=true;paths.horse_speed_lab=true;
    }
    return paths;
}
inline Paths Resolve(const fs::path& game_root) {
    // The startup save observer can run before ScriptMain. Pin one selection
    // for both readers and all save-load reinitializations; no per-file mixing,
    // migration, directory scans or repeated status probes in the game loop.
    static std::mutex mutex;
    static std::map<fs::path,Paths> selected;
    const auto key=game_root.lexically_normal();
    std::lock_guard lock(mutex);
    const auto found=selected.find(key);
    if(found!=selected.end())return found->second;
    return selected.emplace(key,Detect(key)).first->second;
}
} // namespace horse_races::runtime_paths
