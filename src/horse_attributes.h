#pragma once

#include "game_natives.h"
#include <algorithm>
#include <optional>
#include <string>

namespace horse_attributes {
constexpr int kSpeed = 5, kAcceleration = 6;
struct Ranks {
    int speed = -1, acceleration = -1; // Raw ranks: 0..9, never display levels.
    bool Valid() const { return speed >= 0 && speed <= 9 && acceleration >= 0 && acceleration <= 9; }
    bool operator==(const Ranks&) const = default;
};
struct Snapshot {
    Hash model = 0;
    Ranks model_default, current, entity_base, bonus;
};
inline Ranks ReadModelDefaults(Hash model)
{
    if (!model) return {};
    return {invoke<int>(0x958DD43D41F89A47, model, kSpeed),
            invoke<int>(0x958DD43D41F89A47, model, kAcceleration)};
}
inline Snapshot Read(Ped horse)
{
    Snapshot s;
    if (!horse || !ENTITY::DOES_ENTITY_EXIST(horse) || ENTITY::IS_ENTITY_DEAD(horse)) return s;
    s.model = ENTITY::GET_ENTITY_MODEL(horse);
    // The older SDK misleadingly calls this argument Ped. Rockstar pause_menu
    // func_261/262 and the current native DB both pass a MODEL HASH, not a handle.
    s.model_default = ReadModelDefaults(s.model);
    s.current = {ATTRIBUTE::GET_ATTRIBUTE_RANK(horse,kSpeed), ATTRIBUTE::GET_ATTRIBUTE_RANK(horse,kAcceleration)};
    s.entity_base = {ATTRIBUTE::GET_ATTRIBUTE_BASE_RANK(horse,kSpeed), ATTRIBUTE::GET_ATTRIBUTE_BASE_RANK(horse,kAcceleration)};
    s.bonus = {ATTRIBUTE::GET_ATTRIBUTE_BONUS_RANK(horse,kSpeed), ATTRIBUTE::GET_ATTRIBUTE_BONUS_RANK(horse,kAcceleration)};
    return s;
}
struct Resolution {
    Ranks base;
    const char* source = "unresolved";
    bool mismatch = false;
};
inline Resolution ResolveNpcBase(const Snapshot& s, const std::optional<Ranks>& backup)
{
    if (!s.model) return {};
    const bool have_backup = backup && backup->Valid();
    const bool mismatch = have_backup && s.model_default.Valid() && s.model_default != *backup;
    // Zero is valid, even for both ranks. Conflicting files diagnose, never
    // overrule a valid runtime model default. Only invalid native data may fall back.
    if (s.model_default.Valid()) return {s.model_default, "runtime-model-default", mismatch};
    if (have_backup) return {*backup, "verified-file-backup", mismatch};
    return {};
}
inline int DisplayLevel(int raw) { return std::clamp(raw + 1, 1, 10); }
inline std::string Pair(const Ranks& r) { return std::to_string(r.speed) + "/" + std::to_string(r.acceleration); }
} // namespace horse_attributes
