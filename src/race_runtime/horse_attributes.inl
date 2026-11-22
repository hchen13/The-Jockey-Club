// Private horse attributes implementation; included by race_system.cpp.
race_pace::Dynamics ReadRacePaceDynamics(Ped horse=0)
{
    if(!horse)horse=g_opponent_horse;
    auto dynamics=race_pace::ForAttributes(ATTRIBUTE::GET_ATTRIBUTE_RANK(horse,5)+1,
        ATTRIBUTE::GET_ATTRIBUTE_RANK(horse,6)+1);
    auto read=[&](const char* key,const char* fallback,float minimum,float maximum,float original) {
        char value[64]{};GetPrivateProfileStringA("RaceAI",key,fallback,value,sizeof(value),g_ini_path.c_str());
        try {
            std::size_t used=0;const float f=std::stof(value,&used);
            if(used==std::string(value).size() && std::isfinite(f) && f>=minimum && f<=maximum)return f;
        } catch(...) {}
        Log(std::string("invalid RaceAI.")+key+"; default retained");return original;
    };
    dynamics.lateral=read("CorneringEnvelopeMps2","14",5,30,dynamics.lateral);
    dynamics.braking=read("BrakingEnvelopeMps2","6",2,12,dynamics.braking);
    // The opponent's own cornering from F11 first, then the shared envelope.
    // The field rides its host's race and corners as he is set to.
    if(g_planned_difficulty.cornering>0)dynamics.lateral=static_cast<float>(g_planned_difficulty.cornering);
    return dynamics;
}

race_horses::Identity HorseIdentity(Ped horse)
{
    if (!horse || !ENTITY::DOES_ENTITY_EXIST(horse)) return {};
    return {horse,ENTITY::GET_ENTITY_MODEL(horse),reinterpret_cast<std::uintptr_t>(getScriptHandleBaseAddress(horse))};
}
int ReadBaseRank(Ped horse,int index) { return ATTRIBUTE::GET_ATTRIBUTE_BASE_RANK(horse,index); }
void WriteBaseRank(Ped horse,int index,int rank) { ATTRIBUTE::SET_ATTRIBUTE_BASE_RANK(horse,index,rank); }

// Called only immediately after CREATE_PED for our newly owned race horse.
// Materialise runtime model defaults, not the enhanced race target. A real 0/0
// model remains 0/0; never use this on an existing/player-owned horse.
bool InitialiseNewOpponentRanks()
{
    const auto s=horse_attributes::Read(g_opponent_horse);
    if (!g_horse_plan_ready || s.model!=g_horse_model || s.model_default!=g_planned_natural ||
        !s.model_default.Valid() || PED::GET_MOUNT(PLAYER::PLAYER_PED_ID())==g_opponent_horse) return false;
    if (s.entity_base==horse_attributes::Ranks{0,0} && s.current==horse_attributes::Ranks{0,0} &&
        s.model_default!=horse_attributes::Ranks{0,0}) {
        WriteBaseRank(g_opponent_horse,5,s.model_default.speed);
        WriteBaseRank(g_opponent_horse,6,s.model_default.acceleration);
    }
    const auto actual=horse_attributes::Read(g_opponent_horse);
    const bool ok=actual.model==s.model && actual.entity_base==g_planned_natural &&
        actual.current==g_planned_natural;
    Log(std::string("new horse natural rank readback ")+(ok?"verified":"FAILED")+
        " base="+horse_attributes::Pair(actual.entity_base)+" current="+horse_attributes::Pair(actual.current)+
        " expected="+horse_attributes::Pair(g_planned_natural));
    return ok;
}

bool PlayerHasTakenOpponentHorse()
{
    const Ped player=PLAYER::PLAYER_PED_ID();
    if (!player || !g_opponent_horse || !ENTITY::DOES_ENTITY_EXIST(g_opponent_horse)) return g_opponent_horse_claimed;
    if (PED::GET_MOUNT(player)==g_opponent_horse || PED::_GET_RIDER_OF_MOUNT(g_opponent_horse,true)==player ||
        // Preview ranks now exist before accepting: bonding/ownership can
        // transfer without mounting first. The native info card uses these
        // same two getters to identify the player's main/temporary horse.
        PLAYER::_GET_ACTIVE_HORSE_FOR_PLAYER(PLAYER::PLAYER_ID())==g_opponent_horse ||
        PLAYER::_GET_TEMP_PLAYER_HORSE(PLAYER::PLAYER_ID())==g_opponent_horse ||
        (g_opponent_rider && ENTITY::DOES_ENTITY_EXIST(g_opponent_rider) && PED::GET_PEDS_JACKER(g_opponent_rider)==player))
        g_opponent_horse_claimed=true;
    return g_opponent_horse_claimed;
}
bool RestoreOpponentRanks(const char* reason)
{
    if (!g_rank_lease.pending) { g_restore_requested=false; return true; }
    g_restore_requested=true;
    const bool restored=g_rank_lease.Restore(HorseIdentity,ReadBaseRank,WriteBaseRank);
    if (restored || Clock::now()>=g_restore_log_at) {
        Log(std::string("horse rank lease ")+(restored?"restored/released: ":"RESTORE PENDING: ")+reason);
        g_restore_log_at=Clock::now()+std::chrono::seconds(2);
    }
    if (restored) {
        g_restore_requested=false;
        if (g_deferred_horse_release && HorseIdentity(g_rank_lease.owner.horse)==g_rank_lease.owner) {
            Ped horse=g_rank_lease.owner.horse;
            firearm_guard::Release(horse);
            PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(horse,false);
            ENTITY::SET_PED_AS_NO_LONGER_NEEDED(&horse);
        }
        g_deferred_horse_release=false;
    }
    return restored;
}

bool PlannedOpponentRanksReady()
{
    if (!g_horse_plan_ready || !g_planned_natural.Valid() || !g_planned_target.Valid() ||
        g_restore_requested) return false;
    const auto actual=horse_attributes::Read(g_opponent_horse);
    if (actual.model!=g_horse_model || actual.model_default!=g_planned_natural ||
        actual.entity_base!=g_planned_target || actual.current!=g_planned_target) return false;
    if (g_planned_natural==g_planned_target) return !g_rank_lease.pending;
    return g_rank_lease.pending && g_rank_lease.owner==HorseIdentity(g_opponent_horse) &&
        g_rank_lease.original==g_planned_natural && g_rank_lease.target==g_planned_target;
}

bool EnsurePlannedOpponentRanks(const char* stage)
{
    if (!g_horse_plan_ready || g_restore_requested || PlayerHasTakenOpponentHorse() ||
        !g_opponent_rider || !ENTITY::DOES_ENTITY_EXIST(g_opponent_rider) ||
        ENTITY::IS_ENTITY_DEAD(g_opponent_rider)) return false;
    if (!g_rank_lease.pending) {
        const auto before=horse_attributes::Read(g_opponent_horse);
        if (before.model!=g_horse_model || before.model_default!=g_planned_natural ||
            before.entity_base!=g_planned_natural || before.current!=g_planned_natural ||
            !g_rank_lease.Apply(HorseIdentity(g_opponent_horse),g_planned_natural,g_planned_target,
                HorseIdentity,ReadBaseRank,WriteBaseRank)) return false;
    }
    const bool verified=PlannedOpponentRanksReady();
    Log(std::string("horse planned ranks ")+(verified?"verified":"FAILED")+" stage="+stage+
        " raw_natural="+horse_attributes::Pair(g_planned_natural)+
        " raw_target="+horse_attributes::Pair(g_planned_target)+
        (g_rank_lease.pending?" temporary instance lease":" natural match; no writes"));
    return verified;
}

void RefreshHorseCatalog()
{
    const Ped player=PLAYER::PLAYER_PED_ID();
    if (!player || !ENTITY::DOES_ENTITY_EXIST(player) || ENTITY::IS_ENTITY_DEAD(player) ||
        NETWORK::NETWORK_IS_SESSION_ACTIVE() || MISC::GET_MISSION_FLAG()) return;
    if(horse_catalog::runtime::Initialize(g_directory,[](const std::string& line){Log(line);}))g_catalog_ready=false;
    if (g_catalog_ready && g_catalog_player==player &&
        (!g_horse_catalog.empty() || Clock::now()<g_catalog_retry)) return;
    // One identity/availability reader is shared by duels, stakes and bookies.
    // It reads metadata only; the ordinary loading pipeline verifies streaming
    // and the spawned instance before accepting money or starting the race.
    g_horse_catalog=horse_catalog::runtime::Read(g_directory,[](const std::string& line){Log(line);});
    g_catalog_player=player; g_catalog_ready=true; g_catalog_retry=Clock::now()+std::chrono::seconds(5);
}

std::size_t OpponentDifficultyIndex()
{
    const char* key=CurrentRules().Hosted()?FixedProfile().key:
        g_variant->archetype==RivalArchetype::Confident?"EncounterRhodes":
        g_variant->archetype==RivalArchetype::Novice?"EncounterStrawberry":"EncounterMountain";
    return *opponent_difficulty::Find(key);
}
horse_attributes::Ranks DifficultyTarget(const opponent_difficulty::Rule& rule,bool host_entrant=false)
{
    horse_attributes::Ranks original;
    if(!CurrentRules().Hosted()) {
        original=horse_attributes::ReadModelDefaults(MISC::GET_HASH_KEY(g_rival->horse_model));
        if(!original.Valid())return {};
        const int bonus=kDifficultyTiers[g_difficulty_tier].rank_bonus;
        original={std::min(9,original.speed+bonus),std::min(9,original.acceleration+bonus)};
    }
    // A stakes host takes the ceiling of their F11/default bands; ordinary
    // jockeys and duel hosts still draw independently. ResolveNaturalAcceleration
    // applies the same ceiling choice to the selected horse's relative bonus.
    return opponent_difficulty::Target(rule,CurrentRules().Hosted()?&FixedProfile():nullptr,original,
        [](int minimum,int maximum){return MISC::GET_RANDOM_INT_IN_RANGE(minimum,maximum);},
        host_entrant && CurrentRules().Field());
}
