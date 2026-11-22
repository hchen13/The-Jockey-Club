// Nonparticipant ownership, parking, restoration and script handback.
// Included within the owning namespace/class; member access and order are preserved.
    bool Deletable(Entity e) const {
        return Exists(e) && !PlayerOwns(e) && !ENTITY::IS_ENTITY_A_MISSION_ENTITY(e);
    }
    static bool Remove(Entity e) {
        if(!Exists(e))return true;
        ENTITY::SET_ENTITY_AS_MISSION_ENTITY(e,true,true);
        Entity handle=e;
        if(ENTITY::GET_ENTITY_TYPE(e)==2)VEHICLE::DELETE_VEHICLE(&handle);else PED::DELETE_PED(&handle);
        return !Exists(e);
    }
    // Whole attachment groups go together: half a wagon is worse than a wagon.
    bool RemoveGroup(const std::vector<Entity>& members,std::string& left) {
        for(auto e:members)if(Exists(e) && (Racer(e) || !Deletable(e))){left="undeletable entity="+std::to_string(e);return false;}
        for(auto e:members)if(Exists(e) && ENTITY::GET_ENTITY_TYPE(e)==1 && !ENTITY::GET_IS_ANIMAL(e))Remove(e); // riders first
        for(auto e:members)if(Exists(e))Remove(e);
        for(auto e:members)if(Exists(e)){left="entity survived deletion="+std::to_string(e);return false;}
        ++removed_groups_;return true;
    }
    bool Decline(Group& group,const std::string& why) {
        if(!may_decline_){error=why;return false;}
        group.declined=true;++declined_groups_;
        std::vector<Entity> members;
        for(const auto& m:group.members)if(m.id.Same())members.push_back(m.id.entity);
        std::string left;
        const bool removed=RemoveGroup(members,left);
        if(!removed)for(auto& m:group.members)if(m.id.Same()) {
            // Last resort for what may not be deleted: it stays hidden and
            // frozen with its collision off, so the field rides through where it
            // stands. Restore() puts collision back before it is ever unfrozen.
            const auto e=m.id.entity;
            ENTITY::SET_ENTITY_VISIBLE(e,false);
            ENTITY::FREEZE_ENTITY_POSITION(e,true);
            ENTITY::SET_ENTITY_COLLISION(e,false,true);
            m.neutralised=!ENTITY::IS_ENTITY_VISIBLE(e) && ENTITY::_IS_ENTITY_FROZEN(e) &&
                ENTITY::GET_ENTITY_COLLISION_DISABLED(e);
        }
        if(declined_groups_<=16)Log(std::string("isolation declined, race continues; ")+
            (removed?"group removed: ":"group could not be removed ("+left+"), neutralised in place: ")+why);
        // This failure was handled. Leaving `error` populated made the next
        // preparation frame fail even though Tick returned success.
        error.clear();
        return true;
    }
    bool Enforce(Group& group) {
        if(group.declined)return true;
        // A previous race's native takeover is not a new isolation lease.
        // Never re-hide/re-park that actor (or its attached group) simply
        // because another race has begun while collision read-back settles.
        if(std::any_of(group.members.begin(),group.members.end(),[](const Saved& s){return s.handed_back;}))return true;
        std::vector<Saved*> live;live.reserve(group.members.size());
        // Identity is checked once per entity per script frame; no yield occurs
        // inside this transaction. Avoid dozens of redundant native lookups.
        for(auto& s:group.members)if(s.id.Same())live.push_back(&s);
        // Hide/freeze EVERY member before moving ANY member. Keep collision intact.
        for(const auto* s:live)if(!ApplyIsolation(*s,"before-park"))return Decline(group,error);
        bool moved=false;
        for(const auto* s:live) {
            const auto e=s->id.entity;const auto p=ENTITY::GET_ENTITY_COORDS(e,true,false);
            if(!Close(p,s->parking)) {
                ENTITY::SET_ENTITY_COORDS_NO_OFFSET(e,s->parking,false,false,false);
                moved=true;
                if(!ApplyIsolation(*s,"after-park",true))return Decline(group,error);
            }
        }
        // Moving a later rider/passenger/draft member can reset an earlier
        // member too. Reassert the whole group before the final acceptance pass.
        if(moved)for(const auto* s:live)if(!ApplyIsolation(*s,"after-group",true))return Decline(group,error);
        for(auto* s:live) {
            const auto e=s->id.entity;
            const auto p=ENTITY::GET_ENTITY_COORDS(e,true,false);
            const auto state=IsolationState::Read(e);
            if(!state.Restrained())
                return Decline(group,"linked group changed final isolation state; entity="+std::to_string(e)+
                    " model="+std::to_string(s->id.model)+" "+state.Describe());
            // This geometric condition is mandatory even with collision-off:
            // the entire group must have arrived outside the WHOLE course box,
            // not merely have hidden flags or be clear of one nearby segment.
            if(!Close(p,s->parking) || InBounds(p,kBufferHalfWidth+10)) {
                return Decline(group,"off-course parking not acknowledged; entity="+std::to_string(e)+
                    " model="+std::to_string(s->id.model)+" actual="+std::to_string(p.x)+","+
                    std::to_string(p.y)+","+std::to_string(p.z)+" expected="+std::to_string(s->parking.x)+","+
                    std::to_string(s->parking.y)+","+std::to_string(s->parking.z));
            }
            s->parked=true;s->last_position=p;
        }
        return true;
    }
    bool Park(const std::set<Entity>& entities) {
        Group group;
        for(const auto e:entities)if(Exists(e) && !Racer(e) && !Held(e)) {
            Saved s;s.id=Identity::Read(e);s.home=ENTITY::GET_ENTITY_COORDS(e,true,false);
            s.last_position=s.home;
            s.heading=ENTITY::GET_ENTITY_HEADING(e);s.visible=ENTITY::IS_ENTITY_VISIBLE(e);
            s.collision_disabled=ENTITY::GET_ENTITY_COLLISION_DISABLED(e);s.frozen=ENTITY::_IS_ENTITY_FROZEN(e);
            s.player_owned=PlayerOwns(e);
            if(!destination::Finite(s.home) || !std::isfinite(s.heading)) {
                error="invalid traffic transform; entity="+std::to_string(e);return false;
            }
            group.members.push_back(s);
        }
        if(group.members.empty())return true;
        if(Count()+group.members.size()>4096){error="temporary isolation registry full";return false;}
        auto lo=group.members.front().home,hi=lo;
        for(const auto& s:group.members) {
            lo.x=std::min(lo.x,s.home.x);lo.y=std::min(lo.y,s.home.y);
            hi.x=std::max(hi.x,s.home.x);hi.y=std::max(hi.y,s.home.y);
        }
        // Common translation preserves a complete mounted/wagon group's layout.
        // Choose the nearest outside edge of the ENTIRE course's bounding box,
        // not a point that could land on another arm of the loop. Hidden frozen
        // parking needs no guessed ground height. Never release physics there
        // as part of an ordinary return; return home before unfreezing.
        const std::array<Vector3,4> choices{{{high_.x+70-lo.x,0,0},{low_.x-70-hi.x,0,0},
            {0,high_.y+70-lo.y,0},{0,low_.y-70-hi.y,0}}};
        const auto shift=*std::min_element(choices.begin(),choices.end(),[](auto a,auto b){
            return std::hypot(a.x,a.y)<std::hypot(b.x,b.y);});
        for(auto& s:group.members)s.parking={s.home.x+shift.x,s.home.y+shift.y,s.home.z};
        // Save rollback information BEFORE the first native write, including
        // writes that can fail. Never refresh the original state on later ticks.
        parked_.push_back(std::move(group));
        if(!Enforce(parked_.back()))return false;
        isolated_total_+=parked_.back().members.size();
        if(isolated_total_<=64) {
            const auto& s=parked_.back().members.front();
            Log("verified temporary isolation: entity="+std::to_string(s.id.entity)+
                " model="+std::to_string(s.id.model)+" members="+std::to_string(parked_.back().members.size())+
                " home="+std::to_string(s.home.x)+","+std::to_string(s.home.y)+","+std::to_string(s.home.z));
        }
        return true;
    }
    bool CanRestore(const Group& group,bool immediate) const {
        // A partial restore is already committed to this return location. Its
        // own returned members must not permanently block the occupancy query.
        if(group.restoring || immediate || CAM::IS_SCREEN_FADED_OUT())return true;
        const auto player=PLAYER::PLAYER_PED_ID();
        if(!Exists(player))return false;
        const auto p=ENTITY::GET_ENTITY_COORDS(player,true,false);
        for(const auto& s:group.members)if(s.id.Same()) {
            if(destination::Distance(s.home,p)<45 || CAM::IS_SPHERE_VISIBLE(s.home,6))return false;
            if(MISC::IS_POSITION_OCCUPIED(s.home.x,s.home.y,s.home.z+1,3,false,true,true,false,false,0,false))return false;
        }
        return true;
    }
    bool Restore(Group& group) {
        // Move the whole bundle home while still hidden/frozen. Refuse
        // to reveal a member whose transform native has not actually succeeded.
        group.restoring=true;
        if(!group.positions_restored) {
            for(auto& s:group.members)if(s.id.Same() && !s.home_returned) {
                ENTITY::SET_ENTITY_COORDS_NO_OFFSET(s.id.entity,s.home,false,false,false);
                ENTITY::SET_ENTITY_HEADING(s.id.entity,s.heading);
                s.last_position=ENTITY::GET_ENTITY_COORDS(s.id.entity,true,false);
                s.home_returned=Close(s.last_position,s.home);
            }
            for(const auto& s:group.members)if(s.id.Same() && !s.home_returned)return false;
            group.positions_restored=true;
        }
        for(const auto& s:group.members)if(s.id.Same()) {
            ENTITY::SET_ENTITY_COLLISION(s.id.entity,!s.collision_disabled,true);
            ENTITY::FREEZE_ENTITY_POSITION(s.id.entity,s.frozen);
            ENTITY::SET_ENTITY_VISIBLE(s.id.entity,s.visible);
        }
        for(const auto& s:group.members)if(s.id.Same() &&
            (bool(ENTITY::GET_ENTITY_COLLISION_DISABLED(s.id.entity))!=s.collision_disabled ||
             bool(ENTITY::_IS_ENTITY_FROZEN(s.id.entity))!=s.frozen ||
             bool(ENTITY::IS_ENTITY_VISIBLE(s.id.entity))!=s.visible))return false;
        return true;
    }
    bool HandBack(Saved& s,bool restoring) {
        if(!s.parked)return false; // Failed initial parking uses ordinary rollback.
        const auto e=s.id.entity;
        const auto p=ENTITY::GET_ENTITY_COORDS(e,true,false);
        const auto state=IsolationState::Read(e);
        // Compare with the last ACKNOWLEDGED pose, not the looser 2m tolerance
        // used to accept a parking native. A new script's small movement still
        // means we must not teleport its actor back to an old display slot.
        const bool displaced=!Close(p,s.last_position,.15F);
        const bool occupied=PlayerOccupies(e);
        const bool claimed=!s.player_owned && PlayerOwns(e);
        const bool released=!restoring && (state.visible || !state.frozen);
        if(!s.handed_back && !(displaced || occupied || claimed || released))return false;
        if(!s.handed_back)Log("native/player handover: entity="+std::to_string(e)+
            " displaced="+std::to_string(displaced)+" occupied="+std::to_string(occupied)+
            " newly_owned="+std::to_string(claimed)+" released="+std::to_string(released));
        s.handed_back=true;
        // A selection can change ownership before the stable teleports the
        // horse. Return a still-parked, unoccupied actor to its saved ground
        // location before releasing freeze. Never teleport a claimed actor
        // already moved by the game, or move the player with an occupied mount.
        if(!displaced && !occupied && !s.home_returned) {
            ENTITY::SET_ENTITY_COORDS_NO_OFFSET(e,s.home,false,false,false);
            ENTITY::SET_ENTITY_HEADING(e,s.heading);
            s.last_position=ENTITY::GET_ENTITY_COORDS(e,true,false);
            s.home_returned=Close(s.last_position,s.home);
            if(!s.home_returned)return false;
        }
        // Relinquish only our hide/freeze, preserving native reveal/unfreeze
        // choices. In particular, do not refreeze a newly selected stable horse
        // just because it happened to be frozen before the race.
        // Never switch collision OFF during handover, even if it was already
        // off before isolation: a newer native collision-enable takes priority.
        if(!s.collision_disabled && ENTITY::GET_ENTITY_COLLISION_DISABLED(e))ENTITY::SET_ENTITY_COLLISION(e,true,true);
        if(!s.frozen && ENTITY::_IS_ENTITY_FROZEN(e))ENTITY::FREEZE_ENTITY_POSITION(e,false);
        if(s.visible && !ENTITY::IS_ENTITY_VISIBLE(e))ENTITY::SET_ENTITY_VISIBLE(e,true);
        const auto after=IsolationState::Read(e);
        const bool acknowledged=(s.collision_disabled || !after.collision_off) &&
            (s.frozen || !after.frozen) && (!s.visible || after.visible);
        if(!acknowledged && !s.handover_reported) {
            s.handover_reported=true;
            Log("handover read-back pending: entity="+std::to_string(e)+" model="+
                std::to_string(s.id.model)+" at="+std::to_string(p.x)+","+
                std::to_string(p.y)+","+std::to_string(p.z)+" "+after.Describe());
        }
        return acknowledged;
    }
