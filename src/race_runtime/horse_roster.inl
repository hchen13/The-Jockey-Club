// Private horse roster implementation; included by race_system.cpp.
// The runtime horse catalog is built after the save loads. Until it is there,
// no selection can be attempted - but nothing has been cancelled either, so
// callers must wait rather than report a failure to the player.
bool HorseCatalogReady()
{
    RefreshHorseCatalog();
    return g_catalog_ready && !g_horse_catalog.empty();
}
// Set whenever PrepareHorsePlan fails on something that simply has not
// finished yet. Between races the previous host's rank lease, its field and its
// wager all unwind over several seconds; none of that is a cancelled race, and
// reporting it as one told the player their race was cancelled once a frame.
std::string g_plan_wait;
bool PrepareHorsePlan()
{
    g_plan_wait.clear();
    const auto wait=[](const char* why){g_plan_wait=why;return false;};
    if (!HorseCatalogReady()) return wait("horse catalog still building");
    if (g_rank_lease.pending) return wait("previous rank lease still releasing");
    if (!g_extra_rivals.empty()) return wait("previous field still retiring");
    if (!g_fixed_wager.CanBegin()) return wait("previous wager still settling");
    const auto rule=opponent_difficulty::Read(g_ini_path,OpponentDifficultyIndex());
    if (CurrentRules().Hosted()) {
        const auto& host=FixedProfile();
        // The protagonist's twin is refused as a debug swap, but a route that
        // names the player's own character for this chapter gets it.
        if(!hosts::AllowedPlayer(host,ENTITY::GET_ENTITY_MODEL(PLAYER::PLAYER_PED_ID()),
            (!g_test_host_override.empty() && g_test_listener!=dialogue_test::Listener::Auto) ||
            &host==hosts::Find(FixedRouteDefinition(g_variant).host)))return false;
        g_difficulty_tier=0;
    } else {
        const auto original=horse_attributes::ReadModelDefaults(MISC::GET_HASH_KEY(g_rival->horse_model));
        if (!original.Valid()) return false;
        const int roll=MISC::GET_RANDOM_INT_IN_RANGE(0,100);
        g_difficulty_tier=roll<45?0:(roll<80?1:2);
    }
    auto target=DifficultyTarget(rule,true);
    if(target.speed<0 || target.speed>9 || (rule.acceleration.enabled && target.acceleration<0))return false;
    // Fixed bounty races intentionally decouple appearance from difficulty:
    // every already-validated ordinary breed/coat is eligible at every tier.
    // Original encounters retain their existing nearest-rank selection policy.
    auto choices=g_horse_catalog;
    const characters::Id host_id=CurrentRules().Hosted()?FixedProfile().id:characters::Id::OtherRival;
    // The camp asked for one of Arthur's own horses by name: he rides the one
    // the player locked on to, and nothing else is drawn for.
    if(!g_asked_host_horse.empty()) {
        const std::string asked=g_asked_host_horse;g_asked_host_horse.clear();
        auto only=choices;
        std::erase_if(only,[&](const auto& choice){return !choice.raw.Valid() || asked!=choice.name ||
            !host_horses::Eligible(host_id,choice.name) ||
            (CurrentRules().breed && !host_horses::Contains(field_race::ReservedHostModels(CurrentBreed(),host_id),asked));});
        if(only.empty())Log("the horse asked for ("+asked+") is not in the validated catalogue; drawing as usual");
        else {
            const auto& choice=only.front();
            g_horse_model=choice.model;g_fixed_selected_horse_name=choice.name;
            g_planned_natural=choice.raw;
            g_planned_target=opponent_difficulty::ResolveNaturalAcceleration(target,
                CurrentRules().Hosted()?&FixedProfile():nullptr,choice.raw,
                [](int low,int high){return MISC::GET_RANDOM_INT_IN_RANGE(low,high);},CurrentRules().Field());
            if(!g_planned_target.Valid())return false;
            g_planned_difficulty=rule;g_horse_plan_ready=true;
            if(CurrentRules().Field() && !PrepareExtraPlan(rule))return false;
            g_race_player=PLAYER::PLAYER_PED_ID();
            Log("horse plan model="+g_fixed_selected_horse_name+" asked for at the camp");
            return true;
        }
    }
    // A field race sets the host's mount itself (their own named horse or the
    // breed's host coat), so the stable lists that bound a hosted race's draw
    // do not apply there.
    std::erase_if(choices,[&](const auto& choice){return !choice.raw.Valid() ||
        !(CurrentRules().breed?field_race::HostMayRide(host_id,choice.name):host_horses::Eligible(host_id,choice.name));});
    host_horses::Pool horse_pool;
    if(CurrentRules().breed) {
        // The host of a field race rides a mount the field never spawns, so the
        // one horse the player cannot get is the one setting the pace - but it
        // has to be the right BREED, which their model name does not say.
        const auto& breed=CurrentBreed();
        const char* how="none";
        const auto host_horse=field_race::ChooseHostHorse(breed,host_id,
            [&](std::string_view m){return std::any_of(choices.begin(),choices.end(),[&](const auto& c){return std::string_view(c.name)==m;});},
            [](int n){return MISC::GET_RANDOM_INT_IN_RANGE(0,n);},how);
        if(host_horse.empty()) {
            Log("field host mount: no horse of breed "+std::string(breed.name)+" that "+FixedProfile().name+" may ride is in the validated catalog");
            return false;
        }
        std::erase_if(choices,[&](const auto& choice){return std::string_view(choice.name)!=host_horse;});
        horse_pool.group="fixed-host-mount";
        Log(std::string("field host mount=")+std::string(host_horse)+" ("+how+") race_breed="+std::string(breed.name)+
            " game_label="+field_race::BreedLabel(host_horse));
    } else if(CurrentRules().Hosted()) {
        const int own_percent=opponent_difficulty::OwnHorsePercent(g_ini_path,host_horses::kOwnHorseChancePercent);
        horse_pool=host_horses::ChoosePool(FixedProfile().id,choices,PreviousFixedHorse(),own_percent,
            [](int low,int high){return MISC::GET_RANDOM_INT_IN_RANGE(low,high);});
        choices=horse_pool.choices;
    } else {
        choices=race_horses::Nearest(choices,target);
        fixed_horses::AvoidPrevious(choices,PreviousFixedHorse());
    }
    if (choices.empty()) return false;
    const int draw=choices.size()==1?0:MISC::GET_RANDOM_INT_IN_RANGE(0,static_cast<int>(choices.size()));
    if (draw<0 || static_cast<std::size_t>(draw)>=choices.size()) return false;
    const auto& choice=choices[draw];
    g_horse_model=choice.model; g_fixed_selected_horse_name=choice.name;
    g_planned_natural=choice.raw;
    g_planned_target=opponent_difficulty::ResolveNaturalAcceleration(target,
        CurrentRules().Hosted()?&FixedProfile():nullptr,choice.raw,
        [](int low,int high){return MISC::GET_RANDOM_INT_IN_RANGE(low,high);},CurrentRules().Field());
    if(!g_planned_target.Valid())return false;
    g_planned_difficulty=rule;
    g_horse_plan_ready=true;
    if(CurrentRules().Field() && !PrepareExtraPlan(rule))return false;
    g_race_player=PLAYER::PLAYER_PED_ID();
    Log("horse plan model="+g_fixed_selected_horse_name+" raw_natural="+horse_attributes::Pair(g_planned_natural)+
        " raw_target="+horse_attributes::Pair(g_planned_target)+
        (g_planned_natural==g_planned_target?" natural match; no writes":" temporary instance rank lease")+
        (CurrentRules().Hosted()?" selection=all-validated-coats pool=":" selection=nearest-ranks pool=")+
        std::to_string(choices.size())+" difficulty_profile="+opponent_difficulty::At(OpponentDifficultyIndex()).key+
        " ranges="+opponent_difficulty::Encode(rule)+(CurrentRules().Hosted()?
        " mount_group="+std::string(horse_pool.group)+" own_pool="+std::to_string(horse_pool.own_count)+
        " other_pool="+std::to_string(horse_pool.other_count)+" own_chance_percent="+std::to_string(horse_pool.own_percent):""));
    return true;
}

bool SelectDifficultyTier()
{
    const auto attributes = horse_attributes::Read(g_opponent_horse);
    const auto base = horse_attributes::ResolveNpcBase(attributes, g_attribute_backup.Find(attributes.model));
    Log("opponent attributes horse=" + std::to_string(g_opponent_horse) + " model=" + std::to_string(attributes.model) +
        " raw modelDefault=" + horse_attributes::Pair(attributes.model_default) +
        " entityCurrent=" + horse_attributes::Pair(attributes.current) +
        " entityBase=" + horse_attributes::Pair(attributes.entity_base) +
        " bonus=" + horse_attributes::Pair(attributes.bonus) + " source=" + base.source +
        (base.mismatch ? " FILE/RUNTIME MISMATCH" : ""));
    if (!base.base.Valid()) {
        Log("opponent base attributes unresolved; refusing to invent 1/1 ranks"); return false;
    }
    if (!g_horse_plan_ready || attributes.model!=g_horse_model || attributes.model_default!=g_planned_natural) {
        Log("horse plan absent or selected runtime metadata changed; rescan before next selection");
        g_catalog_ready=false; return false;
    }
    PreviousFixedHorse()=g_horse_model;
    g_opponent_base_speed_level = horse_attributes::DisplayLevel(base.base.speed);
    g_opponent_base_accel_level = horse_attributes::DisplayLevel(base.base.acceleration);
    g_opponent_speed_level=horse_attributes::DisplayLevel(g_planned_target.speed);
    g_opponent_accel_level=horse_attributes::DisplayLevel(g_planned_target.acceleration);
    const DifficultyTier& tier = kDifficultyTiers[static_cast<std::size_t>(g_difficulty_tier)];
    if (CurrentRules().wagered) {
        const auto& route=SelectedFixedRoute();
        std::vector<int> speeds{g_opponent_speed_level};
        if(CurrentRules().Field())
            for(const auto& e:g_extra_rivals)speeds.push_back(e.target.speed+1);
        // g_course is the frozen whole instance (including every selected lap),
        // never a single lap or the distance remaining from the player's ped.
        const bool recorded=g_course.points.size()>=2;
        const double metres=recorded?g_course.Length():route.pricing_distance_metres;
        const bool automatic=route.reward_multiplier==distance_pricing::kAutomatic ||
            route.stake_multiplier==distance_pricing::kAutomatic;
        const double reference_metres=automatic?g_price_reference_metres:0;
        const double reward_factor=distance_pricing::Multiplier(route.reward_multiplier,metres,reference_metres);
        const double stake_factor=distance_pricing::Multiplier(route.stake_multiplier,metres,reference_metres);
        const auto reward=route.reward_multiplier==distance_pricing::kAutomatic?
            wager::QuoteRosterProportional(speeds,reward_factor):wager::QuoteRoster(speeds,reward_factor);
        const auto stake=route.stake_multiplier==distance_pricing::kAutomatic?
            wager::QuoteRosterProportional(speeds,stake_factor):wager::QuoteRoster(speeds,stake_factor);
        g_reward_cents=reward.cents;
        g_fixed_stake_cents=stake.cents;
        if(!reward.valid || !stake.valid) {
            Log("fixed price rejected; no invitation or debit; course_m="+std::to_string(metres)+
                " reference_m="+std::to_string(reference_metres)+" reward_factor="+std::to_string(reward_factor)+
                " stake_factor="+std::to_string(stake_factor));return false;
        }
        if(!g_reward_cents && !g_fixed_stake_cents)
            Log("entire roster has zero prize; practice available, paid race disabled");
        Log("fixed price snapshot stake_cents="+std::to_string(g_fixed_stake_cents)+
            " net_reward_cents="+std::to_string(g_reward_cents)+" reward_multiplier="+std::to_string(reward_factor)+
            " stake_multiplier="+std::to_string(stake_factor)+
            " site="+std::to_string(g_variant->id)+" course_m="+std::to_string(metres)+
            " distance_source="+(recorded?"frozen_course":"surveyed_reference")+
            " laps="+std::to_string(g_laps.Frozen()?g_laps.plan.Total():1)+
            " reference_m="+std::to_string(reference_metres)+
            " reward_policy="+(route.reward_multiplier==distance_pricing::kAutomatic?"distance":"fixed")+
            " stake_policy="+(route.stake_multiplier==distance_pricing::kAutomatic?"distance":"fixed")+
            " companion_count="+std::to_string(speeds.size()-1)+" companion_pricing=mean");
    } else g_reward_cents=tier.reward_cents;
    Log(std::string("difficulty=") + tier.name + " pending instance attributes" +
        " horse ranks speed=" + std::to_string(g_opponent_base_speed_level) + "->" +
        std::to_string(g_opponent_speed_level) + " acceleration=" +
        std::to_string(g_opponent_base_accel_level) + "->" + std::to_string(g_opponent_accel_level) +
        " reward=$" + std::to_string(g_reward_cents / 100));
    return true;
}

using NativeBgmAudio=bgm::audio::WindowsBridge;
using NativeBgmFadeAudio=bgm::audio::fade::WindowsBridge;
using NativeBgmMusic=bgm::native_music::WindowsBridge;
struct NativeBgmOutput {
    static bgm::native_music::output::Result Inspect(const bgm::native_music::Token& token) {
        const auto bridge=NativeBgmMusic::View();
        if(bridge.calls.Supported())return bgm::native_music::output::Inspect(bridge.memory,bridge.base,token);
        bgm::native_music::output::Result result;result.reason="unsupported_engine";return result;
    }
};
