// Private implementation of arthur_camp.cpp; included inside its original namespace.
// Arthur creation and ordered camp scene construction.
Ped SpawnCampHorse(Hash model,const Pose& pose) {
    return PED::CREATE_PED(model,pose.position.x,pose.position.y,pose.position.z,pose.heading,false,true,true,true);
}
// Arthur himself: his named outfit if it has loaded, a bare PLAYER_ZERO
// dressed by hash if it has not. Called when the camp is built and again if
// he is ever lost, so losing him costs him and not the camp.
bool MakeArthur(const Pose& spot) {
    Ped arthur=0;std::string dressed;
    if(OutfitLoaded()) {
        arthur=PED::_CREATE_META_PED_OUTFIT_PED(g_outfit_request,spot.position.x,spot.position.y,spot.position.z,spot.heading,true,true,false,true);
        dressed="from his named outfit";
    }
    if(!Exists(arthur)) {
        arthur=PED::CREATE_PED(Key("PLAYER_ZERO"),spot.position.x,spot.position.y,spot.position.z,spot.heading,false,true,true,true);
            if(!Exists(arthur)){Log("Arthur could not be created");return false;}
        // A PLAYER_ZERO with no outfit has no components: he exists and is
        // invisible, which is what the player met on 2026-09-15. Dress him.
        if(OutfitLoaded() && PED::_APPLY_PED_META_PED_OUTFIT(g_outfit_request,arthur,true,false))dressed="named outfit applied";
        else {PED::_EQUIP_META_PED_OUTFIT(arthur,appearance::FixedOutfit(characters::Id::Arthur));dressed="outfit equipped by hash";}
    }
    PED::_UPDATE_PED_VARIATION(arthur,false,true,true,true,false);
    Log("Arthur created "+dressed+"; components="+std::to_string(PED::_GET_NUM_COMPONENTS_IN_PED(arthur)));
    ENTITY::SET_ENTITY_AS_MISSION_ENTITY(arthur,true,true);
    WEAPON::REMOVE_ALL_PED_WEAPONS(arthur,true,true);
    PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(arthur,true);
    PED::SET_PED_CAN_BE_TARGETTED(arthur,true);
    // A horse at a gallop knocked him over and killed him, and the camp went
    // with him (2026-09-16). He takes no damage and does not go to ragdoll:
    // a rider who runs into him pushes him about and nothing more.
    ENTITY::SET_ENTITY_INVINCIBLE(arthur,true);
    PED::SET_PED_CAN_RAGDOLL(arthur,false);
    // Surveyed on the chapter 2 save (2026-09-15): every gang member and
    // every gang horse carries 297 ForceInteractionLockonOnTargetPed, and
    // none carries 130 DisableTalkTo - which the race hosts set on purpose
    // and which had been copied here, so he could not be talked to.
    // ...and the rest of a race host's flag set is kept: he was visible with
    // it (18:50, 20:20) and not without it (21:18, 2026-09-15), 315 and the
    // cleared ones being the only difference between the two.
    for(int on:{297,315})PED::SET_PED_CONFIG_FLAG(arthur,on,true);
    for(int off:{130,178,301,317,331,493})PED::SET_PED_CONFIG_FLAG(arthur,off,false);
    PED::SET_PED_RELATIONSHIP_GROUP_HASH(arthur,Key("REL_GANG_DUTCHS"));
    if(const auto* profile=hosts::Find(characters::Id::Arthur))
        PED::_SET_PED_PROMPT_NAME(arthur,i18n::Literal(i18n::Text(profile->name,profile->chinese)));
    firearm_guard::Track(arthur,false);
    g_arthur=arthur;g_targeted=false;
    return true;
}
bool Build() {
    if(!PreservedHorseWorldCurrent()) {
        Log("camp build waits for a reliable loaded-world timeline");return false;
    }
    g_objects.clear();g_placed.clear();g_horses.clear();g_seat_points.clear();g_seat_point_of.clear();g_activity_point.clear();
    RequestIpls();
    RequestScenarioAssets();
    int seats=0;
    struct SeatProp {int handle;std::string key,model;Pose pose;};
    std::vector<SeatProp> seat_props;
    for(const auto& p:g_layout.props) {
        const Hash h=Key(p.model);
        if(std::find(g_models.begin(),g_models.end(),h)==g_models.end())continue;
        const Pose pose=Grounded(PropPose(g_layout,p),p.ground,p.up);
        const Object o=OBJECT::CREATE_OBJECT_NO_OFFSET(h,pose.position.x,pose.position.y,pose.position.z,false,true,false,false);
        if(!Exists(o)){Log("prop not created: "+p.key+" "+p.model);continue;}
        ENTITY::SET_ENTITY_HEADING(o,pose.heading);
        // A piece that leans or hangs: its recorded pitch and roll, the yaw as placed.
        if(p.pitch!=0 || p.roll!=0)ENTITY::SET_ENTITY_ROTATION(o,p.pitch,p.roll,pose.heading,2,true);
        if(p.ground)OBJECT::PLACE_OBJECT_ON_GROUND_PROPERLY(o,true);
        ENTITY::SET_ENTITY_AS_MISSION_ENTITY(o,true,true);
        ENTITY::FREEZE_ENTITY_POSITION(o,true);
        Pose landed=pose;
        const Vector3 at=ENTITY::GET_ENTITY_COORDS(o,true,false);
        if(std::isfinite(at.z) && std::abs(at.z-pose.position.z)<2.0F)landed.position.z=at.z;
        g_placed[p.key]=landed;
        g_objects.push_back({o,p.key,landed.position.z,h,landed.position});
        // Its scenario point is made in the pass below, once every prop stands and the fire's place
        // is known: a seat round the fire has to face the fire, whichever way the prop was dropped.
        if(g_layout.player_seats && SeatScenario(p.model))seat_props.push_back({o,p.key,p.model,landed});
    }
    // One spot on a prop carries one point, however many things want it. A crate that was both a seat
    // the player may use and where Mary takes her coffee carried two, and the game duly seated two
    // people on the one crate - her and a passer-by, inside each other (the user, 2026-09-22). Where a
    // seat point and an activity want the same spot they now share one, whose height is the one
    // measured off the model rather than the one typed into the layout.
    const auto spot_key=[](const std::string& prop,const std::string& scenario,float right,float forward) {
        const auto tenth=[](float v){return std::to_string(std::lround(v*10.0F));};
        return prop+"|"+scenario+"|"+tenth(right)+","+tenth(forward);
    };
    std::map<std::string,int> point_at;
    for(const auto& seat_prop:seat_props) {
        const char* scenario=SeatScenario(seat_prop.model);
        if(!scenario)continue;
        // A fallen log's model bounds include its tall broken ends. Those are
        // not the surface the sitter uses; putting the point at the measured
        // maximum left the campfire NPC visibly suspended over its middle.
        const bool uneven_log=Lower(seat_prop.model).find("_log")!=std::string::npos;
        const auto seat=SeatPointOffset(scenario,uneven_log?0.0F:SeatTop(Key(seat_prop.model)));
        const float yaw=FireFacingYaw(seat_prop.pose).value_or(seat.yaw);
        const int point=TASK::CREATE_SCENARIO_POINT_HASH_ATTACHED_TO_ENTITY(seat_prop.handle,Key(scenario),0,0,seat.up,yaw,0,0,false);
        if(point){g_seat_points.push_back(point);g_seat_point_of[seat_prop.key]=point;point_at[spot_key(seat_prop.key,scenario,0,0)]=point;++seats;}
        else Log("no scenario point on "+seat_prop.key+" ("+scenario+")");
    }
    // A sitting activity sits on a point of its own, on its anchor prop and at
    // the offsets the activity states. The point is what the game aligns a
    // sitting ped to, and the one the player may use is left as it is.
    for(const auto& s:g_layout.scenes) {
        if(!s.sitting || s.anchor.empty())continue;
        const Placed* anchor=PlacedByKey(s.anchor);
        if(!anchor || !Exists(anchor->handle))continue;
        const std::string key=spot_key(s.anchor,s.scenario,s.right,s.forward);
        const auto have=point_at.find(key);
        int point=have==point_at.end()?0:have->second;
        if(!point) {
            const auto placed=g_placed.find(s.anchor);
            const float yaw=placed==g_placed.end()?s.yaw:SeatFacesFire(placed->second,s.anchor).value_or(s.yaw);
            point=TASK::CREATE_SCENARIO_POINT_HASH_ATTACHED_TO_ENTITY(anchor->handle,Key(s.scenario),
                s.right,s.forward,s.up,yaw,0,0,false);
            if(!point){Log("no seat point for activity "+s.name+" on "+s.anchor);continue;}
            g_seat_points.push_back(point);point_at[key]=point;
            if(!g_seat_point_of.count(s.anchor))g_seat_point_of[s.anchor]=point;
        }
        g_activity_point[s.name]=point;
    }
    g_mary.points.clear();
    if(g_layout.companion)for(const auto& s:g_layout.companion_scenes) {
        if(!s.sitting || s.anchor.empty())continue;
        const Placed* anchor=PlacedByKey(s.anchor);
        if(!anchor || !Exists(anchor->handle))continue;
        // Her lying-down activity is on the same cot as his, at the same spot: one point, and
        // whichever of them is on it, the other takes a different activity.
        const std::string key=spot_key(s.anchor,s.scenario,s.right,s.forward);
        const auto have=point_at.find(key);
        int point=have==point_at.end()?0:have->second;
        if(!point) {
            const auto placed=g_placed.find(s.anchor);
            const float yaw=placed==g_placed.end()?s.yaw:SeatFacesFire(placed->second,s.anchor).value_or(s.yaw);
            point=TASK::CREATE_SCENARIO_POINT_HASH_ATTACHED_TO_ENTITY(anchor->handle,Key(s.scenario),
                s.right,s.forward,s.up,yaw,0,0,false);
            if(!point){Log("no seat point for Mary's activity "+s.name+" on "+s.anchor);continue;}
            g_seat_points.push_back(point);point_at[key]=point;
        }
        g_mary.points[s.name]=point;
    }
    HideMapPieces();   // After the props: a swap needs to know that its own piece stands.
    // ...and the dressing the map files bring, once they have brought it.
    g_dressing_at=Clock::now()+kDressingSettle;g_dressing_done=false;
    MakeInteractions();
    // Arthur, placed straight into his first activity out of the player's sight.
    const std::size_t first=First(g_layout.scenes.size(),Unit());
    const Pose spot=Grounded(ActivityPose(g_layout.scenes[first]),true,0);
    if(!MakeArthur(spot))return false;
    const Ped arthur=g_arthur;
    // His horses: grazing on the meadow on the camp's grazing scenario, as
    // the gang's did, each on the place the chapter 2 camp was recorded at.
    int grazed=0;
    const PlayerHorses player_horses;
    for(std::size_t i=0;i<g_layout.horse_models.size();++i) {
        const auto& model=g_layout.horse_models[i];
        const Hash h=Key(model);
        if(std::find(g_models.begin(),g_models.end(),h)==g_models.end())continue;
        // The meadow's places are the recorded ones: a horse with none of its own has nowhere to stand.
        if(i>=g_layout.horse_spots.size()){Log("no recorded spot on the meadow for "+model+"; it is left out");continue;}
        Horse slot;slot.stand=g_layout.horse_spots[i];slot.model=h;slot.slot=i;
        slot.retry_after=Clock::now()+10s;
        g_horses.push_back(slot);
        if(HorseSlotOccupiedByPlayer(g_horses.back(),player_horses)) {
            LogHorseState(g_horses.back(),"waiting_for_slot","reason=player_horse_nearby_at_build");continue;
        }
        if(MakeCampHorse(g_horses.back(),"camp_build"))++grazed;
    }
    // The map marker is not made here: it is on the map from the moment a
    // save comes up for John, built camp or not (TickMarkers).
    Log("camp up: "+std::to_string(g_ipls.size())+" IPLs, "+std::to_string(g_objects.size())+" props ("+
        std::to_string(seats)+" seats), Arthur ped="+std::to_string(arthur)+", "+std::to_string(grazed)+" horses ("+
        std::to_string(grazed)+" grazing on "+g_layout.horse_scenario+(g_layout.horse_spots.empty()?std::string():
        " at "+std::to_string(std::min(g_layout.horse_spots.size(),g_layout.horse_models.size()))+" recorded spots")+")"+
        (g_hides.empty()?std::string():", "+std::to_string(g_hides.size())+" map pieces hidden")+
        (g_locker.prompt?", locker prompt":"")+(g_blip?", map marker":""));
    StartActivity(first,true,"first");
    g_horse_check=Clock::now()+10s;g_fast_horse_check={};g_activity_attempts=0;
    g_mary.ped=0;g_mary.remake_at=Clock::now();   // Made by TickMary, as soon as her model is in.
    return true;
}
