// Private read-only native diagnostics and candidate-map policy. Never spawns a
// native encounter, mutates script globals, advances a reward or changes a roll.
namespace native_candidates {
using Snapshot=native_encounter::Snapshot;
int RegionAt(const Vector3& point);
struct Data {
    Snapshot state;native_encounter::Admission admission;
    native_encounter::ReasonReport reasons;
    std::optional<int> reason_flags,reason_state,reason_carried,reason_stowed;
    native_encounter::Truth reason_lasso;
    std::optional<int> reason_volume;
    std::array<int,11> point_regions{-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1};
};
Data Read() {
    const native_reward::NativeApi api;
    if(!api.Supported())return {};
    const native_encounter::Reader<native_reward::NativeApi> read{api};
    Data data{read.Capture(),native_encounter::ReadAdmission(read,MISC::GET_GAME_TIMER())};
    data.reasons=native_encounter::ReadReasons(read,MISC::GET_GAME_TIMER());
    data.reason_flags=read.Int(native_encounter::kConfig+1);data.reason_state=read.Int(native_encounter::kEventState+5);
    data.reason_carried=read.Int(1935630+32);data.reason_stowed=read.Int(1935630+33);data.reason_lasso=read.Bool(1935630+30);
    const auto place=read.Int(1894899+2);
    if(place && *place>=0 && *place<=150) {
        const auto type=read.Element(1888801,*place,35,20,151);
        if(type && *type==8) {
            const int slot=*place==11?0:*place==76?1:*place==15?3:*place==128?2:-1;
            if(slot>=0)data.reason_volume=read.Element(1392194,slot,10,5,4);
        }
    }
    for(const auto& point:kOriginalRaceVariants)data.point_regions[point.id]=RegionAt(point.encounter_anchor);
    return data;
}
using Probe=Data(*)();
Probe g_probe=Read;
struct Cache {Data data;Clock::time_point next{};bool ready=false;} g_cache;
void Reset(){g_cache={};}
const Data& Current() {
    if(DLC::GET_IS_LOADING_SCREEN_ACTIVE()){Reset();return g_cache.data;}
    const auto now=Clock::now();
    // At most four snapshots per game second, only while this page or the
    // unawarded native-marker path needs it. No disk, scans or entity creation.
    if(!g_cache.ready || now>=g_cache.next) {
        g_cache.data=g_probe();g_cache.next=now+std::chrono::milliseconds(250);g_cache.ready=true;
    }
    return g_cache.data;
}
NativeRaceRewardState Display(native_encounter::Truth value) {
    return !value?NativeRaceRewardState::Unknown:*value?NativeRaceRewardState::Yes:NativeRaceRewardState::No;
}
native_encounter::Truth NativeSafety(const Data& data);
native_encounter::ReasonReport Reasons(const Data& data);
native_encounter::Truth OtherConditions(const Data& data) {
    native_encounter::Truth region;
    if(data.state.player_region && *data.state.player_region>=0 && *data.state.player_region<17)
        region=data.state.regions[*data.state.player_region];
    bool unknown=false;
    for(const auto test:{data.state.history_ok,data.state.common_ok,region,NativeSafety(data),
        data.state.used?native_encounter::Truth{true}:native_encounter::Truth{}}) {
        if(test && !*test)return false;
        if(!test)unknown=true;
    }
    return unknown?native_encounter::Truth{}:native_encounter::Truth{true};
}
NativeEncounterConditions Conditions() {
    NativeEncounterConditions out;
    if(DLC::GET_IS_LOADING_SCREEN_ACTIVE()){Reset();return out;}

    const auto& data=Current().state;
    if(!native_encounter::Yes(data.supported))return out;
    out.rewards=NativeRaceRewards();
    out.other_native_conditions=Display(OtherConditions(Current()));
    const auto reasons=Reasons(Current());
    out.native_block_reasons=reasons.mask;out.native_reasons_unknown=reasons.unknown;
    out.bonding=Display(data.bonding);out.race_cooldown_steps=data.cooldown_steps;
    if(data.shared_deadline)out.shared_cooldown_seconds=native_encounter::SharedSeconds(*data.shared_deadline,MISC::GET_GAME_TIMER());
    out.daytime=Display(native_encounter::Daytime(CLOCK::GET_CLOCK_HOURS()));
    const Ped player=PLAYER::PLAYER_PED_ID();
    if(!player || !ENTITY::DOES_ENTITY_EXIST(player))return out;
    const bool mounted=PED::IS_PED_ON_MOUNT(player);
    out.mounted=Display(mounted);
    const Ped horse=mounted?PED::GET_MOUNT(player):0;
    if(horse && ENTITY::DOES_ENTITY_EXIST(horse))
        out.horse_attributes=Display(native_encounter::Attributes(ATTRIBUTE::GET_ATTRIBUTE_RANK(horse,4),ATTRIBUTE::GET_ATTRIBUTE_RANK(horse,5)));
    return out;
}
native_encounter::Truth NativeSafety(const Data& data) {
    if(!data.admission.allowed)return std::nullopt;
    if(!*data.admission.allowed)return false;
    const Ped player=PLAYER::PLAYER_PED_ID();
    if(!player || !ENTITY::DOES_ENTITY_EXIST(player))return std::nullopt;
    if(PED::IS_PED_INJURED(player) ||
        MISC::GET_MISSION_FLAG() || NETWORK::NETWORK_IS_SESSION_ACTIVE() ||
        PLAYER::IS_PLAYER_RIDING_TRAIN(PLAYER::PLAYER_ID()))return false;
    // medium_update func_256 requires >=5 runtime seconds since this LAW event.
    const int now=MISC::GET_GAME_TIMER(),last=LAW::_0x7FC667F6DDFBCDCC(PLAYER::PLAYER_ID());
    if(now<0 || last<0)return std::nullopt;
    if(static_cast<std::int64_t>(now)-last<5000)return false;
    if(data.admission.restricted_volume) {
        const int volume=*data.admission.restricted_volume;
        if(VOLUME::DOES_VOLUME_EXIST(volume) &&
            VOLUME::IS_POINT_IN_VOLUME(volume,ENTITY::GET_ENTITY_COORDS(player,true,false)))return false;
    }
    const auto& state=data.state;
    if(!state.ignore_human_cargo) {
        if(!state.carried || !state.stowed || !state.lasso_active)return std::nullopt;
        const auto human=[](int ped){return ENTITY::DOES_ENTITY_EXIST(ped) && PED::IS_PED_HUMAN(ped);};
        if(human(*state.carried))return false;
        if(human(*state.stowed) && (PED::IS_PED_HOGTIED(*state.stowed) || ENTITY::IS_ENTITY_DEAD(*state.stowed)))return false;
        if(*state.lasso_active && human(PED::_GET_LASSO_TARGET(player)))return false;
    }
    return true;
}
native_encounter::ReasonReport Reasons(const Data& data) {
    using namespace native_encounter;
    auto out=data.reasons;
    const Ped player=PLAYER::PLAYER_PED_ID();
    if(!player || !ENTITY::DOES_ENTITY_EXIST(player)){out.unknown=true;return out;}
    out.Add(BlockReason::PlayerStateRestricted, PED::IS_PED_INJURED(player) ||
        MISC::GET_MISSION_FLAG() || NETWORK::NETWORK_IS_SESSION_ACTIVE() ||
        PLAYER::IS_PLAYER_RIDING_TRAIN(PLAYER::PLAYER_ID()));
    const int now=MISC::GET_GAME_TIMER(),last=LAW::_0x7FC667F6DDFBCDCC(PLAYER::PLAYER_ID());
    out.Add(BlockReason::LawConflictRestricted,now<0 || last<0?Truth{}:
        Truth{static_cast<std::int64_t>(now)-last<5000});
    if(data.reason_volume) {
        const int volume=*data.reason_volume;
        out.Add(BlockReason::LocationRestricted,VOLUME::DOES_VOLUME_EXIST(volume) &&
            VOLUME::IS_POINT_IN_VOLUME(volume,ENTITY::GET_ENTITY_COORDS(player,true,false)));
    }
    // Independently read cargo even when another native condition blocked first.
    const auto flags=data.reason_flags,state=data.reason_state;
    if(!flags || !state)out.unknown=true;
    else if(!(*flags&(1<<20)) && *state!=1) {
        const auto carried=data.reason_carried,stowed=data.reason_stowed;
        const auto lasso=data.reason_lasso;
        const auto human=[](int ped){return ENTITY::DOES_ENTITY_EXIST(ped) && PED::IS_PED_HUMAN(ped);};
        out.Add(BlockReason::HumanCargoRestricted,carried?Truth{human(*carried)}:Truth{});
        out.Add(BlockReason::HumanCargoRestricted,stowed?Truth{human(*stowed) &&
            (PED::IS_PED_HOGTIED(*stowed) || ENTITY::IS_ENTITY_DEAD(*stowed))}:Truth{});
        out.Add(BlockReason::HumanCargoRestricted,lasso?Truth{*lasso && human(PED::_GET_LASSO_TARGET(player))}:Truth{});
    }
    return out;
}
int RegionAt(const Vector3& point) {
    const Hash zone=ZONE::_GET_MAP_ZONE_AT_COORDS(point,10);
    const std::array<const char*,17> names{"BAYOUNWA","BIGVALLEY","BLUEWATERMARSH","CUMBERLAND","GREATPLAINS",nullptr,
        "GRIZZLIESEAST","GRIZZLIESWEST","GUARMAD","HEARTLANDS","ROANOKE","SCARLETTMEADOWS","TALLTREES",
        "GAPTOOTHRIDGE","RIOBRAVO","CHOLLASPRINGS","HENNIGANSSTEAD"};
    for(std::size_t i=0;i<names.size();++i)if(names[i] && zone==MISC::GET_HASH_KEY(names[i]))return static_cast<int>(i);
    return -1;
}
std::array<bool,11> VisiblePoints() {
    std::array<bool,11> visible{};
    const auto conditions=Conditions();
    // An unknown first flag must not be described as an unawarded native save.
    if(conditions.rewards.speed!=NativeRaceRewardState::No || conditions.rewards.bonding==NativeRaceRewardState::Unknown ||
        conditions.daytime!=NativeRaceRewardState::Yes || conditions.mounted!=NativeRaceRewardState::Yes ||
        conditions.bonding!=NativeRaceRewardState::Yes || conditions.horse_attributes!=NativeRaceRewardState::Yes ||
        conditions.race_cooldown_steps!=0 || conditions.shared_cooldown_seconds!=0)return visible;
    const auto& data=Current();
    // Native's shared deadline comparison is strict: an equal zero-second
    // display does not authorize the marker until the following runtime tick.
    if(!data.state.shared_deadline || *data.state.shared_deadline>=MISC::GET_GAME_TIMER() || !native_encounter::Yes(NativeSafety(data)))return visible;
    for(const auto& point:kOriginalRaceVariants)
        visible[point.id]=native_encounter::Candidate(data.state,point.id,data.point_regions[point.id],true);
    return visible;
}
} // namespace native_candidates
