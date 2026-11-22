// Bounded native-road start grid search.
// Included within the owning namespace/class; member access and order are preserved.
    bool FindRoadGrid() {
        if(!road_ || road_ready_)return true;
        road_time_+=FrameTime();
        if(road_time_>4.5) {error="no verified clear road grid near pickup: "+road_rejection_;return false;}
        if(road_probes_[0] || road_probes_[1]) {
            bool blocked=false,pending=false;
            for(std::size_t i=0;i<road_probes_.size();++i)if(road_probes_[i]) {
                BOOL hit=false;Vector3 end{},normal{};Entity entity=0;
                const int state=SHAPETEST::GET_SHAPE_TEST_RESULT(road_probes_[i],&hit,&end,&normal,&entity);
                if(state==1){pending=true;continue;}
                road_probes_[i]=0;road_clear_[i]=state==2 && !hit;
                if(!road_clear_[i]){blocked=true;road_rejection_="runway obstacle or invalid shape result";}
            }
            if(pending)return false; // Keep ownership until completion, even when the query is slow.
            if(!blocked && !pending && road_clear_[0] && road_clear_[1]) {
                grid_=road_candidate_;road_ready_=true;
                if(log_)log_("circuit road grid verified: player="+std::to_string(grid_.slots[0].x)+","+
                    std::to_string(grid_.slots[0].y)+","+std::to_string(grid_.slots[0].z)+" rival="+
                    std::to_string(grid_.slots[1].x)+","+std::to_string(grid_.slots[1].y)+","+
                    std::to_string(grid_.slots[1].z)+" heading="+std::to_string(grid_.heading)+
                    "; both road/ground/runway checks passed; pickup/finish/recorded course unchanged");
                return true;
            }
            road_probes_={};road_clear_={};
        }
        const auto& path=road_->path;
        const float along=road_along_,spacing=road_width_?1.6F:2.2F;
        if(++road_width_==2){road_width_=0;road_along_+=4;}
        if(road_along_>road_->RoadStartLimit()-2)road_along_=12;
        const auto segment=path.Segment(along);const auto a=path.points[segment],b=path.points[segment+1];
        const float len=path.metres[segment+1]-path.metres[segment],t=(along-path.metres[segment])/len;
        const Vector3 sample{a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t};
        Vector3 node{};float heading=0;
        if(!PATHFIND::GET_CLOSEST_VEHICLE_NODE_WITH_HEADING(sample,&node,&heading,1,3,0) ||
            !destination::Finite(node) || !std::isfinite(heading) || destination::Distance(node,sample)>35 ||
            std::abs(node.z-sample.z)>4) {road_rejection_="nearby road node unavailable";return false;}
        const Vector3 forward{(b.x-a.x)/len,(b.y-a.y)/len,0};
        const float radians=heading*.01745329252F;
        Vector3 road_forward{-std::sin(radians),std::cos(radians),0};
        const float alignment=road_forward.x*forward.x+road_forward.y*forward.y;
        if(std::abs(alignment)<.85F){road_rejection_="road direction differs from opening course";return false;}
        // A road-node sample is not necessarily the closest point on its road.
        // Project locally along that road, then verify actual road membership.
        const float offset=(sample.x-node.x)*road_forward.x+(sample.y-node.y)*road_forward.y;
        Vector3 centre{node.x+road_forward.x*offset,node.y+road_forward.y*offset,sample.z};
        auto candidate=RoadGrid(centre,forward,road_swap_,spacing);
        if(!RoadGridWithin(*road_,candidate)){road_rejection_="road grid outside equal-progress staging corridor";return false;}
        std::array<Vector3,2> rear{},front{};
        for(std::size_t i=0;i<2;++i) {
            auto& slot=candidate.slots[i];
            rear[i]={slot.x-forward.x*1.5F,slot.y-forward.y*1.5F,slot.z};
            front[i]={slot.x+forward.x*8,slot.y+forward.y*8,slot.z+(b.z-a.z)*8/len};
            for(auto* point:{&slot,&rear[i],&front[i]}) {
                STREAMING::REQUEST_COLLISION_AT_COORD(*point);
                float z=0;Vector3 normal{};
                if(!PATHFIND::IS_POINT_ON_ROAD(*point,0) ||
                    !MISC::GET_GROUND_Z_AND_NORMAL_FOR_3D_COORD(point->x,point->y,point->z+3,&z,&normal) ||
                    !std::isfinite(z) || !destination::Finite(normal) || std::abs(z-point->z)>3 || normal.z<.85F) {
                    road_rejection_="road width/ground/runway unavailable";return false;
                }
                point->z=z;
                if(!road_->StrictProjection(*point,0,road_->gates.front().along-2).valid) {
                    road_rejection_="runway leaves recorded corridor";return false;
                }
            }
        }
        if(!RoadGridWithin(*road_,candidate)){road_rejection_="grounded grid left staging corridor";return false;}
        road_candidate_=candidate;road_clear_={};
        // Full collision mask: terrain, tree trunks, props and physical actors.
        // Tests are asynchronous, black-screen only, and never treated as clear
        // until both return COMPLETED/no-hit. The radius stays above road ground.
        for(std::size_t i=0;i<2;++i) {
            rear[i].z+=1.2F;front[i].z+=1.2F;
            road_probes_[i]=SHAPETEST::START_SHAPE_TEST_CAPSULE(rear[i],front[i],.65F,511,horses_[i].ped,7);
        }
        if(!road_probes_[0] || !road_probes_[1])road_rejection_="runway shape query unavailable";
        return false;
    }
