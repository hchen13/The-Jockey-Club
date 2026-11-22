// Spectator animation preparation and physical placement.
// Included within the owning namespace/class; member access and order are preserved.
    static void RemoveAnimation(Spectator& s) {
        if(s.scene && ANIMSCENE::DOES_ANIM_SCENE_EXIST(s.scene)) {
            if(ANIMSCENE::IS_ANIM_SCENE_RUNNING(s.scene,false))ANIMSCENE::ABORT_ANIM_SCENE(s.scene,true);
            if(s.actor.Same())ANIMSCENE::REMOVE_ANIM_SCENE_ENTITY(s.scene,s.slot.c_str(),s.actor.ped);
            ANIMSCENE::_DELETE_ANIM_SCENE(s.scene);
        }
        s.scene=0;
    }
    static bool RequestClip(AnimScene scene,const std::string& name) {
        if(ANIMSCENE::_IS_ANIM_SCENE_PLAYBACK_LIST_PHASE_LOADED(scene,name.c_str()))return true;
        if(!ANIMSCENE::_IS_ANIM_SCENE_PLAYBACK_LIST_PHASE_LOADING(scene,name.c_str()))
            ANIMSCENE::REQUEST_ANIM_SCENE_PLAY_LIST(scene,name.c_str());
        return ANIMSCENE::_IS_ANIM_SCENE_PLAYBACK_LIST_PHASE_LOADED(scene,name.c_str());
    }
    void ReportSpectator(Spectator& s,double now,bool force=false) {
        if(!force && now<s.next_status)return;
        s.next_status=now+1;
        Log("audience status slot="+s.slot+" ped="+std::to_string(s.actor.ped)+" pending="+s.pending+
            " render="+std::to_string(s.render_ready)+" components="+std::to_string(s.components)+
            " running="+std::to_string(s.running)+" starts="+std::to_string(s.start_attempts)+
            " playlist="+s.startup);
    }
    bool PrepareClips(Spectator& s,double now) {
        // Generic streaming readiness is not proof that a named playlist exists.
        // Wait for scene metadata before inspecting its actual capability table.
        if(!ANIMSCENE::IS_ANIM_SCENE_METADATA_LOADED(s.scene,false) ||
            !ANIMSCENE::IS_ANIM_SCENE_LOADED(s.scene,true,false))return false;
        if(!s.clips_checked) {
            if(s.metadata_at<0)s.metadata_at=now;
            const bool enter=ANIMSCENE::_DOES_ANIM_SCENE_PLAY_LIST_EXIST(s.scene,"pl_Enter");
            const bool actor=ANIMSCENE::_DOES_ENTITY_WITH_ID_EXIST_IN_ANIM_SCENE(s.scene,s.slot.c_str());
            if(!enter || !actor) {
                // Allow metadata publication to settle, not an unbounded retry.
                if(now-s.metadata_at>=1)
                    error="audience metadata missing "+std::string(!enter?"pl_Enter":"actor slot")+": "+s.slot;
                return false;
            }
            // Exact candidate families from mudtown2::func_1841. None of these
            // optional variants is universal, including the unsuffixed pl_Idle.
            for(const char* name:{"pl_Idle","pl_Idle2","pl_Idle3"})
                if(ANIMSCENE::_DOES_ANIM_SCENE_PLAY_LIST_EXIST(s.scene,name))s.idles.emplace_back(name);
            for(const char* name:{"pl_React_Large","pl_React_Large2","pl_React_Small","pl_React_Small2"})
                if(ANIMSCENE::_DOES_ANIM_SCENE_PLAY_LIST_EXIST(s.scene,name))s.reactions.emplace_back(name);
            s.startup=s.idles.empty()?"pl_Enter":s.idles.front();
            s.clips_checked=true;
            std::string available;
            for(const auto* group:{&s.idles,&s.reactions})for(const auto& name:*group)available+=" "+name;
            Log("audience clips slot="+s.slot+" start="+s.startup+" available="+available+
                (s.idles.empty()?"; native Enter -> S_BASE fallback":""));
        }
        const bool ready=RequestClip(s.scene,s.startup);
        for(const auto* group:{&s.idles,&s.reactions})for(const auto& name:*group)RequestClip(s.scene,name);
        return ready;
    }
    bool Ground(Vector3 candidate,Vector3& result) {
        STREAMING::REQUEST_COLLISION_AT_COORD(candidate);
        float z=0;Vector3 normal{};
        if(!MISC::GET_GROUND_Z_AND_NORMAL_FOR_3D_COORD(candidate.x,candidate.y,candidate.z+3,&z,&normal) ||
            !std::isfinite(z) || normal.z<.94F || std::abs(z-candidate.z)>2.5F)return false;
        result={candidate.x,candidate.y,z+.05F};
        for(const auto offset:std::array<Vector3,4>{{{.4F,0,0},{-.4F,0,0},{0,.4F,0},{0,-.4F,0}}}) {
            float foot=0;
            if(!MISC::GET_GROUND_Z_AND_NORMAL_FOR_3D_COORD(result.x+offset.x,result.y+offset.y,result.z+1,&foot,&normal) ||
                std::abs(foot-z)>.3F || normal.z<.94F)return false;
        }
        return Nearest(result).distance>=circuit_exclusion::kLaneHalfWidth+3 && !MISC::IS_POSITION_OCCUPIED(result.x,result.y,result.z+1,.65F,
            false,true,true,false,false,0,false);
    }
    bool PlaceSpectator(Spectator& s,std::size_t index,double now) {
        // Main-street/sidewalk candidates are offsets from surveyed nodes, not
        // road-node snapping. Reject steep steps, gaps and occupied doorways.
        constexpr std::array<std::size_t,6> nodes{0,2,4,27,30,32};
        Vector3 position{},outward{};bool placed=false;
        for(int attempt=0;attempt<12 && !placed;++attempt) {
            const std::size_t i=std::min(nodes[(index/2+attempt/4)%nodes.size()],ring_.points.size()-2);
            const auto a=ring_.points[i],b=ring_.points[i+1];const float len=destination::Distance(a,b);
            const float side=(index%2?1.0F:-1.0F);
            outward={-(b.y-a.y)/len*side,(b.x-a.x)/len*side,0};
            const float offset=16.0F+(attempt%4)*1.5F;
            placed=Ground({a.x+outward.x*offset,a.y+outward.y*offset,a.z},position);
            for(const auto& other:crowd_)if(other.actor.ped && destination::Distance(position,other.position)<2)placed=false;
        }
        if(!placed)return false;
        const float heading=std::atan2(outward.x,-outward.y)*57.2957795F;
        auto p=PED::CREATE_PED(model_,position,heading,false,true,true,true);
        if(!Exists(p))return false;
        s.actor=Identity(p);s.position=position;s.outward=outward;s.next_reaction=now+index*1.3;
        s.placed_at=now;s.next_start=now+.25;
        PED::_SET_RANDOM_OUTFIT_VARIATION(p,true);PED::_UPDATE_PED_VARIATION(p,false,true,true,true,false);
        PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(p,true);
        PED::SET_PED_RELATIONSHIP_GROUP_HASH(p,MISC::GET_HASH_KEY("REL_CIVMALE"));
        // The SDK location pointer is a script matrix, not a single Vector3.
        // Reserve all four padded vectors; translate the native scene's authored
        // actor position to the checked sidewalk. No original mission is run.
        std::array<Vector3,4> transform{};
        ANIMSCENE::SET_ANIM_SCENE_ORIGIN(s.scene,0,0,0,0,0,0,2);
        if(!ANIMSCENE::GET_ANIM_SCENE_ENTITY_LOCATION_DATA(s.scene,s.slot.c_str(),transform.data(),false,s.startup.c_str(),2) ||
            !destination::Finite(transform[0]) || !destination::Finite(transform[1]))return false;
        const float rotation=heading-transform[1].z,angle=rotation/57.2957795F;
        const float x=transform[0].x*std::cos(angle)-transform[0].y*std::sin(angle);
        const float y=transform[0].x*std::sin(angle)+transform[0].y*std::cos(angle);
        s.scene_origin={position.x-x,position.y-y,position.z-transform[0].z};s.scene_rotation=rotation;
        ANIMSCENE::SET_ANIM_SCENE_ORIGIN(s.scene,s.scene_origin.x,s.scene_origin.y,s.scene_origin.z,0,0,rotation,2);
        ANIMSCENE::SET_ANIM_SCENE_ENTITY(s.scene,s.slot.c_str(),p,0);
        // Binding a new metaped is not proof that it can run an animation yet.
        // Native mudtown2 separates actor creation/readiness from func_1282's
        // not-running -> START check. Defer START to a later audited frame.
        return true;
    }
