#pragma once
#include "race_character_policy.h"
#include "race_host_profiles.h"
#include "fixed_horse_pool.h"
#include "horse_identity.h"
#include <set>
#include <algorithm>
#include <array>
#include <string_view>
#include <utility>

namespace horse_races::host_horses {
inline constexpr std::array<std::string_view,6> kArthur{
    "A_C_HORSE_HUNGARIANHALFBRED_LIVERCHESTNUT","A_C_HORSE_BUELL_WARVETS",
    "A_C_HORSE_THOROUGHBRED_BLOODBAY","A_C_HORSE_SHIRE_RAVENBLACK",
    "A_C_HORSE_MUSTANG_TIGERSTRIPEDBAY","A_C_HORSE_THOROUGHBRED_REVERSEDAPPLEBLACK"};
inline constexpr std::array<std::string_view,6> kJohn{
    "A_C_HORSE_JOHN_ENDLESSSUMMER","A_C_HORSE_BUELL_WARVETS",
    "A_C_HORSE_THOROUGHBRED_BLOODBAY","A_C_HORSE_SHIRE_RAVENBLACK",
    "A_C_HORSE_MUSTANG_TIGERSTRIPEDBAY","A_C_HORSE_THOROUGHBRED_REVERSEDAPPLEBLACK"};
template<class Container> inline bool Contains(const Container& names,std::string_view model) {
    for(const auto& name:names)if(name==model)return true;return false;
}
inline bool Common(std::string_view model) {
    for(const auto reserved:kArthur)if(horse_identity::SameCoat(model,reserved))return false;
    // Gang variants are available coats too. The selector removes the current
    // host's own coat aliases from the 40% category, not every other owner's.
    return !model.starts_with("A_C_HORSE_MURFREEBROOD_") &&
        model!="A_C_HORSE_EAGLEFLIES" && model!="A_C_HORSE_WINTER02_01";   // A story horse, not a coat.
}
// A named horse whose owner hosts nothing here, which a breed race may lend
// to whoever hosts it: the Count is Dutch's, and the Arabian race's host rides
// him (the user, 2026-09-21). Also eligible in the ordinary public pool.
// Ennis is Sean's, and the Standardbred race's host rides him until Sean hosts it himself.
inline bool Lent(std::string_view model) {return model=="A_C_HORSE_GANG_DUTCH" || model=="A_C_HORSE_GANG_SEAN";}
// Their own horse, from the host table: a row per host instead of a case per host, because adding a
// host meant finding every switch that knew about hosts and four were added this month.
inline bool OwnMount(characters::Id id,std::string_view model) {
    const auto* profile=hosts::Find(id);
    return profile && Contains(profile->own_mounts,model);
}
// Bill's other category is deliberately limited to these four actual breeds,
// including their gang variants, not the generic "Draft" handling class.
inline bool BillDraft(std::string_view model) {
    const auto breed=horse_identity::Breed(model);
    return Common(model) && (breed=="ARDENNES" || breed=="SHIRE" ||
        breed=="SUFFOLKPUNCH" || breed=="BELGIAN");
}
// This is a MEMBERSHIP filter on the independently runtime/SP-validated catalog,
// never a replacement for asset, label, rank, instance and restoration checks.
inline bool Eligible(characters::Id id,std::string_view model) {
    if(id==characters::Id::Arthur)return Contains(kArthur,model);
    if(id==characters::Id::John)return Contains(kJohn,model);
    for(const auto reserved:kArthur)if(horse_identity::SameCoat(model,reserved))return false;
    if(id==characters::Id::Bill)return OwnMount(id,model) || BillDraft(model);
    return Common(model) || OwnMount(id,model);
}
inline constexpr int kOwnHorseChancePercent=60;
struct Pool {
    std::vector<fixed_horses::Choice> choices;
    std::size_t own_count=0,other_count=0;
    int own_percent=kOwnHorseChancePercent;
    const char* group="none";
};
// Pick the category BEFORE a model. Otherwise two story mounts are diluted by
// dozens of ordinary coats. Own mounts remain equally likely, even if ridden
// last time: excluding Karen's only own horse would defeat her category odds.
// Availability/labels/ranks have already been audited by the caller. Arthur and
// John still use ONLY their six approved models; Arthur's six are equally likely.
template<class Random> Pool ChoosePool(characters::Id id,
    const std::vector<fixed_horses::Choice>& catalog,Hash previous,int percent,Random random) {
    Pool result;result.own_percent=std::clamp(percent,20,100);
    std::vector<fixed_horses::Choice> own,other;
    std::set<std::string> own_coats,other_coats;
    for(const auto& choice:catalog) {
        if(!choice.raw.Valid() || !Eligible(id,choice.name))continue;
        const bool signature=OwnMount(id,choice.name);
        if((signature?own_coats:other_coats).insert(horse_identity::Key(choice.name)).second)
            (signature?own:other).push_back(choice);
    }
    std::erase_if(other,[&](const auto& c){return own_coats.contains(horse_identity::Key(c.name));});
    result.own_count=own.size();result.other_count=other.size();
    if(id==characters::Id::Arthur) {
        // The six camp mounts are peers, not a 60% Boadicea / 40% remainder
        // split. A duel can still explicitly request the horse selected there.
        result.choices=std::move(own);result.choices.insert(result.choices.end(),other.begin(),other.end());
        result.group="dedicated-six";return result;
    }
    bool use_own=!own.empty();
    if(!own.empty() && !other.empty() && result.own_percent<100) {
        const int roll=random(0,100);
        if(roll<0 || roll>=100)return result;
        use_own=roll<result.own_percent;
    }
    if(use_own) {result.choices=std::move(own);result.group="own";}
    else {
        result.choices=std::move(other);
        result.group=id==characters::Id::Arthur || id==characters::Id::John?"dedicated":
            id==characters::Id::Bill?"draft":"common";
        fixed_horses::AvoidPrevious(result.choices,previous);
    }
    return result;
}
}
