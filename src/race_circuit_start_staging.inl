// Blackout rider placement and start staging.
// Included within the owning namespace/class; member access and order are preserved.
    bool Stage(Ped player,Ped rival) {return StageGroup({player,rival});}
    bool StageGroup(const std::vector<Ped>& riders) {
        if(!active_ || !CAM::IS_SCREEN_FADED_OUT())return false;
        if(!IdentitiesValid())return false;
        if(riders.size()!=horses_.size()){error="starting rider count changed";return false;}
        for(std::size_t i=0;i<riders.size();++i)if(!ENTITY::DOES_ENTITY_EXIST(riders[i]) ||
            ENTITY::IS_ENTITY_DEAD(riders[i]) || PED::GET_MOUNT(riders[i])!=horses_[i].ped) {
            error="starting riders no longer on their horses";return false;
        }
        Hold();
        if(HUD::IS_PAUSE_MENU_ACTIVE())return false;
        if(ready_)return true;
        if(!FindRoadGrid())return false;
        if(placed_) {
            const double dt=FrameTime();placed_time_+=dt;
            bool stable=true;
            for(std::size_t i=0;i<horses_.size();++i) {
                const auto horse=horses_[i].ped;
                const bool collision=ENTITY::HAS_COLLISION_LOADED_AROUND_ENTITY(horse) &&
                    !ENTITY::IS_ENTITY_WAITING_FOR_WORLD_COLLISION(horse);
                if(collision && !ground_placed_[i] && placed_time_>=next_ground_attempt_)
                    ground_placed_[i]=ENTITY::PLACE_ENTITY_ON_GROUND_PROPERLY(horse,false)!=0;
                const auto p=ENTITY::GET_ENTITY_COORDS(horse,true,false);
                const float speed=ENTITY::GET_ENTITY_SPEED(horse);
                const auto& slot=grid_.slots[i];
                const bool at_slot=destination::Finite(p) && std::hypot(p.x-slot.x,p.y-slot.y)<=1.0F &&
                    p.z>=slot.z-.1F && p.z<=slot.z+2.5F;
                // Ground z is not the entity origin. Validate actual post-native
                // physics while UNFROZEN; never compare root z to ground+.05.
                stable=stable && ground_placed_[i] && collision && at_slot &&
                    std::isfinite(speed) && speed<=.15F &&
                    !PED::IS_PED_FALLING(horse) && !PED::IS_PED_RAGDOLL(horse) &&
                    std::hypot(p.x-previous_[i].x,p.y-previous_[i].y,p.z-previous_[i].z)<=.025F;
                previous_[i]=p;
            }
            if(placed_time_>=next_ground_attempt_)next_ground_attempt_=placed_time_+.15;
            stable_time_=stable?stable_time_+dt:0;
            if(placed_time_>=next_report_){Report("settling");next_report_=placed_time_+2;}
            if(placed_time_>=5 && stable_time_<.35) {
                error="starting grid could not settle on loaded ground";Report("failed");return false;
            }
            if(placed_time_<.75 || stable_time_<.35)return false;
            // Freeze ONCE only after native placement and live physics agree.
            // Riders remain attached to their horses, never moved independently.
            anchors_=previous_;
            for(const auto& h:horses_) {
                ENTITY::SET_ENTITY_VELOCITY(h.ped,0,0,0);
                ENTITY::FREEZE_ENTITY_POSITION(h.ped,true);
            }
            held_=true;ready_=true;
            CAM::SET_GAMEPLAY_CAM_RELATIVE_HEADING(0,1);
            CAM::SET_GAMEPLAY_CAM_RELATIVE_PITCH(0,1);
            Report("ready");
            return true;
        }
        auto grounded=grid_.slots;
        std::string unsafe;
        for(std::size_t i=0;i<grounded.size();++i) {
            auto& p=grounded[i];
            STREAMING::REQUEST_COLLISION_AT_COORD(p);
            float z=0;Vector3 normal{};
            if(!MISC::GET_GROUND_Z_AND_NORMAL_FOR_3D_COORD(p.x,p.y,p.z+3,&z,&normal))return false;
            if(!std::isfinite(z) || !destination::Finite(normal) || std::abs(z-p.z)>2 || normal.z<.85F) {
                if(unsafe.empty()) {
                    char b[200];std::snprintf(b,sizeof b,"slot %u at %.2f,%.2f,%.2f ground_z=%.2f normal_z=%.2f",static_cast<unsigned>(i),
                        static_cast<double>(p.x),static_cast<double>(p.y),static_cast<double>(p.z),static_cast<double>(z),static_cast<double>(normal.z));
                    unsafe=b;
                }
                continue;
            }
            p.z=z;
        }
        if(!unsafe.empty()) {
            unsafe_time_+=FrameTime();
            if(!unsafe_logged_ && log_){log_("circuit grid unsafe ground: "+unsafe+"; retrying for "+std::to_string(static_cast<int>(kUnsafeGraceSeconds))+" s");unsafe_logged_=true;}
            if(unsafe_time_<kUnsafeGraceSeconds)return false;
            error="starting grid has unsafe ground ("+unsafe+")";return false;
        }
        unsafe_time_=0;
        grid_.slots=grounded;
        // Stop the rival's waiting task, not the player's mounted task tree.
        for(std::size_t i=1;i<riders.size();++i)TASK::TASK_STAND_STILL(riders[i],-1);
        placed_=true;
        for(std::size_t i=0;i<horses_.size();++i) {
            const auto p=grid_.slots[i];const Ped horse=horses_[i].ped;
            TASK::SET_PED_DESIRED_MOVE_BLEND_RATIO(horse,0);
            ENTITY::SET_ENTITY_VELOCITY(horse,0,0,0);
            // Same pair as native player_horse.c: coordinates with the entity's
            // own root offset, then native ground placement. clearArea=false:
            // the circuit's participant-aware isolation owns all traffic.
            ENTITY::SET_ENTITY_COORDS(horse,p,false,false,true,false);
            ENTITY::SET_ENTITY_HEADING(horse,grid_.heading);
            previous_[i]=ENTITY::GET_ENTITY_COORDS(horse,true,false);
            if(log_ && ENTITY::GET_ENTITY_COLLISION_DISABLED(horse))
                log_("circuit grid: horse "+std::to_string(horse)+" reached its slot with its collision already disabled");
        }
        Report("placed-unfrozen");return false;
    }
