// Native task identity, ordered chunk selection, recovery and pace protection.
// Included within the owning namespace/class; member access and order are preserved.
inline std::function<void(const std::string&)> diagnostic_log;
// beat_horse_race.c:11049 uses this form with an explicit recording index.
// Its ordinary 67139080 entry selected node 116/119 at Karen's start in the
// 2026-09-10 live capture. Do not use that whole-route entry on overlapping laps.
inline constexpr int kFlags=26120;
// The native starts a recording at the waypoint NEAREST the ped, whatever
// index it is handed (Karen's start read node 116 of 119 on 2026-09-13 with
// entry 1: the third pass of the ring ends where the first begins, and the
// grid stands nearer to it), and it can snap to the recording's last node
// when the rider comes within some tens of metres of it, even if the route
// still has a pass to run. So a recording is registered one CHUNK at a
// time: from the entry node up to the node before the line first comes
// back within kRevisitMetres of a point it passed more than kRevisitAlong
// earlier. Within a chunk no two waypoints share a place, so the nearest
// one is the right one; when the rider is kChunkTail nodes from the
// chunk's end the caller registers the next chunk from where the rider is.
// A course that never revisits itself is one chunk: nothing changes for it.
inline constexpr float kRevisitMetres=15,kRevisitAlong=80; // 15 m: two passes of one road, however their lines differ; not a switchback 25 m below.
// The task snaps to a recording's last node once the ped is within a few
// tens of metres of it (41 m before the finish on 2026-09-13). So a chunk
// ends at least kOriginMetres before the line returns to the rider's own
// place - the grid must not lie within the snap of the chunk's last node -
// and a readback at the last kChunkTail nodes is the chunk's end, not a
// judgement of the section: the caller registers the next chunk.
inline constexpr float kOriginMetres=50;
inline constexpr int kChunkTail=2;
// A native recording holds 1024 nodes at most (race_course_native.h) and the
// line a rival rides may hold three thousand (the 4 m line over the 11 km
// camp duel, course::kMaxNodes, 2026-09-20): a chunk is also cut at
// kChunkNodes nodes and handed over like a revisit. The hand-over is due
// within kChunkTailMetres of the chunk's last node as well as within
// kChunkTail nodes of it: on a 4 m line two nodes are eight metres, inside
// the snap the task makes to a recording's last node (41 m on 2026-09-13).
inline constexpr std::size_t kChunkNodes=600;
inline constexpr float kChunkTailMetres=40;
// The native's progress readback can step back a node and come forward
// again; on a 17 m line that was a lap-scale fault, on a 4 m line it is a
// four-metre jitter, and it stopped the host for three seconds at 2.07 km on
// 2026-09-20 (Regressed, task restarted). A step back within kRegressMetres
// of the furthest accepted progress is not a regression: nothing is judged,
// the accepted progress stands, the effort is still written.
inline constexpr float kRegressMetres=20;   // 12 still failed twice in the 15:59 race of 2026-09-20 (four nodes back near a chunk's end, and at the finish).
enum class Status { Idle,Starting,Running,Fallback };
enum class Failure { None,InvalidInput,IdentityChanged,InvalidIndex,WrongSection,Regressed,NotStarted,Stopped,MissedWaypoint };
inline const char* FailureName(Failure why) {
    switch(why) {
    case Failure::InvalidInput:return "invalid-input";
    case Failure::IdentityChanged:return "identity-changed";
    case Failure::InvalidIndex:return "invalid-index";
    case Failure::WrongSection:return "wrong-route-section";
    case Failure::Regressed:return "progress-regressed";
    case Failure::NotStarted:return "playback-not-started";
    case Failure::Stopped:return "playback-stopped";
    case Failure::MissedWaypoint:return "missed-waypoint";
    default:return "none";
    }
}
// Task completion/cancellation is a navigation event, never a sporting foul.
// Short, overlapping circuits routinely end their native chunk early. Resume
// from QUALIFIED progress just as for a stale native node.
inline bool Retryable(Failure why) {
    return why==Failure::WrongSection || why==Failure::Regressed || why==Failure::InvalidIndex ||
        why==Failure::Stopped || why==Failure::NotStarted || why==Failure::MissedWaypoint;
}
struct Fault {
    Failure reason=Failure::None;
    double time=0;
    int rider=0,mount=0,entry=-1,progress=-1,native_index=-1,chunk_end=-1;
    float along=0,speed=0,effort=0,gait=0;
    Vector3 position{};
    bool playing=false,collision_disabled=false,static_entity=false;
    std::string recording;
    int next=-1;
    Vector3 goal{};
    std::string Detail() const {
        return " reason="+std::string(FailureName(reason))+" at="+std::to_string(time)+
            " rider="+std::to_string(rider)+" horse="+std::to_string(mount)+
            " entry="+std::to_string(entry)+" progress="+std::to_string(progress)+
            " native_index="+std::to_string(native_index)+" chunk_end="+std::to_string(chunk_end)+
            " along_m="+std::to_string(along)+" speed_mps="+std::to_string(speed)+
            " effort="+std::to_string(effort)+" gait="+std::to_string(gait)+
            " playing="+std::to_string(playing)+" collision_disabled="+std::to_string(collision_disabled)+
            " static="+std::to_string(static_entity)+" xyz="+std::to_string(position.x)+","+
            std::to_string(position.y)+","+std::to_string(position.z)+" next="+std::to_string(next)+
            " goal="+std::to_string(goal.x)+","+std::to_string(goal.y)+","+std::to_string(goal.z)+" recording="+recording;
    }
};
// Three retries are a LOCAL recovery budget, not a lifetime allowance for a
// 15-20 lap race. A rider who has made 100 m of qualified forward progress
// since the last restart has demonstrably recovered and gets a fresh budget.
struct RetryBudget {
    int consecutive=0,total=0;
    float last_along=0;
    bool Available(float along) {
        if(consecutive && std::isfinite(along) && along-last_along>=100.0F)consecutive=0;
        return consecutive<3;
    }
    void Record(float along) {++consecutive;++total;last_along=along;}
};
// Native navigation is renewable. A task ending, a stale readback and a
// motionless mounted rider are NOT elimination reasons. Rate-limit a refused
// task without a finite race/lap-wide retry limit; make real forward progress
// the watchdog, not the number of task calls made on earlier laps.
struct Recovery {
    double issued_at=-1,progress_at=-1,forward_at=-1;
    float anchor=0;
    int renewals=0;
    // Outside Follower: Start/Reset must never erase the triggering evidence.
    Fault last_fault;
    unsigned faults=0;
    bool fault_pending=false;
    bool propulsion_suspended=false; // missed next; clear only on actual healthy playback
    native_passing::Session passing;
    vehicle_clearance::NativeGuard vehicles;
    double vehicle_log_at=-1;
    void Observe(float along,double now) {
        if(progress_at<0 || now<progress_at || along>=anchor+3) {anchor=along;progress_at=now;}
    }
    bool Creeping(double now) const {return progress_at>=0 && now-progress_at>=4.0;}
    bool Ready(double now,bool chunk) const {return chunk || issued_at<0 || now-issued_at>=.5;}
    void Issued(float along,double now) {issued_at=progress_at=now;anchor=along;++renewals;}
};
inline void ProtectVehiclePace(Recovery& recovery,race_pace::Command& command,
    float along,Vector3 position,double now,Ped horse,const race_pace::Plan* plan=nullptr,float simulated_speed=-1) {
    if(plan && plan->path.points.size()>=2 && horse) {
        if(recovery.vehicles.Update(horse,*plan,along,position,now,simulated_speed)) {
            const float cap=recovery.vehicles.Ceiling();
            const float speed=std::max(ENTITY::GET_ENTITY_SPEED(horse),simulated_speed);
            if(cap<=0 || command.target_mps>cap || speed>cap) {
                command.vehicle_limited=command.braking=true;
                command.vehicle_blocked=cap<=0;
                command.target_mps=std::min(command.target_mps,cap);
                command.effort=std::min(command.effort,std::clamp(3.F*cap/15.F,0.F,3.F));
            }
        }
        const auto& guard=recovery.vehicles;
        if(diagnostic_log && (guard.changed || (guard.decision.blocked &&
            (recovery.vehicle_log_at<0 || now-recovery.vehicle_log_at>=2)))) {
            recovery.vehicle_log_at=now;
            const auto& observed=guard.decision.observation;
            diagnostic_log("vehicle clearance horse="+std::to_string(horse)+
                " hold="+std::to_string(command.vehicle_blocked)+
                " restricted="+std::to_string(guard.decision.blocked)+
                " ceiling_mps="+std::to_string(guard.Ceiling())+
                " clearing="+std::to_string(guard.decision.clearing)+
                " vehicle="+std::to_string(guard.blocking_vehicle)+
                " model="+std::to_string(guard.blocking_model)+
                " distance_m="+std::to_string(observed.distance)+
                " horizon_m="+std::to_string(observed.lookahead)+
                " known="+std::to_string(observed.known)+
                " vehicle_xyz="+std::to_string(guard.vehicle_position.x)+","+
                    std::to_string(guard.vehicle_position.y)+","+std::to_string(guard.vehicle_position.z)+
                " vehicle_velocity="+std::to_string(guard.vehicle_velocity.x)+","+
                    std::to_string(guard.vehicle_velocity.y)+","+std::to_string(guard.vehicle_velocity.z));
        }
    }
}
// Project only around already-qualified progress. A spatial nearest-point
// search over all laps cannot distinguish the first departure from the finish.
inline std::size_t EntryIndex(const course::Path& line,float qualified_along,Vector3 position,
    const std::vector<float>* reference=nullptr,float minimum_ahead=0) {
    // qualified_along is measured on the recorded centreline. An optimised
    // racing line is shorter: searching its own path.metres picks nodes a bend
    // ahead (Tennessee's 930 m restart jumped from node 165 to 175), steering
    // straight through the 8 m boundary. Project in the SAME units as the
    // progress tracker and the follower's section validation.
    const auto& metres=reference && reference->size()==line.points.size()?*reference:line.metres;
    const auto local=course::Project(line,position,std::max(0.F,qualified_along-5),qualified_along+25,&metres);
    const float along=local.valid?local.along:qualified_along;
    // A mount advanced by off-screen simulation must not retrieve a waypoint
    // just behind its new position when physics wakes. Keep the projection in
    // qualified route order, but start the new recording strictly ahead.
    if(minimum_ahead>0) {
        const auto ahead=std::upper_bound(metres.begin(),metres.end(),along+minimum_ahead);
        return std::min(line.points.size()-1,static_cast<std::size_t>(ahead-metres.begin()));
    }
    const auto it=std::upper_bound(metres.begin(),metres.end(),along);
    auto first=std::min(line.points.size()-2,static_cast<std::size_t>(std::max<std::ptrdiff_t>(0,it-metres.begin()-1)));
    if(along>metres[first]+.5F)++first;
    return first;
}
// The last node of the chunk that starts at `first`: the node before the
// line first returns to within kRevisitMetres of a point it passed more
// than kRevisitAlong earlier in this chunk, or of the rider's own place
// (`origin`: the grid stands BEHIND the first node, on the previous pass's
// last metres, and those nodes would be the nearest of all). At least first+1.
inline std::size_t ChunkEnd(const course::Path& line,std::size_t first,const Vector3* origin=nullptr) {
    const std::size_t n=line.points.size();
    if(n<2)return 0;
    if(first+1>=n)return n-1;
    for(std::size_t j=first+2;j<n;++j) {
        if(j-first+1>kChunkNodes)return j-1;
        const auto& p=line.points[j];
        if(origin && line.metres[j]-line.metres[first]>kRevisitAlong &&
            std::hypot(p.x-origin->x,p.y-origin->y)<kOriginMetres)return j-1;
        for(std::size_t k=first;k<j;++k) {
            if(line.metres[j]-line.metres[k]<=kRevisitAlong)break;
            if(std::hypot(p.x-line.points[k].x,p.y-line.points[k].y)<kRevisitMetres)return j-1;
        }
    }
    return n-1;
}
