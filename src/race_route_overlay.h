#pragma once
#include "race_route_definition.h"

namespace horse_races::route_config {
inline std::string Trim(std::string text)
{
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}
inline bool ValidId(const std::string& id)
{
    return !id.empty() && id.size() <= 64 && id.find_first_not_of(
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") == std::string::npos;
}
inline bool ValidLabel(const std::string& label)
{
    return !label.empty() && label.size() <= 96 && std::none_of(label.begin(), label.end(), [](unsigned char c) {
        return c < 32 || c == 127 || c == '~'; // No embedded controls or game formatting tokens.
    });
}
inline bool Number(const std::string& text, float& value)
{
    try {
        std::size_t consumed = 0;
        value = std::stof(text, &consumed);
        return consumed == text.size() && std::isfinite(value);
    } catch (...) { return false; }
}
inline bool Multiplier(const std::string& text, double& value)
{
    if(text=="Distance"){value=distance_pricing::kAutomatic;return true;}
    if(text.empty() || text.size()>16 || text.front()=='.' || text.back()=='.' ||
        text.find_first_not_of("0123456789.")!=std::string::npos)return false;
    try {
        std::size_t consumed=0;const double parsed=std::stod(text,&consumed);
        if(consumed!=text.size() || !wager::ScaleHalfUnits(parsed))return false;
        value=parsed;return true;
    } catch(...) {return false;}
}

// An overlay route file: the same race under another host. It names the file
// it stands on and says only what differs:
//     [Overlay]
//     Base=ArthurCampRaceRoutes.ini
//     Remove=HostPoint         ; a [Route] key of the base this route does without
//     [Route]
//     Host=John            ; replaces the base's key, or adds one it lacks
//     [Point.john_host_01] ; a point the base does not have
// BaseOf() is the file named, empty for an ordinary route file; Overlay() is
// the base's text with those differences written in, which Parse() then reads
// as it reads any route file. The course is never copied, so it cannot drift.
inline std::string BaseOf(const std::string& bytes)
{
    std::istringstream input(bytes);std::string line,section;
    while(std::getline(input,line)) {
        line=Trim(line);
        if(line.empty() || line.front()==';' || line.front()=='#')continue;
        if(line.front()=='['){section=line;continue;}
        if(section!="[Overlay]")continue;
        const auto equals=line.find('=');
        if(equals!=std::string::npos && Trim(line.substr(0,equals))=="Base")return Trim(line.substr(equals+1));
    }
    return {};
}
inline std::string Overlay(const std::string& base,const std::string& overlay)
{
    std::vector<std::pair<std::string,std::string>> keys;  // The overlay's [Route] lines, by key, in order.
    std::vector<std::string> removed;
    std::string extra,line,section;
    {
        std::istringstream input(overlay);
        while(std::getline(input,line)) {
            const std::string t=Trim(line);
            if(!t.empty() && t.front()=='['){section=t;if(section!="[Overlay]" && section!="[Route]")extra+=t+"\n";continue;}
            if(t.empty() || t.front()==';' || t.front()=='#')continue;
            const auto equals=t.find('=');
            if(section=="[Overlay]") {
                // Remove=KeyA,KeyB: [Route] keys of the base this route does without.
                if(equals!=std::string::npos && Trim(t.substr(0,equals))=="Remove") {
                    std::istringstream names(t.substr(equals+1));std::string name;
                    while(std::getline(names,name,','))if(!(name=Trim(name)).empty())removed.push_back(name);
                }
                continue;
            }
            if(section=="[Route]"){if(equals!=std::string::npos)keys.emplace_back(Trim(t.substr(0,equals)),t);}
            else extra+=t+"\n";
        }
    }
    const auto replaced=[&](const std::string& key){for(const auto& k:keys)if(k.first==key)return true;
        return std::find(removed.begin(),removed.end(),key)!=removed.end();};
    std::string out;bool in_route=false,written=false;
    const auto write=[&]{for(const auto& k:keys)out+=k.second+"\n";written=true;};
    std::istringstream input(base);
    while(std::getline(input,line)) {
        const std::string t=Trim(line);
        if(!t.empty() && t.front()=='[') {
            if(in_route && !written)write();
            in_route=t=="[Route]";
        } else if(in_route && !t.empty() && t.front()!=';' && t.front()!='#') {
            const auto equals=t.find('=');
            if(equals!=std::string::npos && replaced(Trim(t.substr(0,equals))))continue;
        }
        out+=t+"\n";
    }
    if(in_route && !written)write();
    return out+extra;
}
} // namespace horse_races::route_config
