#pragma once

#include <string>
#include <types.h>
#include "race_character_policy.h"

namespace horse_races::showcase {

// Copies of the site's racer, never the encounter or a story-owned entity.
struct Site {
    int id{};
    Vector3 anchor{};
    const char* rider_model{};
    Hash rider_outfit{};
    const char* horse_model{};
    bool female{};
    bool remove_original_hat{};
    bool all_rider_presets{}; // Whole outfits from this rider model, e.g. Mary-Beth.
    characters::Id character=characters::Id::OtherRival;
};

void Initialize(const std::string& directory);
bool Start(const Site& site);
bool Active();
int SiteId();
void ChangePage(int direction); // Left/Right with the menu closed; never starts a race.
void Tick();
void Clear(const char* reason);

} // namespace horse_races::showcase
