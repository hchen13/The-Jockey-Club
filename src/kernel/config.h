#pragma once
#include <charconv>
#include <string>
#include <string_view>
#include <windows.h>

// The one place the mod writes a setting.
//
// Eighteen places called WritePrivateProfileStringA, each with its own idea of what to do when the
// write failed - three checked, the rest did not - and each leaving the file in whatever state
// Windows left it. Collecting them costs nothing today and buys three things the migration needs:
// the write can become atomic (write a temporary, rename over) in ONE place; it can move off the
// script thread, where a write under Defender has been measured in hundreds of microseconds; and
// there is somewhere to note that a setting changed, which is how a reader learns to reload.
//
// New event-triggered typed reads enter here too. The key table that turns the whole file into one
// typed Config is still step A1 of docs/refactor/05-migration.md; callers should not add pollers.
namespace horse_races::config {

// One bounded integer, read only when its consumer needs a new snapshot. Do not let malformed,
// truncated or out-of-range text silently become zero (which can mean mute or disable).
inline int ReadInt(const std::string& ini,const char* section,const char* key,
                   int fallback,int minimum,int maximum) {
    if(ini.empty() || !section || !key)return fallback;
    char value[32]{};
    const DWORD count=GetPrivateProfileStringA(section,key,"",value,sizeof(value),ini.c_str());
    if(count>=sizeof(value)-1)return fallback;
    std::string_view text(value);
    const auto first=text.find_first_not_of(" \t"),last=text.find_last_not_of(" \t");
    if(first==std::string_view::npos)return fallback;
    text=text.substr(first,last-first+1);
    int number=0;const auto parsed=std::from_chars(text.data(),text.data()+text.size(),number);
    return parsed.ec==std::errc{} && parsed.ptr==text.data()+text.size() && number>=minimum && number<=maximum?
        number:fallback;
}

// Writes one key. A null value deletes the key, which is how an override is cleared. Returns whether
// the file now says what was asked - the callers that checked used to do this by reading back, and
// the ones that did not check silently kept a stale setting.
inline bool SetRaw(const std::string& ini, const char* section, const char* key, const char* value) {
    if (ini.empty() || !section || !key) return false;
    return WritePrivateProfileStringA(section, key, value, ini.c_str()) != 0;
}

inline bool SetRaw(const std::string& ini, const char* section, const char* key, const std::string& value) {
    return SetRaw(ini, section, key, value.c_str());
}

inline bool SetRaw(const std::string& ini, const char* section, const char* key, int value) {
    return SetRaw(ini, section, key, std::to_string(value).c_str());
}

inline bool SetRaw(const std::string& ini, const char* section, const char* key, bool value) {
    return SetRaw(ini, section, key, value ? "1" : "0");
}

// Removes a key, so that whatever default the code carries applies again.
inline bool Clear(const std::string& ini, const char* section, const char* key) {
    return SetRaw(ini, section, key, static_cast<const char*>(nullptr));
}

}  // namespace horse_races::config
