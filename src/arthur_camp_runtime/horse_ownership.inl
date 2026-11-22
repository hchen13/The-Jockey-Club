// Exact resident identities and read-only player ownership evidence.
bool SameCampHorse(const Horse& horse) {
    return world::Still({horse.ped,horse.model,horse.address});
}
bool PreservedHorseWorldCurrent() {
    return !g_horse_world_invalidated && g_horse_world && horse_ledger::runtime::ObservedTimeline()==g_horse_world;
}
void SetCampHorseFleeBlock(Horse& horse,bool blocked) {
    if(horse.flee_checked==blocked)return;
    // The native prompt override belongs to this exact resident, never to a
    // recycled handle or a horse in a newly loaded world. Type 33 is the
    // horse FLEE prompt; mode 0 blocks only that prompt, leaving PAT intact.
    if(!PreservedHorseWorldCurrent() || !SameCampHorse(horse)){
        horse.flee_checked=horse.flee_blocked=false;horse.flee_recheck_after={};return;
    }
    const Player player=PLAYER::PLAYER_ID();
    const bool already=PLAYER::_GET_PLAYER_UI_PROMPT_FOR_PED_IS_ENABLED(player,horse.ped,33,0)!=0;
    if(blocked) {
        // Other scripts may already own this block. Only restore an override
        // we actually introduced for this resident.
        horse.flee_checked=true;horse.flee_blocked=!already;
        if(!already)PLAYER::_MODIFY_PLAYER_UI_PROMPT_FOR_PED(player,horse.ped,33,0,true);
    }else {
        if(horse.flee_blocked && already)PLAYER::_MODIFY_PLAYER_UI_PROMPT_FOR_PED(player,horse.ped,33,0,false);
        horse.flee_checked=horse.flee_blocked=false;
        horse.flee_recheck_after={};
    }
}
void RefreshCampHorseFleeBlock(Horse& horse,const Clock::time_point& now) {
    if(!horse.flee_checked)SetCampHorseFleeBlock(horse,true);
    if(!horse.flee_checked || now<horse.flee_recheck_after)return;
    horse.flee_recheck_after=now+250ms;
    if(!PreservedHorseWorldCurrent() || !SameCampHorse(horse)){
        horse.flee_checked=horse.flee_blocked=false;horse.flee_recheck_after={};return;
    }
    const Player player=PLAYER::PLAYER_ID();
    // Grooming or another native interaction may rebuild its prompt state.
    // Take ownership only if our recheck actually has to restore the block.
    if(!PLAYER::_GET_PLAYER_UI_PROMPT_FOR_PED_IS_ENABLED(player,horse.ped,33,0)){
        PLAYER::_MODIFY_PLAYER_UI_PROMPT_FOR_PED(player,horse.ped,33,0,true);
        horse.flee_blocked=true;
    }
}
void DiscardPreservedHorses() {
    for(const auto& horse:g_preserved_horses)firearm_guard::Discard(horse.id.ped);
    g_preserved_horses.clear();
}
bool SyncPreservedHorseWorld() {
    // This is a pure read of the world already observed before camp Tick.
    // Do not call Timeline(): that function pumps the save observer.
    const auto world=horse_ledger::runtime::ObservedTimeline();
    const bool changed=g_horse_world && world!=g_horse_world;
    if((changed || !world) && g_phase!=Phase::Idle) {
        // Race Tick runs before the scene's Tick. Invalidate its old leases
        // now, without looking up any entity; latch the scene cleanup for Tick.
        firearm_guard::Discard(g_arthur);firearm_guard::Discard(g_mary.ped);
        for(const auto& horse:g_horses)firearm_guard::Discard(horse.ped);
        g_horse_world_invalidated=true;
    }
    if(!world || world!=g_horse_world)DiscardPreservedHorses();
    g_horse_world=world;
    return changed || g_horse_world_invalidated;
}
void PreserveCampHorse(const Horse& horse) {
    // Check the world before even asking whether an old handle exists.
    if(!PreservedHorseWorldCurrent() || !SameCampHorse(horse))return;
    const auto at=ENTITY::GET_ENTITY_COORDS(horse.ped,true,false);
    const Point position{at.x,at.y,at.z};
    const firearm_guard::Identity id{horse.ped,horse.model,horse.address};
    for(auto& prior:g_preserved_horses)if(prior.world==g_horse_world && prior.id==id){prior.at=position;return;}
    g_preserved_horses.push_back({id,position,g_horse_world});
}
struct PlayerHorses {
    Ped player=PLAYER::PLAYER_PED_ID();
    Player id=PLAYER::PLAYER_ID();
    Ped mount=player?PED::GET_MOUNT(player):0;
    Ped active=PLAYER::_GET_ACTIVE_HORSE_FOR_PLAYER(id);
    Ped saddle=PLAYER::_GET_SADDLE_HORSE_FOR_PLAYER(id);
    Ped temporary=PLAYER::_GET_TEMP_PLAYER_HORSE(id);
    // camp_horseshoeoverlook::func_805 assigns active slot0 with
    // 0x8FBF9EDB378CCB8C and temporary slot1 with SET_PED_AS_TEMP_PLAYER_HORSE;
    // it reads them with 0x46FA0AE18F4C7FA9 and 0xD3F7445CEA2E5035 respectively.
    // GET_LAST_MOUNT is history, so it is deliberately not ownership evidence.
    const char* Binding(Ped horse) const {
        if(!horse)return nullptr;
        if(horse==mount)return "player_mount";
        if(horse==active)return "player_active";
        if(horse==saddle)return "player_saddle";
        if(horse==temporary)return "player_temporary";
        if(player && PED::_GET_ACTIVE_ANIMAL_OWNER(horse)==player)return "player_animal_owner";
        return nullptr;
    }
};
struct CampHorseOwnership {
    individual_identity::Ownership state=individual_identity::Ownership::Unknown;
    std::string evidence;
    individual_identity::OwnershipIssue issue=individual_identity::OwnershipIssue::None;
};
CampHorseOwnership PlayerClaim(const Horse& horse,const PlayerHorses& player) {
    using individual_identity::Ownership;
    if(const char* binding=player.Binding(horse.ped))return {Ownership::Owned,binding};
    // The ten-second check, teardown and actual race/first-hide transactions
    // call this fallback; ordinary offer display uses only the cached state.
    // Metadata failure is Unknown. Only the probe's explicit stable-table
    // exclusion is NotOwned; no control flow depends on diagnostic wording.
    const auto probe=individual_identity::ProbeOwnership(horse.ped);
    if(probe.state==Ownership::NotOwned)return {Ownership::NotOwned,"stable_player_table_excludes_entity"};
    if(probe.state!=Ownership::Owned)return {Ownership::Unknown,
        "probe_issue="+std::to_string(static_cast<int>(probe.issue))+" "+probe.detail,probe.issue};
    const auto& saved=probe.identity;
    if(!saved || saved->model!=horse.model)return {Ownership::Unknown,"owned_probe_identity_mismatch"};
    char guid[48];const auto& w=saved->guid.words;
    std::snprintf(guid,sizeof guid,"%08x:%08x:%08x:%08x",w[0],w[1],w[2],w[3]);
    return {Ownership::Owned,"player_saved_slot="+std::to_string(saved->slot)+" guid="+guid};
}
std::optional<CampHorseOwnership> CheckedPlayerClaim(const Horse& horse,const PlayerHorses& player) {
    if(!PreservedHorseWorldCurrent() || !SameCampHorse(horse)){firearm_guard::Discard(horse.ped);return {};}
    const auto claim=PlayerClaim(horse,player);
    if(!PreservedHorseWorldCurrent() || !SameCampHorse(horse)){firearm_guard::Discard(horse.ped);return {};}
    return claim;
}
bool AvailableCampHorse(const Horse& horse,const PlayerHorses& player) {
    // The game may register a brushed horse in special slots 5/6 without
    // making it the player's temporary or active horse. This ambiguous slot
    // permits a race offer only; it is never evidence for deleting the horse.
    const bool offerable=horse.ownership==individual_identity::Ownership::NotOwned ||
        (horse.ownership==individual_identity::Ownership::Unknown &&
            horse.ownership_issue==individual_identity::OwnershipIssue::SpecialSlot);
    return offerable && SameCampHorse(horse) &&
        !ENTITY::IS_ENTITY_DEAD(horse.ped) && !player.Binding(horse.ped);
}
std::string CampHorseLabel(const Horse& horse) {
    return "horse slot="+std::to_string(horse.slot+1)+" model="+
        (horse.slot<g_layout.horse_models.size()?g_layout.horse_models[horse.slot]:std::to_string(horse.model));
}
void LogHorseState(Horse& horse,const std::string& state,const std::string& detail={}) {
    if(horse.state==state)return;
    horse.state=state;
    Log(CampHorseLabel(horse)+" state="+state+" ped="+std::to_string(horse.ped)+(detail.empty()?"":" "+detail));
}
// Implemented after race offer state is declared; never use cached ownership
// as authorization for an actual race start or the first hidden stand-down.
bool RecheckCampHorseAction(Horse& horse,const char* action);
