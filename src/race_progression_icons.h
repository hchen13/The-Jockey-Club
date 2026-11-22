#pragma once
#include "race_progression.h"
#include "race_map_textures.h"

namespace horse_races::progression {
// Match the station map's duel/field distinction. Sprite and atlas hashes stay
// in map_textures, shared with the world-map markers and their streaming owner.
inline constexpr map_textures::Dictionary RaceIconDictionary(Race race){
    switch(race){
    case Race::MaryDuel:case Race::PearsonDuel:case Race::KarenDuel:
    case Race::CharlesDuel:case Race::BillDuel:case Race::SadieDuel:
    case Race::JavierDuel:case Race::ArthurDuel:
        return map_textures::Dictionary::Story;
    default:return map_textures::Dictionary::Multiplayer;
    }
}
}
