// Active course sweep and nonparticipant enforcement.
// Included within the owning namespace/class; member access and order are preserved.
    template<class SideAudience> bool Tick(double now,SideAudience side_audience,bool may_decline=false,bool force=false) {
        horse_diagnostics::Scope stage_cost("race: clearance audit");
        may_decline_=may_decline;
        if(!active_)return true;
        const auto started=std::chrono::steady_clock::now();
        error.clear();
        RetryAcknowledgedHandovers();
        std::erase_if(parked_,[](Group& g){std::erase_if(g.members,[](const Saved& s){return !s.id.Same();});return g.members.empty();});
        for(const auto& group:parked_)for(const auto& s:group.members)if(PlayerOccupies(s.id.entity)) {
            error="player took an isolated traffic actor; stop before moving occupied entity";return false;
        }
        for(auto& group:parked_)if(!Enforce(group))return false;
        // A blackout audit forces the sweep: "only the participants are on the track" has to be true
        // when it is written down, not true a quarter of a second ago.
        if(!force && now<next_sweep_)return true;
        next_sweep_=now+kSweepSeconds;
        std::vector<Entity> live;if(!Traffic(live))return false;
        std::map<Entity,std::set<Entity>> graph;
        std::map<Entity,int> kinds;
        auto link=[&](Entity a,Entity b){if(a!=b && Exists(b)){graph[a].insert(b);graph[b].insert(a);}};
        std::set<Entity> seeds;
        for(const auto e:live) {
            const auto kind=ENTITY::GET_ENTITY_TYPE(e);graph[e];kinds[e]=kind;
            const auto attached=ENTITY::GET_ENTITY_ATTACHED_TO(e);
            if(Exists(attached)) {
                // A child knows its parent and a parent does not know its children, so this one is
                // asked of everything; the rest are asked below, only where they are read.
                const auto parent_kind=ENTITY::GET_ENTITY_TYPE(attached);
                // Include carried/tied peds, but never move a scenery anchor.
                if(parent_kind==1 || parent_kind==2)link(e,attached);
            }
            const auto p=ENTITY::GET_ENTITY_COORDS(e,true,false);
            if(!destination::Finite(p)){error="invalid live traffic position";return false;}
            if(InBounds(p) && !Racer(e) && !Held(e) && !WildlifeException(e)) {
                const auto distance=Distance(p);
                if(!(side_audience(e) && distance>kTrackHalfWidth) && ApproachingTrack(e,p,distance))seeds.insert(e);
            }
            // What a PERSON is attached to is asked of every one of them, because it is the side that
            // can be trusted: a ped knows the wagon it sits in, the horse it rides and the harness it
            // pulls from, whereas a wagon asked for its passengers answers from a seat count that can
            // come back as -1 or 0. Three or four natives a ped, and it is what keeps a wagon and the
            // passenger inside it in one group - half a wagon is worse than a wagon.
            if(kind==1) {
                link(e,PED::GET_VEHICLE_PED_IS_IN(e,false));link(e,PED::_GET_VEHICLE_DRAFT_HORSE_IS_ATTACHED_TO(e));
                if(PED::IS_PED_ON_MOUNT(e))link(e,PED::GET_MOUNT(e));
                if(ENTITY::GET_IS_ANIMAL(e))link(e,PED::_GET_RIDER_OF_MOUNT(e,false));
            }
        }
        // A WAGON's own list of who is aboard is the expensive half - a seat query each for up to
        // thirty-two seats and six harness slots, against three or four for a ped - and it is the half
        // that only matters once something has put the wagon in a group. So it is asked when the
        // search reaches it. Everything a wagon would have found this way about a live ped, the ped
        // has already said above; this catches the riders that are not in the window themselves.
        // (Making the ped side lazy too was measured cheaper and was wrong: a wagon reporting -1
        // passengers then formed a group of one and was deleted out from under its own passenger.)
        std::set<Entity> expanded;
        auto expand=[&](Entity e) {
            if(!expanded.insert(e).second)return;
            const auto known=kinds.find(e);
            const int kind=known!=kinds.end()?known->second:ENTITY::GET_ENTITY_TYPE(e);
            if(kind!=2)return;
            const int seats=std::clamp(VEHICLE::GET_VEHICLE_MAX_NUMBER_OF_PASSENGERS(e),0,32);
            for(int seat=-1;seat<seats;++seat)link(e,VEHICLE::GET_PED_IN_VEHICLE_SEAT(e,seat));
            for(int h=0;h<6;++h)link(e,VEHICLE::_GET_PED_IN_DRAFT_HARNESS(e,h));
        };
        std::set<Entity> visited;
        for(auto seed:seeds) {
            if(visited.contains(seed))continue;
            std::vector<Entity> pending{seed};std::set<Entity> group;
            while(!pending.empty()) {
                const auto e=pending.back();pending.pop_back();
                if(!Exists(e) || !group.insert(e).second)continue;
                if(group.size()>4096){error="traffic attachment graph exceeded bound";return false;}
                expand(e);
                for(auto other:graph[e])pending.push_back(other);
            }
            visited.insert(group.begin(),group.end());
            // Never move the player/competitor by moving their occupied mount.
            // An unlisted passenger on a race vehicle is an invalid roster,
            // not grounds for silently granting that passenger track access.
            if(std::any_of(group.begin(),group.end(),[&](auto e){return Racer(e);})) {
                error="nonparticipant attached to a race participant; no safe isolation";return false;
            }
            // Once the race is running, a group that has SPAWNED inside the lane
            // - never seen in the buffer before this tick - is removed outright.
            // One that approached from outside is parked; parking is also what
            // happens to anything that may not be deleted.
            const auto spawned_on_track=[&](Entity e){
                if(!Exists(e))return false;
                const auto it=seen_.find(e);
                if(it!=seen_.end() && it->second.Same())return false;
                return Distance(ENTITY::GET_ENTITY_COORDS(e,true,false))<=kTrackHalfWidth;
            };
            if(may_decline_ && std::any_of(group.begin(),group.end(),spawned_on_track)) {
                std::vector<Entity> members(group.begin(),group.end());std::string left;
                if(RemoveGroup(members,left)) {
                    if(removed_groups_<=16)Log("removed nonparticipant that spawned on the course: entities="+
                        std::to_string(members.size())+" first="+std::to_string(members.front()));
                    continue;
                }
            }
            if(!Park(group))return false;
        }
        // Remember who was here, for the spawn-versus-approach distinction
        // on the next tick. Bounded by the live population of the buffer.
        for(auto e:live)if(Exists(e) && !Racer(e) && InBounds(ENTITY::GET_ENTITY_COORDS(e,true,false)))seen_[e]=Identity::Read(e);
        if(seen_.size()>4096)seen_.clear();
        // Read back the live snapshot AFTER all groups have moved. Counting
        // requests or trusting a void native is not evidence of a clear track.
        int residual=0;
        for(auto e:live)if(Exists(e) && !Racer(e) && !WildlifeException(e) && !Neutralised(e)) {
            const auto p=ENTITY::GET_ENTITY_COORDS(e,true,false);
            if(InBounds(p,kLaneHalfWidth+2) && Distance(p)<=kLaneHalfWidth+2) {
                ++residual;
                if(residual==1)error="track residual after isolation: entity="+std::to_string(e)+
                    " model="+std::to_string(ENTITY::GET_ENTITY_MODEL(e))+
                    " kind="+std::to_string(ENTITY::GET_ENTITY_TYPE(e))+
                    " held="+std::to_string(Held(e))+
                    " player_owned="+std::to_string(PlayerOwns(e))+
                    " visible="+std::to_string(bool(ENTITY::IS_ENTITY_VISIBLE(e)))+
                    " frozen="+std::to_string(bool(ENTITY::_IS_ENTITY_FROZEN(e)))+
                    " collision_disabled="+std::to_string(bool(ENTITY::GET_ENTITY_COLLISION_DISABLED(e)))+
                    " distance="+std::to_string(Distance(p));
            }
        }
        // Before the flag drops, a residual means do not start: nothing is lost
        // by refusing a track we could not clear. Once the race is running the
        // same finding cannot be worth cancelling it - the player has ridden
        // minutes to get here - so it is reported and the race carries on.
        if(residual && !may_decline_)return false;
        if(residual) {
            error.clear();
            if(++residual_reports_<=16)Log("track residual during a running race, not cancelling: "+
                std::to_string(residual)+" nonparticipant(s) inside the lane");
        }
        const double elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
        audit_max_ms_=std::max(audit_max_ms_,elapsed);audit_sum_ms_+=elapsed;++audit_frames_;
        if(now>=next_log_) {
            // What the sweep did, in the three numbers a race that went wrong is
            // read back with: what the window held, what was done about it, and
            // whether the query covered the scene at all. Everything else this
            // line used to carry has its own entry when it happens - a decline,
            // a removal, a flag repair and a residual each log themselves for
            // the first sixteen - so a wall of counters every five seconds was
            // work nobody read. The two timers stay: they are the frame budget.
            next_log_=now+5;Log("audit verified: window="+std::to_string(window_.found)+
                " isolated="+std::to_string(Count())+"/"+std::to_string(isolated_total_)+
                " removed="+std::to_string(removed_groups_)+
                " query_roster="+std::to_string(window_.roster_seen)+"/"+std::to_string(window_.roster_expected)+
                " audit_mean_ms="+std::to_string(audit_sum_ms_/audit_frames_)+" audit_max_ms="+std::to_string(audit_max_ms_));
            audit_max_ms_=audit_sum_ms_=0;audit_frames_=0;
        }
        return true;
    }
