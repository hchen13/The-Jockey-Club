// Blackout preparation, closure audit and reveal sequencing.
// Included within the owning namespace/class; member access and order are preserved.
    Preparation Prepare(double now,const std::function<bool()>& before_reveal={},const std::function<bool()>& after_blackout={}) {
        if(phase_==Phase::Idle || !error.empty())return Preparation::Failed;
        if(!pending_traffic_paths_.empty()) {
            if(traffic_.Begin(pending_traffic_paths_,racers_,log_,allow_wolves_)) {
                pending_traffic_paths_.clear();
                Log("on-course handover complete; resuming verified track preparation");
            } else {
                if(traffic_.error!="native/player handover still pending" || now-phase_at_>=15) {
                    error=traffic_.error;return Preparation::Failed;
                }
                return Preparation::Waiting;
            }
        }
        // The final blackout audit has already proved the lane clear. Ambient
        // scripts may reclaim a parked bystander during the visible fade or
        // countdown; handle that like live-race traffic instead of cancelling
        // an accepted race after the screen has come back.
        if((phase_==Phase::FadingIn || phase_==Phase::Ready) && !GuardTraffic(now,true))return Preparation::Failed;
        // A bookmaker show registers ten mounted pairs during blackout. Their
        // metaped component audits can take longer than a two-rider grid;
        // preserve the established eight-second limit for smaller races.
        const double phase_timeout=phase_==Phase::Loading?15:(racers_.size()>10?20:8);
        if(phase_!=Phase::Ready && now-phase_at_>phase_timeout){
            if(phase_==Phase::Building)for(auto& s:crowd_)ReportSpectator(s,now,true);
            error=std::string("preparation timeout: ")+PhaseName();return Preparation::Failed;
        }
        if(phase_==Phase::Loading) {
            if(spectators_ && !STREAMING::HAS_MODEL_LOADED(model_))return Preparation::Waiting;
            bool ready=true;
            for(auto& s:crowd_) {
                if(!PrepareClips(s,now))ready=false;
                if(!error.empty())return Preparation::Failed;
            }
            if(!ready)return Preparation::Waiting;
            if(adopt_fade_) {
                adopt_fade_=false;phase_=Phase::FadingOut;phase_at_=now;
                Log("assets ready; the blackout already on screen is this scene's from here");
            } else if(CAM::IS_SCREEN_FADED_OUT() || CAM::IS_SCREEN_FADING_OUT()){error="another screen transition in progress";return Preparation::Failed;}
            else {
                CAM::DO_SCREEN_FADE_OUT(500);fade_owned_=true;phase_=Phase::FadingOut;phase_at_=now;
                blackout_audited_=false;
                Log("assets ready; starting 500ms fade-out");
            }
        } else if(phase_==Phase::FadingOut) {
            if(!CAM::IS_SCREEN_FADED_OUT())return Preparation::Waiting;
            // Once, not once a frame. The roster's readiness spans frames, and the whole course was
            // being swept on each of them - 1,643 audit lines over 85 preparations (code review,
            // 2026-09-21). Whatever arrives while the roster fills is caught by the final pass below,
            // which is the one that has to be right.
            if(!blackout_audited_) {
                const auto audit=AuditBlackout(now);
                if(audit!=Preparation::Ready)return audit;
                blackout_audited_=true;
            }
            // Spawn and register the additional entrants only under our owned
            // blackout. Their readiness may span frames; never isolate them as traffic.
            if(after_blackout && !after_blackout())return Preparation::Waiting;
            cleared_=true;
            int left=0,right=0;
            for(std::size_t i=0;i<crowd_.size();++i) {
                if(PlaceSpectator(crowd_[i],i,now)) {if(i%2)++right;else ++left;continue;}
                auto& s=crowd_[i];RemoveAnimation(s);
                if(s.actor.Same()){Ped p=s.actor.ped;PED::DELETE_PED(&p);}s.actor={};
                Log("skipped unsafe audience placement index="+std::to_string(i));
            }
            std::erase_if(crowd_,[](const Spectator& s){return !s.actor.ped;});
            if(spectators_ && (left<2 || right<2))Log("audience reduced to safe positions; left="+std::to_string(left)+
                " right="+std::to_string(right)+"; does not cancel a verified clear circuit");
            phase_=Phase::Building;phase_at_=now;
            Log("blackout audience placed; left="+std::to_string(left)+" right="+std::to_string(right)+"; verifying render/animation");
        } else if(phase_==Phase::Building) {
            bool ready=now-phase_at_>=.5;
            for(auto& s:crowd_) {
                if(!s.actor.Same() || ENTITY::IS_ENTITY_DEAD(s.actor.ped)){error="audience actor invalid";return Preparation::Failed;}
                s.render_ready=PED::IS_PED_READY_TO_RENDER(s.actor.ped);
                s.components=s.render_ready?PED::_GET_NUM_COMPONENTS_IN_PED(s.actor.ped):0;
                s.running=ANIMSCENE::IS_ANIM_SCENE_RUNNING(s.scene,false);
                if(!s.render_ready || s.components<2 || s.components>128 || now-s.placed_at<.25) {
                    s.pending=!s.render_ready?"metaped render":(s.components<2 || s.components>128?"metaped components":"post-creation frame");
                    ready=false;ReportSpectator(s,now);continue;
                }
                if(!s.running) {
                    s.pending="animation running acknowledgement";
                    if(now>=s.next_start && s.start_attempts<6) {
                        // The void START call can be ignored while engine-side
                        // actor binding is settling. Retry only while NOT running;
                        // never restart a playing scene or reset its playlist.
                        if(!s.start_attempts)ANIMSCENE::SET_ANIM_SCENE_PLAY_LIST(s.scene,s.startup.c_str(),true);
                        ANIMSCENE::START_ANIM_SCENE(s.scene);
                        ++s.start_attempts;s.next_start=now+.5;
                    }
                    ready=false;ReportSpectator(s,now);continue;
                }
                if(s.idles.empty() && !ANIMSCENE::IS_ANIM_SCENE_IN_SECTION(s.scene,"S_BASE",true)) {
                    s.pending="entry S_BASE";ready=false;ReportSpectator(s,now);continue;
                }
                s.pending="sidewalk position";
                const auto position=ENTITY::GET_ENTITY_COORDS(s.actor.ped,true,false);
                if(!destination::Finite(position)){error="audience non-finite position: "+s.slot;return Preparation::Failed;}
                if(destination::Distance(position,s.position)>2 || std::abs(position.z-s.position.z)>1.5F) {
                    // Entry/idle variants can have different authored offsets.
                    // Re-anchor once, only while our screen is fully black and
                    // the entry has reached its native stationary base. Never
                    // teleport a visible spectator or relax the final sidewalk check.
                    if(!s.reanchored && CAM::IS_SCREEN_FADED_OUT() && destination::Distance(position,s.position)<=30 &&
                        std::abs(position.z-s.position.z)<=3) {
                        s.scene_origin={s.scene_origin.x+s.position.x-position.x,s.scene_origin.y+s.position.y-position.y,
                            s.scene_origin.z+s.position.z-position.z};
                        ANIMSCENE::SET_ANIM_SCENE_ORIGIN(s.scene,s.scene_origin.x,s.scene_origin.y,s.scene_origin.z,0,0,s.scene_rotation,2);
                        s.reanchored=true;s.reanchor_at=now;ready=false;
                        Log("audience re-anchored under blackout slot="+s.slot);continue;
                    }
                    if(s.reanchored && now-s.reanchor_at<.5){ready=false;continue;}
                    error="authored audience animation moved outside sidewalk: "+s.slot;return Preparation::Failed;
                }
                if(s.reanchored && now-s.reanchor_at<.5){ready=false;s.pending="position settling";}
                else s.pending="ready";
                ReportSpectator(s,now);
            }
            if(ready){
                if(before_reveal && !before_reveal())return Preparation::Waiting;
                // Models/scenarios can finish streaming while the audience is
                // binding. Do not reveal new ordinary traffic that arrived
                // after the first blackout pass several frames earlier.
                const auto audit=AuditBlackout(now);
                if(audit!=Preparation::Ready)return audit;
                CAM::DO_SCREEN_FADE_IN(500);phase_=Phase::FadingIn;phase_at_=now;
                Log(spectators_?"audience verified; final traffic audit passed; starting 500ms fade-in":
                    "no-audience grid verified; final traffic audit passed; starting 500ms fade-in");
            }
        } else if(phase_==Phase::FadingIn && CAM::IS_SCREEN_FADED_IN()) {
            fade_owned_=false;phase_=Phase::Ready;Log("prepared, visible; race may now start");return Preparation::Ready;
        } else if(phase_==Phase::Ready)return Preparation::Ready;
        return Preparation::Waiting;
    }
    // Called before Prepare() reaches its fade: the screen is already black and
    // this scene owns it from this moment - Finish() lifts it whether the race
    // goes on to start or falls over first. (An abort between the hand-over
    // and the fade step left the player in the dark on 2026-09-17.)
