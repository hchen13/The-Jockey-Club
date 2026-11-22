// Course spatial queries, bounded traffic discovery and isolation primitives.
// Included within the owning namespace/class; member access and order are preserved.
    static bool Exists(Entity e){return e && ENTITY::DOES_ENTITY_EXIST(e);}
    void Log(const std::string& s){if(log_)log_("circuit exclusion: "+s);}
    struct IsolationState {
        bool visible=false,collision_off=false,frozen=false;
        bool Restrained() const {return !visible && frozen;}
        std::string Describe() const {
            return "visible="+std::to_string(visible)+" collision_disabled="+
                std::to_string(collision_off)+" frozen="+std::to_string(frozen);
        }
        static IsolationState Read(Entity e) {
            return {bool(ENTITY::IS_ENTITY_VISIBLE(e)),bool(ENTITY::GET_ENTITY_COLLISION_DISABLED(e)),
                bool(ENTITY::_IS_ENTITY_FROZEN(e))};
        }
    };
    bool ApplyIsolation(const Saved& s,const char* phase,bool report_repair=false) {
        const auto e=s.id.entity;
        auto state=IsolationState::Read(e);
        if(state.Restrained())return true;
        const auto before=state;
        // Hide/freeze are prerequisites for moving the group without exposing
        // an intermediate state. Do NOT disable collision: spatial exclusion
        // plus freeze is sufficient. A stable/native script can reclaim, reveal
        // and unfreeze a horse between frames without repairing our collision
        // bit. Leaving collision off until a deferred return made it fall through
        // terrain (1.13.12). Enforce still verifies the off-course transform.
        for(int attempt=0;attempt<2 && !state.Restrained();++attempt) {
            if(state.visible)ENTITY::SET_ENTITY_VISIBLE(e,false);
            if(!state.frozen)ENTITY::FREEZE_ENTITY_POSITION(e,true);
            state=IsolationState::Read(e);
        }
        if(!state.Restrained()) {
            error="isolation flags not acknowledged after repair; phase="+std::string(phase)+
                " entity="+std::to_string(e)+" model="+std::to_string(s.id.model)+" "+state.Describe();
            return false;
        }
        if(report_repair && state.Restrained()) {
            ++flag_repairs_;
            if(flag_repairs_<=16)Log("post-move isolation repaired: phase="+std::string(phase)+
                " entity="+std::to_string(e)+" model="+std::to_string(s.id.model)+
                " before["+before.Describe()+"] after["+state.Describe()+"]");
        }
        return true;
    }
    static bool Close(Vector3 a,Vector3 b,float tolerance=2) {
        return destination::Finite(a) && destination::Finite(b) &&
            destination::Distance(a,b)<=tolerance && std::abs(a.z-b.z)<=tolerance;
    }
    void BuildRouteBlocks() {
        route_blocks_.clear();
        for(std::size_t path_index=0;path_index<paths_.size();++path_index) {
            const auto& points=paths_[path_index].points;
            if(points.size()<2)continue;
            for(std::size_t first=0;first+1<points.size();first+=kSegmentsPerBlock) {
                const auto last=std::min(first+kSegmentsPerBlock,points.size()-1);
                RouteBlock block{path_index,first,last,points[first],points[first]};
                for(std::size_t i=first+1;i<=last;++i) {
                    const auto p=points[i];
                    block.low.x=std::min(block.low.x,p.x);block.low.y=std::min(block.low.y,p.y);
                    block.low.z=std::min(block.low.z,p.z);
                    block.high.x=std::max(block.high.x,p.x);block.high.y=std::max(block.high.y,p.y);
                    block.high.z=std::max(block.high.z,p.z);
                }
                route_blocks_.push_back(block);
            }
        }
    }
    static float BlockLowerBound(const RouteBlock& block,Vector3 p) {
        const auto axis=[](float x,float low,float high){return std::max({low-x,0.0F,x-high});};
        const float dx=axis(p.x,block.low.x,block.high.x);
        const float dy=axis(p.y,block.low.y,block.high.y);
        // course::Project gives the track six metres' vertical tolerance. A
        // block bound must use the same allowance or it could prune a bridge.
        const float dz=std::max(0.0F,axis(p.z,block.low.z,block.high.z)-6.0F);
        return std::hypot(std::hypot(dx,dy),dz);
    }
    bool InBounds(Vector3 p,float margin=kBufferHalfWidth) const {
        return p.x>=low_.x-margin && p.x<=high_.x+margin && p.y>=low_.y-margin && p.y<=high_.y+margin;
    }
    bool ApproachingTrack(Entity e,Vector3 p,float distance) const {
        // Inside the actual course EVERY nonparticipant is removed, irrespective
        // of species, ownership, script or interior. The outer buffer is only
        // predictive: stationary stable/shop horses beside the track are not
        // traffic. Do not depend on potentially absent interior metadata.
        if(distance<=kTrackHalfWidth)return true;
        if(distance>kBufferHalfWidth)return false;
        const auto v=ENTITY::GET_ENTITY_VELOCITY(e,0);
        if(!destination::Finite(v))return false;
        for(int step=1;step<=4;++step) {
            const float t=step*.25F;
            if(Distance({p.x+v.x*t,p.y+v.y*t,p.z+v.z*t})<=kTrackHalfWidth)return true;
        }
        return false;
    }
    bool PlayerOwns(Entity e) const {
        const auto player=PLAYER::PLAYER_ID();
        return e==PLAYER::_GET_SADDLE_HORSE_FOR_PLAYER(player) || e==PLAYER::_GET_TEMP_PLAYER_HORSE(player) ||
            std::any_of(player_mounts_.begin(),player_mounts_.end(),[&](const Identity& id){return id.entity==e && id.Same();});
    }
    static bool PlayerOccupies(Entity e) {
        const auto player=PLAYER::PLAYER_PED_ID();
        return Exists(player) && (e==player || (PED::IS_PED_ON_MOUNT(player) && e==PED::GET_MOUNT(player)) ||
            e==PED::GET_VEHICLE_PED_IS_IN(player,false) || e==ENTITY::GET_ENTITY_ATTACHED_TO(player));
    }
    // Inside the racer window: what the native entity query is asked about.
    bool QueryBounds(Vector3 p) const {
        return p.x>=window_low_.x && p.x<=window_high_.x && p.y>=window_low_.y && p.y<=window_high_.y &&
            p.z>=window_low_.z && p.z<=window_high_.z;
    }
    void UpdateWindow() {
        bool any=false;Vector3 lo,hi;
        if(may_decline_)for(const auto& racer:racers_)if(racer.Same()) {   // Running: round the field.
            const auto p=ENTITY::GET_ENTITY_COORDS(racer.entity,true,false);
            if(!destination::Finite(p))continue;
            if(!any){lo=hi=p;any=true;continue;}
            lo.x=std::min(lo.x,p.x);lo.y=std::min(lo.y,p.y);lo.z=std::min(lo.z,p.z);
            hi.x=std::max(hi.x,p.x);hi.y=std::max(hi.y,p.y);hi.z=std::max(hi.z,p.z);
        }
        if(!any) {   // Preparing, or no racer readable this frame: the whole course, as before.
            window_low_=Vector3(low_.x-64,low_.y-64,low_.z-64);
            window_high_=Vector3(high_.x+64,high_.y+64,high_.z+64);return;
        }
        window_low_=Vector3(lo.x-kWindowMargin,lo.y-kWindowMargin,lo.z-kWindowHeight);
        window_high_=Vector3(hi.x+kWindowMargin,hi.y+kWindowMargin,hi.z+kWindowHeight);
    }
    bool NativePool(int kind,std::vector<Entity>& result) {
        // Same unfiltered entity query as abigail2_1::func_1865/1866, before
        // those scripts apply their own mission/population exclusions. Query
        // one complete box round the racers, NOT a limited set of nearby checkpoints.
        // This does not use ScriptHook's worldGetAll early exit when its script
        // GUID pool has <=256 free slots (verified in 1.0.1491.17).
        const ItemSet items=ITEMSET::CREATE_ITEMSET(true);
        if(!items || !ITEMSET::IS_ITEMSET_VALID(items)) {
            error="native traffic itemset unavailable; kind="+std::to_string(kind);return false;
        }
        const int count=ENTITY::_GET_ENTITIES_IN_VOLUME(query_volume_,items,kind);
        const int size=ITEMSET::GET_ITEMSET_SIZE(items);
        bool good=count>=0 && count==size && count<4096;
        std::set<Entity> unique;
        for(int i=0;good && i<count;++i) {
            const Entity e=static_cast<Entity>(ITEMSET::GET_INDEXED_ITEM_IN_ITEMSET(i,items));
            // Do not silently discard failed conversions and call the result
            // empty/clean. No script yield occurs during this snapshot.
            good=Exists(e) && ENTITY::GET_ENTITY_TYPE(e)==kind && unique.insert(e).second;
            if(good)result.push_back(e);
        }
        ITEMSET::DESTROY_ITEMSET(items);
        if(!good) {
            error="native traffic query invalid/incomplete; kind="+std::to_string(kind)+
                " count="+std::to_string(count)+" size="+std::to_string(size);return false;
        }
        window_.found+=count;
        return true;
    }
    bool Traffic(std::vector<Entity>& result) {
        window_={};
        UpdateWindow();
        const Vector3 centre((window_low_.x+window_high_.x)/2,(window_low_.y+window_high_.y)/2,(window_low_.z+window_high_.z)/2);
        const Vector3 size(window_high_.x-window_low_.x,window_high_.y-window_low_.y,window_high_.z-window_low_.z);
        // One volume for the race, moved and resized to the window each audit:
        // a volume is a script object with a GUID of its own, not something to
        // create every frame.
        if(!query_volume_ || !VOLUME::DOES_VOLUME_EXIST(query_volume_))
            query_volume_=VOLUME::_CREATE_VOLUME_BOX_WITH_CUSTOM_NAME(centre.x,centre.y,centre.z,0,0,0,size.x,size.y,size.z,"TJC_Circuit_Entity_Query");
        else {
            VOLUME::SET_VOLUME_COORDS(query_volume_,centre.x,centre.y,centre.z);
            VOLUME::SET_VOLUME_SCALE(query_volume_,size.x,size.y,size.z);
        }
        if(!query_volume_ || !VOLUME::DOES_VOLUME_EXIST(query_volume_)) {
            error="racer-window entity query volume unavailable";return false;
        }
        std::vector<Entity> native;
        if(!NativePool(2,native) || !NativePool(1,native))return false;
        const std::set<Entity> native_set(native.begin(),native.end());
        for(const auto& racer:racers_)if(racer.Same() && QueryBounds(ENTITY::GET_ENTITY_COORDS(racer.entity,true,false))) {
            ++window_.roster_expected;
            if(native_set.contains(racer.entity))++window_.roster_seen;
        }
        // Known, live in-bounds actors are the positive control for the scanner.
        // Zero outsiders is legitimate; failing to find even the known actors
        // means the query did not cover the scene, NOT that it was clean.
        if(!window_.Covered()) {
            error="native query missed known in-bounds race actors: "+std::to_string(window_.roster_seen)+
                "/"+std::to_string(window_.roster_expected);return false;
        }
        result.assign(native_set.begin(),native_set.end());
        bool train=false;
        std::erase_if(result,[&](Entity e){
            if(ENTITY::GET_ENTITY_TYPE(e)!=2 || !VEHICLE::IS_THIS_MODEL_A_TRAIN(ENTITY::GET_ENTITY_MODEL(e)))return false;
            train=true;return true;});
        if(train) {
            const int now=MISC::GET_GAME_TIMER();
            if(now-last_train_purge_>2000){last_train_purge_=now;VEHICLE::DELETE_ALL_TRAINS();Log("a train on the course: every train deleted");}
        }
        return true;
    }
    // A transform native can be refused for an entity another script owns, and
    // the read-back happens in the same frame with no yield, so a refusal is
    // indistinguishable from "it did not move yet". That used to abort the race:
    // one wagon near the New Austin finish declined its parking spot and
    // cancelled the race 80 m from the line, after four minutes of riding.
    //
    // Clearing the track is a presentation guarantee, not a safety one. The
    // safety line is the residual check in Tick(), which still aborts if
    // anything ends up within a lane-width of the racing line. Everything
    // outside that is worth a log entry, not a cancelled race.
