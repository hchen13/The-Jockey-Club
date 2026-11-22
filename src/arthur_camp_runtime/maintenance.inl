// Private implementation of arthur_camp.cpp; included inside its original namespace.
// Live resident and horse maintenance in original execution order.
void Maintain(const Clock::time_point& now) {
    horse_diagnostics::Scope stage_cost("camp: residents");
    if(SyncPreservedHorseWorld() || !PreservedHorseWorldCurrent()) {
        Forget("loaded-world timeline changed before maintenance; old references discarded");return;
    }
    // Also guard this entry point itself: no old resident or offer cleanup is
    // permitted after a world replacement, even when called outside Tick.
    if(WorldReplaced()){Forget("a save came up before resident maintenance; old entity handles left alone");return;}
    // Arthur's walk, recovery and race stand-down must not starve his horses'
    // ten-second ownership check or leave vacant meadow slots forgotten.
    MaintainHorses(now);
    if(!PreservedHorseWorldCurrent()){Forget("world changed during resident ownership probe; old references discarded");return;}
    TickMary(now);   // Her life goes on whatever Arthur is doing - racing, recovering, stood down.
    if(g_recovering){TickRecovery(now);return;}
    TickStandDown();
    if(g_stood_down)return;
    TickOffer(now);
    if(WorldReplaced()){Forget("a save came up; nothing this camp made is here any more");return;}
    if(!Exists(g_arthur) || ENTITY::IS_ENTITY_DEAD(g_arthur)) {
        Log(Exists(g_arthur)?"Arthur is dead; fetching him back":"Arthur is gone; fetching him back");
        if(Exists(g_arthur)){firearm_guard::Discard(g_arthur);Ped gone=g_arthur;PED::DELETE_PED(&gone);}
        g_arthur=0;g_targeted=false;
        g_recovering=true;g_recover_since=now;RequestAssets();
        return;
    }
    TickGreeting(now);
    // Where he is, against where his activity put him: a minute into an
    // activity that is running, he should be within a few metres of its
    // spot. Farther than that he is brought back (the player could not find
    // him on 2026-09-15); logged each time, with the distance.
    if(now>=g_where_check) {
        g_where_check=now+10s;
        if(g_activity_verified && now-g_activity_started>=60s) {
            const Scene& scene=g_layout.scenes[std::min(g_activity,g_layout.scenes.size()-1)];
            const Pose spot=ActivityPose(scene);
            const Vector3 at=ENTITY::GET_ENTITY_COORDS(g_arthur,true,false);
            const float away=std::hypot(at.x-spot.position.x,at.y-spot.position.y);
            if(away>6.0F) {
                char b[160];std::snprintf(b,sizeof b,"Arthur is %.0f m from his %s spot (at %.1f,%.1f,%.1f); sent back",static_cast<double>(away),ActivityName(g_activity),
                    static_cast<double>(at.x),static_cast<double>(at.y),static_cast<double>(at.z));
                Log(b);
                StartActivity(g_activity,away>30.0F,"brought back");
            }
        }
    }
    if(TickPending(now))return;
    // He is walking to his activity: the scenario starts, with its own way in,
    // when he gets there or when his patience runs out.
    if(g_walking) {
        const Vector3 at=ENTITY::GET_ENTITY_COORDS(g_arthur,true,false);
        const bool there=std::isfinite(at.x) && std::hypot(at.x-g_walk_to.position.x,at.y-g_walk_to.position.y)<=kArrived;
        if(!there && now<g_walk_by)return;
        if(g_walking_to_door) {
            if(!there) {
                Log("Arthur: tent entrance not reachable; choosing another activity without placing him through the wall");
                g_walking=false;g_walking_to_door=false;g_walk_seat=0;
                StartActivity(Next(g_activity,g_layout.scenes.size(),Unit()),false,"tent entrance blocked");
                return;
            }
            g_walking_to_door=false;g_walk_to=g_walk_after_door;g_walk_by=now+kWalkPatience;
            TASK::TASK_GO_TO_COORD_ANY_MEANS(g_arthur,g_walk_to.position.x,g_walk_to.position.y,g_walk_to.position.z,1.0F,0,false,0,0);
            PED::SET_PED_KEEP_TASK(g_arthur,true);
            Log("Arthur: through tent entrance; walking to bed side");
            return;
        }
        const Scene& walked=g_layout.scenes[std::min(g_activity,g_layout.scenes.size()-1)];
        if(!there && TentDoorFor(walked)) {
            Log("Arthur: bed side not reachable after entering tent; choosing another activity");
            g_walking=false;g_walking_to_door=false;g_walk_seat=0;
            StartActivity(Next(g_activity,g_layout.scenes.size(),Unit()),false,"bed side blocked");
            return;
        }
        if(!there) {
            ENTITY::SET_ENTITY_COORDS(g_arthur,g_walk_to.position.x,g_walk_to.position.y,g_walk_to.position.z,false,false,true,false);
            ENTITY::SET_ENTITY_HEADING(g_arthur,g_walk_to.heading);
        }
        if(g_walk_seat) {
            // He is standing in front of the seat: the point takes him the last step and sits him down.
            TASK::TASK_USE_SCENARIO_POINT(g_arthur,g_walk_seat,walked.anim.empty()?nullptr:walked.anim.c_str(),-1,false,kWalkToPoint,0,false,-1.0F,false);
        } else TASK::TASK_START_SCENARIO_IN_PLACE_HASH(g_arthur,Key(walked.scenario),-1,there,walked.anim.empty()?0:Key(walked.anim),g_walk_to.heading,false);
        PED::SET_PED_KEEP_TASK(g_arthur,true);
        const bool seat=g_walk_seat!=0;
        g_walking=false;g_walk_seat=0;g_verify_after=now+12s;
        Log(std::string("activity ")+ActivityName(g_activity)+(seat?(there?": he is at his seat; sitting down":": he could not reach his seat and was placed beside it")
            :(there?": he is there; starting it where he stands":": he could not walk there and was placed")));
        return;
    }
    const Scene& s=g_layout.scenes[std::min(g_activity,g_layout.scenes.size()-1)];
    // Once an activity is running it is watched: a scenario that ends by
    // itself used to leave him standing over a dropped cigar until the
    // activity's timer ran out, and a rider who shoved him out of it was not
    // noticed at all (2026-09-16). Three checks out of it start the next one.
    if(g_activity_verified && !g_activity_given_up && now>=g_scenario_check) {
        g_scenario_check=now+kSceneCheckEvery;
        if(PED::IS_PED_USING_SCENARIO_HASH(g_arthur,Key(s.scenario)))g_scenario_missing=0;
        else if(++g_scenario_missing>=kSceneMissingChecks) {
            Log(std::string("activity ")+ActivityName(g_activity)+" has ended or he was pushed out of it; on to the next");
            g_activity_attempts=0;
            AskActivityAt(Next(g_activity,g_layout.scenes.size(),Unit()),now,"the last one ended");
        }
    }
    if(!g_activity_verified && now>=g_verify_after) {
        const auto stand_by_the_fire=[&]{
            Log("no activity's scenario ran; Arthur stands by the fire until the next change");
            g_activity_verified=true;g_activity_given_up=true;
            if(const auto* fire=FindProp(g_layout,"Fire")){const Pose p=Offset(g_placed.contains("Fire")?g_placed["Fire"]:PropPose(g_layout,*fire),-1.2F,0,0,0);
                TASK::CLEAR_PED_TASKS(g_arthur,false,false);TASK::TASK_GO_TO_COORD_ANY_MEANS(g_arthur,p.position.x,p.position.y,p.position.z,1.0F,0,false,0,0);}
        };
        const bool typed=PED::IS_PED_USING_SCENARIO_HASH(g_arthur,Key(s.scenario));
        const bool any=PED::IS_PED_USING_ANY_SCENARIO(g_arthur);
        // Only the named scenario counts. "Some scenario" was accepted once,
        // and it was the map's: he walked off into another tent's point and
        // could not be found (2026-09-15). A walk that ended elsewhere is
        // retried by placing him; only then is another activity tried.
        if(typed) {
            g_activity_verified=true;g_activity_attempts=0;
            Log(std::string("activity ")+ActivityName(g_activity)+" running (the named scenario)");
        } else if(SeatPoint(s)) {
            // A seat's point either takes him or it does not; placing him in it is what went wrong before.
            if(++g_activity_attempts>=kActivityAttempts){stand_by_the_fire();return;}
            Log(std::string("activity ")+ActivityName(g_activity)+": he did not reach his seat; trying another");
            StartActivity(Next(g_activity,g_layout.scenes.size(),Unit()),false,"seat not reached");
        } else if(!g_activity_placed) {
            Log(std::string("activity ")+ActivityName(g_activity)+": "+s.scenario+(any?" not reached (he is in another scenario)":" did not start")+"; placing him in it");
            StartActivity(g_activity,true,"placed");
        } else if(g_asked) {
            Log(std::string("activity ")+ActivityName(g_activity)+": "+s.scenario+" did not start, but it is the one asked for; he stays with it");
            g_activity_verified=true;g_activity_attempts=0;
        } else if(++g_activity_attempts<kActivityAttempts) {
            Log(std::string("activity ")+ActivityName(g_activity)+": "+s.scenario+" did not start in 8 s; trying another");
            StartActivity(Next(g_activity,g_layout.scenes.size(),Unit()),true,"fallback");
        } else stand_by_the_fire();
    }
    if(now>=g_activity_until && !g_has_pending){g_activity_attempts=0;AskActivityAt(Next(g_activity,g_layout.scenes.size(),Unit()),now,"next");}
}
