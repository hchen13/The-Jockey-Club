#pragma once
#include <algorithm>
#include <string_view>
#include <vector>
#include "fixed_horse_pool.h"
#include "race_native_follower.h"
#include "race_live_overtake.h"
#include "race_pace_plan.h"
#include "race_catchup.h"
#include "race_far_catchup.h"
#include "race_racing_line.h"
#include "race_appearance.h"
#include "race_horse_compat.h"
#include "race_horse_avoidance.h"
#include "race_circuit.h"
#include "race_localization.h"
#include "horse_story_support.h"
#include "race_breeds.h"
#include "race_host_horses.h"
#include "horse_catalog.h"
#include <span>
#include <string_view>

namespace horse_races::field_race {
// One name per entrant, shared by the lap HUD and the ped's interaction
// prompt: the leader shown on screen must be the rider the player can look at.
inline std::string EntrantName(std::size_t index) {
    return std::string(i18n::Text("Jockey ","骑手"))+std::to_string(index+1);
}
// The breed the GAME reports for a model, not one inferred from its name.
inline std::string BreedLabel(std::string_view model) {
    const auto* labels=horse_catalog::current.Labels(model);
    if(!labels || !HUD::DOES_TEXT_LABEL_EXIST(labels->breed))return {};
    const char* text=HUD::_GET_LABEL_TEXT_2(labels->breed);
    return text?text:std::string{};
}
// Broad eligibility only; ReservedHostModels below controls a breed host's
// actual draw. Accompaniment has its own policy below, including the shared
// seven-coat pool in the two protagonist Thoroughbred stakes.
inline bool HostMayRide(characters::Id host,std::string_view model) {
    if(host_horses::Eligible(host,model))return true;
    for(const auto reserved:host_horses::kArthur)if(horse_identity::SameCoat(model,reserved))return false;
    return host_horses::Common(model) || host_horses::Lent(model);
}
inline std::span<const std::string_view> ProtagonistStable(characters::Id host) {
    if(host==characters::Id::Arthur)return host_horses::kArthur;
    if(host==characters::Id::John)return host_horses::kJohn;
    return {};
}
// The entire reserved pool, not just today's winner of the draw. John keeps
// his six-mount ordinary pool but his Thoroughbred stakes has exactly two.
inline std::vector<std::string_view> ReservedHostModels(const Breed& breed,characters::Id host) {
    std::vector<std::string_view> result;
    const auto add=[&](std::string_view model){if(SameBreed(breed,model))result.push_back(model);};
    if(host==characters::Id::John && breed.id=="Thoroughbred") {
        add("A_C_HORSE_JOHN_ENDLESSSUMMER");add("A_C_HORSE_THOROUGHBRED_REVERSEDAPPLEBLACK");return result;
    }
    const auto stable=ProtagonistStable(host);
    if(!stable.empty()){for(auto model:stable)add(model);return result;}
    if(const auto* profile=hosts::Find(host))for(auto model:profile->own_mounts)add(model);
    if(!result.empty())return result;
    struct Assignment {characters::Id host;std::string_view breed,model;};
    constexpr Assignment assigned[]{
        {characters::Id::Charles,"Arabian","A_C_HORSE_GANG_DUTCH"},
        {characters::Id::Trelawny,"AmericanStandardbred","A_C_HORSE_GANG_SEAN"},
        {characters::Id::RainsFall,"AmericanPaint","A_C_HORSE_AMERICANPAINT_GREYOVERO"},
        {characters::Id::Tilly,"TennesseeWalker","A_C_HORSE_TENNESSEEWALKER_MAHOGANYBAY"}};
    for(const auto& row:assigned)if(row.host==host && row.breed==breed.id)add(row.model);
    return result;
}
inline bool AccompanimentMayRide(const Breed& breed,characters::Id host,std::string_view model,
    std::string_view selected_host_model={}) {
    // These two stakes share all seven Thoroughbred coats. Only the mount
    // actually drawn for the host is reserved; the unused host option joins
    // the jockeys, including the otherwise protected Arthur coats.
    if(breed.id=="Thoroughbred" && (host==characters::Id::John || host==characters::Id::Arthur)) {
        if(!SameBreed(breed,model) || (!selected_host_model.empty() &&
            horse_identity::SameCoat(model,selected_host_model)))return false;
        for(auto coat:kThoroughbredSpawnCoats)if(horse_identity::SameCoat(model,coat))return true;
        return false;
    }
    if(!SameBreed(breed,model) || !host_horses::Common(model))return false;
    for(auto reserved:ReservedHostModels(breed,host))if(horse_identity::SameCoat(model,reserved))return false;
    return true;
}
// Uniform draw of the available reserved pool. Missing all reserved models is
// a pre-offer failure, never permission to borrow a different owner's horse.
template<class Available,class Random>
std::string_view ChooseHostHorse(const Breed& breed,characters::Id host,Available available,Random random,const char*& how) {
    std::vector<std::string_view> candidates;
    std::set<std::string> keys;
    for(auto model:ReservedHostModels(breed,host))
        if(HostMayRide(host,model) && available(model) && keys.insert(horse_identity::Key(model)).second)candidates.push_back(model);
    how=!ProtagonistStable(host).empty()?"own stable":
        (!candidates.empty() && host_horses::OwnMount(host,candidates.front())?"own mount":"host coat");
    if(candidates.empty()){how="none";return {};}
    const int pick=candidates.size()>1?random(static_cast<int>(candidates.size())):0;
    return pick>=0 && static_cast<std::size_t>(pick)<candidates.size()?candidates[static_cast<std::size_t>(pick)]:std::string_view{};
}
inline bool EligiblePlayerModel(const Breed& breed,Hash model) {
    for(const auto& [name,definition]:horse_catalog::current.definitions)
        if(model==MISC::GET_HASH_KEY(name.c_str()))return SameBreed(breed,name);
    for(auto host:breed.host_horses)if(model==MISC::GET_HASH_KEY(host.data()))return SameBreed(breed,host);
    return std::any_of(breed.allowed.begin(),breed.allowed.end(),
        [&](auto name){return model==MISC::GET_HASH_KEY(name.data());});
}
struct Entrant {
    fixed_horses::Choice choice;
    horse_attributes::Ranks target;
    Hash rider_model=0;
    race_horses::Identity rider{},horse{};
    race_horses::Lease lease;
    race_avoidance::Lease racing_assist;
    appearance::Session appearance;
    native_follower::Follower follower;
    live_overtake::Session avoidance;
    race_pace::Plan pace;
    far_catchup::Runner far_runner;
    // The line this rider is driven on: the corridor's racing line at this
    // rider's own aggression, node for node on g_course's nodes (so restarts
    // find their entry on g_course and the follower is ordered in centreline
    // metres). line_jitter is drawn ONCE, at the first start, and scales the
    // aggression only - a scale <= 1 of a verified line is still legal.
    racing_line::Result line;
    float line_jitter=-1;     // u in [0,1]; -1 = not drawn yet.
    float line_aggression=0;  // The aggression the line was built with.
    float terrain_caution=-1; // Per-race risk draw; preserved through renewals/rebuilds.
    int line_rebuilds=0;      // Live rebuilds (width/percent change), counted apart from restarts.
    race_pace::Controller controller;
    race_pace::Command command;
    catchup::State chase;
    circuit::Progress progress;
    course::Progress proxy;
    std::size_t traffic_target=0;
    bool traffic_speed_owned=false;
    double task_at=0;
    double stalled_since=-1;
    Blip blip=0;
    native_follower::RetryBudget retries;
    native_follower::Recovery navigation;
    int source_site=-1; // Station id the field raced at; a direction renews only after the pair retires.
    std::string inspection_name; // Stable Jockey N even as earlier retiring entries leave the vector.
    bool out=false,retiring=false;
};
// Losing/eliminating one NPC must not end a multi-opponent race. Compare the
// first real finish against the player, using same-sample crossing fractions.
inline std::optional<std::size_t> FirstFinisher(const std::vector<circuit::Advance>& rivals) {
    std::optional<std::size_t> first;
    for(std::size_t i=0;i<rivals.size();++i)if(rivals[i].finish_fraction &&
        (!first || *rivals[i].finish_fraction<*rivals[*first].finish_fraction))first=i;
    return first;
}
inline circuit::Decision Decide(const circuit::Advance& player,const std::vector<circuit::Advance>& rivals,bool all_out) {
    circuit::Advance leader;
    if(const auto first=FirstFinisher(rivals))leader.finish_fraction=rivals[*first].finish_fraction;
    if(all_out && !leader.finish_fraction)leader.disqualification_fraction=1.0F;
    return circuit::Decide(player,leader);
}
}

namespace horse_races::field_race {
// The field's coats (2026-09-19): the host's mount is drawn first (the
// horse plan), then its coat comes out of the field's pool - Arthur's blood
// bay is Arthur's, and a host without a signature horse who draws a coat
// keeps it to himself the same way - and the field draws without
// replacement: a coat rides again only once every other has ridden. A pool
// of one is a pool of one. `random(n)` answers 0..n-1. Returns `count`
// choices, or nothing for an empty pool or a random that misbehaves.
template<class Random>
inline std::vector<fixed_horses::Choice> DrawCoats(std::vector<fixed_horses::Choice> pool,std::string_view host_coat,int count,Random random) {
    std::vector<fixed_horses::Choice> drawn;
    if(pool.empty() || count<=0)return drawn;
    std::set<std::string> coats;
    std::erase_if(pool,[&](const fixed_horses::Choice& c){return !c.name ||
        horse_identity::SameCoat(c.name,host_coat) || !coats.insert(horse_identity::Key(c.name)).second;});
    if(pool.empty())return drawn;
    std::vector<fixed_horses::Choice> bag;
    for(int i=0;i<count;++i) {
        if(bag.empty())bag=pool;
        const int index=random(static_cast<int>(bag.size()));
        if(index<0 || static_cast<std::size_t>(index)>=bag.size())return {};
        drawn.push_back(bag[static_cast<std::size_t>(index)]);
        bag.erase(bag.begin()+index);
    }
    return drawn;
}
} // namespace horse_races::field_race
