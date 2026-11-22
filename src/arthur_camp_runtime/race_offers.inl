// Private implementation of arthur_camp.cpp; included inside its original namespace.
// Arthur and owned-horse race offer prompt ownership and selection.
// Arthur and his horses offer races the way every other fixed host does: lock
// on and the same two prompts appear, the paid race and the free practice.
// Arthur's is the Thoroughbred stakes race at his camp; each horse's is a
// one-on-one with him riding that horse. Choosing starts it there and then -
// the pipeline takes the choice as read once its host is ready, so there is
// no second prompt to press at a host the player is already facing.
Prompt g_offer_race=0,g_offer_practice=0,g_offer_info=0;
Ped g_offer_on=0;
firearm_guard::Identity g_offer_identity{};
bool g_offer_race_was=false,g_offer_practice_was=false,g_offer_info_was=false;
// The mount the prompts were last worded for; -1 means they have none yet.
int g_offer_mount=-1;
Ped g_racing_horse=0;            // The horse chosen, kept out of the way while the race runs.
bool g_horse_stood_down=false;
// The horse Arthur rides in his stakes race, the one the card on him shows.
std::string g_stakes_horse;
Ped CampHorse(const char* model) {
    const Hash h=Key(model);
    const PlayerHorses player;
    for(const auto& horse:g_horses)if(horse.model==h && AvailableCampHorse(horse,player))return horse.ped;
    return 0;
}
std::string HorseModelName(Ped horse) {
    const PlayerHorses player;
    for(const auto& resident:g_horses)if(resident.ped==horse && AvailableCampHorse(resident,player) &&
        resident.slot<g_layout.horse_models.size())return g_layout.horse_models[resident.slot];
    return {};
}
// Snapshot only this exact unclaimed resident. No retained entity handle or
// player-horse state is passed to the race system.
bool CaptureCampHorseDirt(const Horse& horse,std::optional<float>& result) {
    result.reset();
    if(!PreservedHorseWorldCurrent() || !SameCampHorse(horse) || ENTITY::IS_ENTITY_DEAD(horse.ped) ||
        PlayerHorses{}.Binding(horse.ped))return false;
    // GET_PED_DIRT_LEVEL: the locally bundled native declaration has an
    // untyped return, but the engine and SP scripts use a float in [0,1].
    const float dirt=invoke<float>(0x0105FEE8F9091255,horse.ped,true);
    if(!PreservedHorseWorldCurrent() || !SameCampHorse(horse) || ENTITY::IS_ENTITY_DEAD(horse.ped) ||
        PlayerHorses{}.Binding(horse.ped))return false;
    if(std::isfinite(dirt) && dirt>=0.F && dirt<=1.F)result=dirt;
    return true;
}
Prompt MakeOfferPrompt(const std::string& text,const char* control) {
    const Prompt prompt=HUD::_UI_PROMPT_REGISTER_BEGIN();
    if(!prompt)return 0;
    HUD::_UI_PROMPT_SET_CONTROL_ACTION(prompt,Key(control));
    HUD::_UI_PROMPT_SET_TEXT(prompt,i18n::Literal(text));
    HUD::_UI_PROMPT_SET_PRIORITY(prompt,0);
    HUD::_UI_PROMPT_SET_TRANSPORT_MODE(prompt,0);
    HUD::_UI_PROMPT_SET_ATTRIBUTE(prompt,18,true);
    HUD::_UI_PROMPT_SET_STANDARD_MODE(prompt,false);
    HUD::_UI_PROMPT_REGISTER_END(prompt);
    HUD::_UI_PROMPT_SET_VISIBLE(prompt,true);
    HUD::_UI_PROMPT_SET_ENABLED(prompt,true);
    return prompt;
}
void ForgetOffer() {
    g_offer_race=g_offer_practice=g_offer_info=0;
    g_offer_on=0;g_offer_identity={};
    g_offer_race_was=false;g_offer_practice_was=false;g_offer_info_was=false;g_offer_mount=-1;
    g_stakes_horse.clear();
}
void ReleaseOffer() {
    if(g_offer_identity.Valid() && firearm_guard::Identify(g_offer_on)==g_offer_identity) {
        MISC::UNREGISTER_INTERACTION_LOCKON_PROMPT(g_offer_on);
        PED::SET_PED_CONFIG_FLAG(g_offer_on,130,false);  // He can be talked to again.
    }
    for(Prompt* prompt:{&g_offer_race,&g_offer_practice,&g_offer_info}) {
        if(*prompt && HUD::_UI_PROMPT_IS_VALID(*prompt))HUD::_UI_PROMPT_DELETE(*prompt);
        *prompt=0;
    }
    horse_races::CloseCampHorseInfo();
    ForgetOffer();
}
void TickOffer(const Clock::time_point& now) {
    if(!PreservedHorseWorldCurrent()){ForgetOffer();return;}
    if(horse_races::CampRaceUnderway()){ReleaseOffer();return;}
    if(g_offer_on && firearm_guard::Identify(g_offer_on)!=g_offer_identity){ReleaseOffer();return;}
    const Player player=PLAYER::PLAYER_ID();
    const Ped player_ped=PLAYER::PLAYER_PED_ID();
    Ped on=0;
    if(StillOurs(g_arthur,Key("PLAYER_ZERO"),Point{},0) && PLAYER::IS_PLAYER_TARGETTING_ENTITY(player,g_arthur,false))on=g_arthur;
    if(!on)for(auto& horse:g_horses)if(horse.ped && PLAYER::IS_PLAYER_TARGETTING_ENTITY(player,horse.ped,false)) {
        const PlayerHorses owned;
        // A temporary/mounted/owned horse is relinquished immediately when
        // targeted, even before the periodic resident maintenance runs.
        if(owned.Binding(horse.ped))SetCampHorseFleeBlock(horse,false);
        else if(AvailableCampHorse(horse,owned)){RefreshCampHorseFleeBlock(horse,now);on=horse.ped;break;}
    }
    if(on!=g_offer_on) {
        ReleaseOffer();
        g_offer_on=on;
        if(!on)return;
        g_offer_identity=firearm_guard::Identify(on);
        if(!g_offer_identity.Valid()){ForgetOffer();return;}
        if(on==g_arthur) {
            std::vector<const char*> available;
            for(const char* model:{"A_C_HORSE_THOROUGHBRED_BLOODBAY","A_C_HORSE_THOROUGHBRED_REVERSEDAPPLEBLACK"})
                if(CampHorse(model))available.push_back(model);
            g_stakes_horse.clear();
            if(!available.empty()) {
                const int index=MISC::GET_RANDOM_INT_IN_RANGE(0,static_cast<int>(available.size()));
                if(index>=0 && static_cast<std::size_t>(index)<available.size())g_stakes_horse=available[static_cast<std::size_t>(index)];
            }
        }
        // The same two prompts every fixed host shows, in the same words, and
        // bound to this ped's own lock-on group: a prompt with no group floats
        // free and shows the moment the player walks up (2026-09-16).
        const char* made_for=on==g_arthur?"arthur-camp":"arthur-camp-duel";
        g_offer_mount=static_cast<int>(ENTITY::GET_ENTITY_MODEL(PED::GET_MOUNT(player_ped)));
        g_offer_race=MakeOfferPrompt(horse_races::CampPromptLabel(made_for,false,g_offer_mount),"INPUT_INTERACT_LOCKON_POS");
        g_offer_practice=MakeOfferPrompt(horse_races::CampPromptLabel(made_for,true,g_offer_mount),"INPUT_INTERACT_LOCKON_NEG");
        if(!g_offer_race || !g_offer_practice){Log("the camp's race prompts could not be made");ReleaseOffer();return;}
        const int group=HUD::_UI_PROMPT_GET_GROUP_ID_FOR_TARGET_ENTITY(on);
        HUD::_UI_PROMPT_SET_GROUP(g_offer_race,group,0);
        HUD::_UI_PROMPT_SET_GROUP(g_offer_practice,group,0);
        // On Arthur himself the third prompt every host has: the card on the
        // horse he races, his blood bay, at the ranks he races it at.
        if(on==g_arthur) {
            g_offer_info=MakeOfferPrompt(i18n::Text("Horse Info","查看马匹属性"),"INPUT_INTERACT_LOCKON_TARGET_INFO");
            if(g_offer_info)HUD::_UI_PROMPT_SET_GROUP(g_offer_info,group,0);
        }
        PED::SET_PED_CONFIG_FLAG(on,130,true);   // As every host carries while it offers.
        const bool registered=MISC::REGISTER_INTERACTION_LOCKON_PROMPT(on,"",15.0F,15.0F,32776,0.0F,0.0F,g_offer_race,false,-1);
        Log(std::string("camp offer interaction ")+(registered?"registered":"registration failed"));
    }
    if(!on || !g_offer_race)return;
    // The visitor may change horse without letting go of the lock-on, so what
    // the prompts say and whether they may be pressed are answered every tick.
    const char* station=on==g_arthur?"arthur-camp":"arthur-camp-duel";
    const int mount=static_cast<int>(ENTITY::GET_ENTITY_MODEL(PED::GET_MOUNT(player_ped)));
    if(mount!=g_offer_mount) {
        g_offer_mount=mount;
        HUD::_UI_PROMPT_SET_TEXT(g_offer_race,i18n::Literal(horse_races::CampPromptLabel(station,false,mount)));
        HUD::_UI_PROMPT_SET_TEXT(g_offer_practice,i18n::Literal(horse_races::CampPromptLabel(station,true,mount)));
    }
    const bool may=horse_races::CampOfferReady(station,mount) && !horse_races::CampRaceUnderway();
    HUD::_UI_PROMPT_SET_ENABLED(g_offer_race,may);
    HUD::_UI_PROMPT_SET_ENABLED(g_offer_practice,may);
    if(g_offer_info) {
        const bool active=horse_races::CampHorseInfoActive();
        HUD::_UI_PROMPT_SET_TEXT(g_offer_info,i18n::Literal(active?
            i18n::Text("Hide Horse Info","收起马匹属性"):i18n::Text("Horse Info","查看马匹属性")));
        const bool info=HUD::_UI_PROMPT_IS_PRESSED(g_offer_info)!=0;
        if(info && !g_offer_info_was) {
            if(active)horse_races::CloseCampHorseInfo();
            else if(const Ped horse=CampHorse(g_stakes_horse.c_str()))horse_races::ShowCampHorseInfo(horse,g_stakes_horse);
            else Log("no reserved Thoroughbred in the camp to show the card on");
        }
        g_offer_info_was=info;
    }
    const bool race=HUD::_UI_PROMPT_IS_PRESSED(g_offer_race)!=0;
    const bool practice=HUD::_UI_PROMPT_IS_PRESSED(g_offer_practice)!=0;
    const bool chose_race=race && !g_offer_race_was;
    const bool chose_practice=!chose_race && practice && !g_offer_practice_was;
    g_offer_race_was=race;g_offer_practice_was=practice;
    if(!chose_race && !chose_practice)return;
    if(on==g_arthur) {
        const Hash model=Key(g_stakes_horse);
        auto reserved=std::find_if(g_horses.begin(),g_horses.end(),[&](const Horse& h){return h.model==model && h.ped;});
        if(reserved==g_horses.end() || !RecheckCampHorseAction(*reserved,"stakes_race_start")){ReleaseOffer();return;}
        std::optional<float> dirt;
        if(!CaptureCampHorseDirt(*reserved,dirt)){ReleaseOffer();return;}
        if(horse_races::BeginCampRace("arthur-camp",g_stakes_horse,chose_practice,dirt))g_racing_horse=0;
        return;
    }
    auto picked=std::find_if(g_horses.begin(),g_horses.end(),[&](const Horse& h){return h.ped==on;});
    if(picked==g_horses.end() || !RecheckCampHorseAction(*picked,"duel_race_start")){ReleaseOffer();return;}
    const std::string name=HorseModelName(on);
    if(name.empty()){Log("the horse locked on to is not one of Arthur's own");return;}
    std::optional<float> dirt;
    if(!CaptureCampHorseDirt(*picked,dirt)){ReleaseOffer();return;}
    if(horse_races::BeginCampRace("arthur-camp-duel",name,chose_practice,dirt))g_racing_horse=on;
}
