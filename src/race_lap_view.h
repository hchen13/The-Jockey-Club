#pragma once
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <string_view>

namespace horse_races::circuit {
// The race board, one system for every race (the user, 2026-09-21: "把 HUD 做成体系，
// 而不是每个比赛一个一个地改"). Always two rows, always the same shape:
//
//   Arthur · Lap 2/3                          1/8
//   Leading Rival · Sadie Adler · Lap 2/3     +25m
//
// The lap is dropped when the race is one lap - which is what "Lap 1/1" on the
// mountain race, the one-on-one, Pearson's and Sadie's was saying for no reason.
// The player is named by their given name, the rival in full. The gap is signed
// from the player's seat: + they are behind, - they are ahead.
struct View {
    bool visible=false;
    int player_lap=0,opponent_lap=0,total=0;
    std::string opponent_name;
    // The standing is reported for every race, two horses included: a place is
    // what "who is winning" looks like when there are no laps to count.
    int player_place=0,field_size=0;
    bool player_leads=false;
    float gap_metres=0;                // The leading rival minus the player, along the course.
    bool FinalLap() const {return visible && total>0 && player_lap==total;}
    // "Arthur Morgan" -> "Arthur", "亚瑟·摩根" -> "亚瑟". The player is on first-name
    // terms with themselves; the rival keeps their full name.
    static std::string GivenName(std::string_view full) {
        const auto space=full.find(' ');
        const auto dot=full.find("\xC2\xB7");   // U+00B7, the separator in the Chinese names.
        const auto cut=std::min(space==std::string_view::npos?full.size():space,
                                dot==std::string_view::npos?full.size():dot);
        return std::string(full.substr(0,cut));
    }
    // Empty on a single-lap race: there is nothing to count.
    std::string Laps(int lap,bool chinese) const {
        if(total<2)return {};
        const std::string count=std::to_string(std::clamp(lap,1,total))+"/"+std::to_string(total);
        return chinese?"第 "+count+" 圈":"Lap "+count;
    }
    std::string Place() const {
        if(field_size<1 || player_place<1)return {};
        return std::to_string(std::clamp(player_place,1,field_size))+"/"+std::to_string(field_size);
    }
    // Quantised, or the board would be rewritten every frame by a metre count that
    // never settles: whole metres under 20, five-metre steps beyond, and kilometres
    // once a rival is a lap or a town away.
    std::string Gap() const {
        if(!std::isfinite(gap_metres))return {};
        const float magnitude=std::abs(gap_metres);
        const char* sign=magnitude<0.5F?"":gap_metres>0?"+":"-";
        if(magnitude>=1000) {
            char text[24];std::snprintf(text,sizeof text,"%s%.1fkm",sign,static_cast<double>(magnitude)/1000.0);
            return text;
        }
        const int metres=magnitude>=20?static_cast<int>(magnitude/5+.5F)*5:static_cast<int>(magnitude+.5F);
        return std::string(sign)+std::to_string(metres)+"m";
    }
    std::string PlayerRow(std::string_view name,bool chinese) const {
        if(!visible)return {};
        const std::string lap=Laps(player_lap,chinese);
        return GivenName(name)+(lap.empty()?std::string():" · "+lap);
    }
    std::string RivalRow(std::string_view name,bool chinese) const {
        if(!visible)return {};
        const std::string lap=Laps(opponent_lap,chinese);
        std::string row=chinese?"领先对手":"Leading Rival";
        if(!name.empty())row+=" · "+std::string(name);
        return lap.empty()?row:row+" · "+lap;
    }
};
}
