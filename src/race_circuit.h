#pragma once
#include "race_course.h"
#include "race_lap_view.h"
#include "race_boundary.h"
#include <array>
#include <span>
#include <string>

// Offline-tested preparation for a surveyed circuit. Not connected to a live
// route until that route and its required road crossings have been reviewed.
namespace horse_races::circuit {
inline constexpr int kMaxLaps=20;
struct Gate {
    Vector3 centre{}, forward{};
    float along=0, half_width=14, height=6;
    std::optional<float> Crossing(Vector3 from,Vector3 to) const {
        if(!destination::Finite(from) || !destination::Finite(to))return {};
        const auto side=[&](Vector3 p){return (p.x-centre.x)*forward.x+(p.y-centre.y)*forward.y;};
        const float a=side(from),b=side(to);
        // Standing on the line or crossing backwards is not a forward crossing.
        if(!(a<0 && b>=0) || b-a<.0001F)return {};
        const float t=-a/(b-a);
        const Vector3 p{from.x+(to.x-from.x)*t,from.y+(to.y-from.y)*t,from.z+(to.z-from.z)*t};
        const float lateral=std::abs((p.x-centre.x)*forward.y-(p.y-centre.y)*forward.x);
        if(lateral>half_width || std::abs(p.z-centre.z)>height)return {};
        return t;
    }
};
struct Layout {
    struct PhysicalLap {std::size_t end_gate=0;float low=0,high=0;};
    course::Path path;
    std::vector<Gate> gates; // Ordered intermediate gates, then the lap line.
    // Independently surveyed laps can overlap without having identical edges.
    // A different copy must neither widen this lap nor impersonate a shortcut.
    std::vector<PhysicalLap> physical_laps;
    float strict_half_width=0,strict_height=6; // Production circuits always configure a positive width.
    bool destination_finish=false; // Only the terminal Charles arrival, never an intermediate lap.
    bool road_start=false; // Explicit per-route staging, never a relaxation of racing bounds.
    float RoadStartLimit() const {return gates.empty()?0:std::min(60.0F,gates.front().along-15.0F);}
    bool Build(const std::vector<Vector3>& points,const std::vector<std::size_t>& required,
               float width=14,float height=6,bool distinct_finish=false) {
        path={};gates.clear();physical_laps.clear();destination_finish=road_start=false;strict_half_width=0;strict_height=6;
        if(!std::isfinite(width) || width<5 || width>30 || !std::isfinite(height) || height<2 || height>10 ||
            required.size()<3 || required.size()>4U*kMaxLaps)return false;
        course::Path checked;
        if(!checked.Build(points) || checked.Length()<200 || checked.Length()>20000 ||
            (!distinct_finish && (destination::Distance(points.front(),points.back())>.1F ||
            std::abs(points.front().z-points.back().z)>.25F)))return false;
        std::size_t previous=0;
        std::vector<Gate> prepared;
        auto add=[&](std::size_t node) {
            if(node<=previous || node>=points.size() || checked.metres[node]-checked.metres[previous]<30)return false;
            const auto a=points[node-1],b=points[node];
            const float length=destination::Distance(a,b);
            prepared.push_back({b,{(b.x-a.x)/length,(b.y-a.y)/length,0},checked.metres[node],width,height});
            previous=node;return true;
        };
        for(auto node:required)if(node>=points.size()-1 || !add(node))return false;
        if(!add(points.size()-1))return false;
        path=std::move(checked);gates=std::move(prepared);destination_finish=distinct_finish;
        physical_laps.push_back({gates.size(),0,path.Length()});return true;
    }
    bool SetPhysicalLaps(std::span<const std::size_t> nonfinal_gate_ends) {
        if(!Ready())return false;
        std::vector<PhysicalLap> prepared;std::size_t previous=0;float low=0;
        for(auto end:nonfinal_gate_ends) {
            if(end<=previous||end>=gates.size())return false;
            const float high=gates[end-1].along;
            prepared.push_back({end,low,high});previous=end;low=high;
        }
        prepared.push_back({gates.size(),low,path.Length()});physical_laps=std::move(prepared);return true;
    }
    PhysicalLap LapForGate(std::size_t next_gate) const {
        for(const auto& lap:physical_laps)if(next_gate<lap.end_gate)return lap;
        return {gates.size(),0,path.Length()};
    }
    bool Ready() const {return path.points.size()>=5 && gates.size()>=4;}
    // The starting grid stands BEHIND the first node - three rows of it at New
    // Austin, 13 m back. Measured to the first node that reads as 13 m off the
    // course, so an 8 m corridor disqualified the back row on the first sweep.
    // Behind the start the corridor is the first segment's line extended.
    static constexpr float kStagingDepth=40;
    void ProjectSegment(course::Projection& best,Vector3 position,std::size_t i,float low,float high) const {
        const auto a=path.points[i],b=path.points[i+1];const float dx=b.x-a.x,dy=b.y-a.y,length2=dx*dx+dy*dy;
        if(!(length2>0))return;
        const float t=std::clamp(((position.x-a.x)*dx+(position.y-a.y)*dy)/length2,0.F,1.F);
        const float along=path.metres[i]+t*(path.metres[i+1]-path.metres[i]);
        if(along<low||along>high)return;
        const float horizontal=std::hypot(position.x-a.x-t*dx,position.y-a.y-t*dy);
        const float height=std::max(0.F,std::abs(position.z-a.z-t*(b.z-a.z))-6.F);
        const float distance=std::hypot(horizontal,height);
        if(distance<best.distance)best={along,distance,i,true};
    }
    course::Projection ProjectLocal(Vector3 position,float low,float high) const {
        course::Projection projected;
        if(path.points.size()<2||!destination::Finite(position)||low>high)return projected;
        auto first=path.Segment(std::max(0.F,low));
        if(first&&path.metres[first]==low)--first; // Include the preceding endpoint exactly as Project does.
        const auto last=path.Segment(std::min(path.Length(),high));
        for(auto i=first;i<=last;++i)ProjectSegment(projected,position,i,low,high);
        return projected;
    }
    course::Projection Qualify(Vector3 position,course::Projection projected,bool staging) const {
        // On a loop the ground behind the start also lies beside the closing
        // segment, so the nearest projection may not be the first node at all;
        // the staging lateral is taken whenever it is the smaller distance.
        if(staging && path.points.size()>=2) {
            const auto a=path.points[0],b=path.points[1];
            const float dx=b.x-a.x,dy=b.y-a.y,len=std::hypot(dx,dy);
            if(len>.01F) {
                const float ux=dx/len,uy=dy/len,rx=position.x-a.x,ry=position.y-a.y,t=rx*ux+ry*uy;
                const float lateral=std::abs(rx*uy-ry*ux);
                if(t<0 && t>-kStagingDepth && (!projected.valid || lateral<projected.distance))
                    projected={0,lateral,0,true};
            }
        }
        if(!projected.valid || projected.distance>strict_half_width)return {};
        const auto i=projected.segment;
        const float t=(projected.along-path.metres[i])/(path.metres[i+1]-path.metres[i]);
        const float z=path.points[i].z+t*(path.points[i+1].z-path.points[i].z);
        if(std::abs(position.z-z)>strict_height)return {};
        return projected;
    }
    course::Projection StrictProjection(Vector3 position,float low,float high) const {
        return Qualify(position,ProjectLocal(position,low,high),low<=0);
    }
    bool InsideCandidates(Vector3 position,std::span<const std::size_t> segments,float low,float high) const {
        course::Projection projected;
        for(auto i:segments)ProjectSegment(projected,position,i,low,high);
        return Qualify(position,projected,low<=0).valid;
    }
};
struct Policy {
    int minimum=3,maximum=3;
    bool Valid() const {return minimum>=1 && maximum>=minimum && maximum<=kMaxLaps;}
};
struct Advance {
    int gates_passed=0,laps_completed=0;
    int boundary_offenses=0;
    std::optional<float> boundary_offense_fraction;
    std::optional<float> finish_fraction;
    std::optional<float> disqualification_fraction;
    bool discontinuity=false;
    // The rider stayed inside the physical corridor but stopped following the
    // ordered line: on an overlapping town circuit that is a skipped section,
    // not a wide corner. Reported so the log can name the actual offence.
    bool section_jump=false;
};
class Progress {
    Vector3 previous_{}, recovery_{};
    double previous_time_=0;
    bool ready_=false;
    float local_along_=0;
    void ProjectLocal(const Layout& layout,Vector3 p) {
        const float low=next_gate?std::max(0.0F,layout.gates[next_gate-1].along-60.0F):0.0F;
        const float high=std::min(layout.path.Length(),layout.gates[next_gate].along+15.0F);
        const auto projection=course::Project(layout.path,p,low,high);
        if(projection.valid)local_along_=std::clamp(projection.along,0.0F,layout.gates[next_gate].along);
    }
public:
    int completed_laps=0,total_laps=0;
    std::size_t next_gate=0;
    bool continuity_lost=false;
    bool off_course=false;
    bool section_jump=false; // Set with off_course when the ordered line was abandoned.
    float offence_along=0;   // Qualified progress when the offence was recorded.
    boundary::Tracker boundary;
    void Reset(){*this={};}
    bool Finished() const {return ready_ && completed_laps==total_laps;}
    bool Start(const Layout& layout,int laps,Vector3 position,double now,int boundary_limit=1) {
        Reset();
        if(!layout.Ready() || laps<1 || laps>kMaxLaps || !std::isfinite(now) || now<0 ||
            !destination::Finite(position))return false;
        if(layout.road_start) {
            // Only the opening section BEFORE the first mandatory gate is a
            // staging area. A nearby return leg/bridge must not qualify here.
            if(!layout.StrictProjection(position,0,layout.RoadStartLimit()).valid)return false;
        } else if(destination::Distance(position,layout.path.points.front())>30 ||
            std::abs(position.z-layout.path.points.front().z)>8)return false;
        total_laps=laps;previous_=position;previous_time_=now;ready_=true;
        boundary.Reset(boundary_limit);
        const auto lap=layout.LapForGate(next_gate);
        if(layout.strict_half_width>0)boundary.Observe(layout.StrictProjection(position,lap.low,lap.high).valid,now);
        if(layout.road_start)ProjectLocal(layout,position); // First NPC goal must be ahead of the relocated grid.
        return true;
    }
    Advance Update(const Layout& layout,Vector3 position,double now) {
        Advance result;
        std::optional<float> strict_along;
        if(!ready_ || Finished() || off_course || !layout.Ready())return result;
        if(!destination::Finite(position) || !std::isfinite(now) || now<previous_time_) {
            if(!continuity_lost)recovery_=previous_;
            continuity_lost=true;result.discontinuity=true;return result;
        }
        const double dt=now-previous_time_;
        const float step=std::hypot(destination::Distance(position,previous_),position.z-previous_.z);
        if(dt==0 && step<.05F)return result; // Multiple consumers in the same tick.
        const bool broken=(dt==0 && step>=.05F) || (dt>1 && step>2) ||
            step>std::min(100.0,std::max(10.0,55.0*dt));
        const auto physical_lap=layout.LapForGate(next_gate);
        if(layout.strict_half_width>0) {
            // Physical bounds use this ordered lap's surveyed geometry. Count
            // continuous outside TIME, not individual outside samples. Sweep
            // actual motion and refine entry/exit times so a return inside
            // resets a short episode even within this observation interval.
            const int checks=broken?1:std::clamp(static_cast<int>(std::ceil(step/2)),1,64);
            const float corner_span=std::max(25.F,2*layout.strict_half_width+3);
            const float physical_low=std::max(0.F,local_along_-25);
            const float physical_high=std::min(layout.path.Length(),std::min(layout.gates[next_gate].along+corner_span,
                local_along_+std::max(corner_span,step*2+3)));
            auto inside_physical=[&](Vector3 p) {
                return layout.StrictProjection(p,physical_low,physical_high).valid||
                    layout.StrictProjection(p,physical_lap.low,physical_lap.high).valid;
            };
            auto observe=[&](bool inside,double at) {
                const auto event=boundary.Observe(inside,at);if(!event.offense)return;
                ++result.boundary_offenses;
                const float fraction=dt>0?static_cast<float>(std::clamp((event.at-previous_time_)/dt,0.0,1.0)):1.F;
                if(!result.boundary_offense_fraction)result.boundary_offense_fraction=fraction;
                if(event.disqualified) {
                    off_course=true;offence_along=local_along_;
                    result.disqualification_fraction=fraction;
                }
            };
            Vector3 before=previous_;float before_fraction=0;
            for(int i=1;i<=checks;++i) {
                const float t=static_cast<float>(i)/checks;
                const Vector3 p{previous_.x+(position.x-previous_.x)*t,
                    previous_.y+(position.y-previous_.y)*t,previous_.z+(position.z-previous_.z)*t};
                const bool inside=inside_physical(p);
                if(!broken&&inside==boundary.Outside()) {
                    const bool was_inside=!boundary.Outside();float lo=0,hi=1;
                    // A transition is rare. Collect all spatially possible
                    // local segments ONCE, then refine against those only;
                    // never perform twelve complete multi-lap projections.
                    std::vector<std::size_t> candidates;candidates.reserve(64);
                    const float min_x=std::min(before.x,p.x)-layout.strict_half_width,max_x=std::max(before.x,p.x)+layout.strict_half_width;
                    const float min_y=std::min(before.y,p.y)-layout.strict_half_width,max_y=std::max(before.y,p.y)+layout.strict_half_width;
                    const auto first=layout.path.Segment(physical_lap.low);
                    const auto last=layout.path.Segment(physical_lap.high);
                    for(std::size_t s=first;s<=last;++s) {
                        const auto a=layout.path.points[s],b=layout.path.points[s+1];
                        if(std::max(a.x,b.x)<min_x||std::min(a.x,b.x)>max_x||
                           std::max(a.y,b.y)<min_y||std::min(a.y,b.y)>max_y)continue;
                        candidates.push_back(s);
                    }
                    for(int refine=0;refine<12;++refine) {
                        const float mid=(lo+hi)*.5F;
                        const Vector3 q{before.x+(p.x-before.x)*mid,before.y+(p.y-before.y)*mid,before.z+(p.z-before.z)*mid};
                        // Match the same local-or-lap predicate used above.
                        // In particular a nearby elevated crossing must not
                        // replace a valid local road during this refinement.
                        const bool inside=layout.StrictProjection(q,physical_low,physical_high).valid||
                            layout.InsideCandidates(q,candidates,physical_lap.low,physical_lap.high);
                        if(inside==was_inside)lo=mid;else hi=mid;
                    }
                    const double crossing=previous_time_+dt*(before_fraction+(t-before_fraction)*(lo+hi)*.5F);
                    if(!was_inside)observe(false,crossing); // Mature the old episode before resetting it.
                    observe(inside,crossing);
                }
                observe(inside,previous_time_+dt*t);
                before=p;before_fraction=t;
                if(off_course)break;
            }
        }
        if(broken) {
            if(!continuity_lost)recovery_=previous_;
            continuity_lost=true;result.discontinuity=true;
        } else if(continuity_lost) {
            // A jump never qualifies on the next stationary frame. Return to
            // the last genuine section first; recovery itself awards no gate.
            if(destination::Distance(position,recovery_)<=8 && std::abs(position.z-recovery_.z)<=6)
                continuity_lost=false;
        } else if(dt<=1) {
            bool qualified=true;
            // Intermediate checkpoints describe ordered sections of the road,
            // not compulsory apex lines. On a strict course, passing their
            // along-distance inside the swept corridor is sufficient. Preserve
            // fractions so a later edge violation cannot erase an earlier finish.
            std::array<std::pair<float,float>,65> along_samples{}; // sample fraction, route metres
            std::size_t along_count=1;
            along_samples[0]={0.0F,local_along_};
            if(layout.strict_half_width>0) {
                const float low=std::max(0.0F,local_along_-25);
                {
                    // A legal inner bend can project from the inbound to the
                    // outbound segment without touching the recorded apex.
                    // Still stay local and in the next ordered gate's section:
                    // never match a remote leg or a later copy of the same lap.
                    const float corner_span=std::max(25.0F,2*layout.strict_half_width+3);
                    const float high=std::min(layout.gates[next_gate].along+corner_span,
                        local_along_+std::max(corner_span,step*2+3));
                    const int checks=std::clamp(static_cast<int>(std::ceil(step/2)),1,64);
                    for(int i=1;i<=checks;++i) {
                        const float t=static_cast<float>(i)/checks;
                        const Vector3 p{previous_.x+(position.x-previous_.x)*t,
                            previous_.y+(position.y-previous_.y)*t,previous_.z+(position.z-previous_.z)*t};
                        auto projection=layout.StrictProjection(p,low,high);
                        if(result.disqualification_fraction && t>=*result.disqualification_fraction)break;
                        if(!projection.valid) {
                            // Off the ordered line. Still inside the corridor
                            // elsewhere means the rider rejoined another
                            // section of a lap that overlaps itself; that is a
                            // shortcut, never progress. Otherwise it is the
                            // ordinary corridor exit, named as such.
                            const bool on_road=layout.StrictProjection(p,physical_lap.low,physical_lap.high).valid;
                            if(on_road) {
                                // Keep earlier samples/finish eligible. The
                                // event fraction rejects only crossings at or
                                // after this jump, not a finish already passed.
                                off_course=true;section_jump=true;offence_along=local_along_;
                                result.section_jump=true;result.disqualification_fraction=t;break;
                            }
                            // A tolerated boundary episode still follows the
                            // same local ordered section. Keep its navigation
                            // anchor/checkpoints moving, otherwise returning
                            // after grace would be falsely labelled a skip.
                            // Teleport and remote on-road branch jumps retain
                            // their independent rejection paths above.
                            projection=layout.ProjectLocal(p,low,high);
                            if(!projection.valid){qualified=false;strict_along.reset();break;}
                        }
                        strict_along=projection.along;
                        along_samples[along_count++]={t,projection.along};
                    }
                }
            }
            float last_fraction=-1;
            for(std::size_t passed=0;qualified && passed<layout.gates.size();++passed) {
                std::optional<float> crossing;
                if(layout.strict_half_width>0 && next_gate+1<layout.gates.size()) {
                    const float gate=layout.gates[next_gate].along;
                    for(std::size_t i=1;i<along_count;++i) {
                        const auto [ta,a]=along_samples[i-1];const auto [tb,b]=along_samples[i];
                        if(a<=gate && b>=gate && b-a>.0001F) {
                            crossing=ta+(tb-ta)*(gate-a)/(b-a);break;
                        }
                    }
                } else {
                    // The actual finish keeps its existing swept line/cylinder
                    // and same-frame arrival ordering. No proximity-only laps.
                    crossing=layout.destination_finish && next_gate+1==layout.gates.size()?
                        destination::ArrivalFraction(previous_,position,layout.path.points.back()):
                        layout.gates[next_gate].Crossing(previous_,position);
                }
                if(!crossing || *crossing<=last_fraction)break;
                if(result.disqualification_fraction && *crossing>=*result.disqualification_fraction)break;
                if(layout.strict_half_width>0&&next_gate+1==layout.gates.size()) {
                    const auto t=*crossing;
                    const Vector3 at{previous_.x+(position.x-previous_.x)*t,previous_.y+(position.y-previous_.y)*t,
                        previous_.z+(position.z-previous_.z)*t};
                    const auto finish_lap=layout.LapForGate(next_gate);
                    if(!layout.StrictProjection(at,finish_lap.low,finish_lap.high).valid)break;
                }
                last_fraction=*crossing;++next_gate;++result.gates_passed;
                if(next_gate==layout.gates.size()) {
                    next_gate=0;++completed_laps;++result.laps_completed;
                    if(Finished()) {
                        result.finish_fraction=crossing;
                        // Crossing a course edge AFTER finishing is irrelevant.
                        off_course=section_jump=result.section_jump=false;offence_along=0;
                        result.disqualification_fraction.reset();break;
                    }
                }
            }
        }
        previous_=position;previous_time_=now;
        if(!continuity_lost && !Finished() && !off_course) {
            if(layout.strict_half_width>0){if(strict_along)local_along_=std::min(*strict_along,layout.gates[next_gate].along);}
            else ProjectLocal(layout,position);
        }
        return result;
    }
    float Metres(const Layout& layout) const {
        return completed_laps*layout.path.Length()+(Finished()?0:local_along_);
    }
    // A rider put back on the line after a fall (the trial's host, 2026-09-19):
    // progress resumes from `position`, the gates before it counted as passed
    // and nothing held against him. Not a start - the position may be
    // anywhere on the line - and the laps already completed stand.
    bool Resume(const Layout& layout,Vector3 position,double now) {
        if(!layout.Ready() || total_laps<1 || !std::isfinite(now) || !destination::Finite(position))return false;
        const auto lap=layout.LapForGate(next_gate);
        const auto strict=layout.StrictProjection(position,lap.low,lap.high);
        const auto loose=layout.ProjectLocal(position,lap.low,lap.high);
        const float along=strict.valid?strict.along:(loose.valid?loose.along:0.0F);
        next_gate=0;
        while(next_gate+1<layout.gates.size() && layout.gates[next_gate].along<=along)++next_gate;
        local_along_=std::clamp(along,0.0F,layout.gates[next_gate].along);
        previous_=position;recovery_=position;previous_time_=now;ready_=true;
        continuity_lost=false;off_course=false;section_jump=false;offence_along=0;
        if(layout.strict_half_width>0)boundary.Observe(strict.valid,now);
        return true;
    }
    // The mod's own stuck-rider recovery moves a horse a short distance back
    // along the current section. Unlike an unknown game/player teleport, this
    // must not leave continuity_lost latched forever: the next gate is still
    // required, and the move itself awards no gate or lap.
    bool RebaseAfterModMove(const Layout& layout,Vector3 position,double now) {
        if(!ready_ || Finished() || off_course || !layout.Ready() ||
            !std::isfinite(now) || !destination::Finite(position))return false;
        const float low=next_gate?std::max(0.0F,layout.gates[next_gate-1].along-60.0F):0.0F;
        const float high=std::min(layout.path.Length(),layout.gates[next_gate].along+15.0F);
        const auto local=layout.StrictProjection(position,low,high);
        if(!local.valid)return false;
        local_along_=std::min(local_along_,std::clamp(local.along,0.0F,layout.gates[next_gate].along));
        previous_=recovery_=position;previous_time_=now;continuity_lost=false;
        boundary.Observe(true,now);
        return true;
    }
    int DisplayLap() const {return ready_?std::min(total_laps,completed_laps+1):0;}
};
enum class Outcome {None,PlayerWon,OpponentWon,Tie};
struct Decision {Outcome outcome=Outcome::None;bool disqualification=false;};
inline Decision Decide(const Advance& p,const Advance& o) {
    // First terminal event wins, independent of update order. Equal-time
    // disqualifications void the wager, just like a dead heat.
    Decision result;float first=INFINITY;
    auto event=[&](std::optional<float> at,Outcome outcome,bool foul) {
        if(!at)return;
        if(*at<first-.001F){first=*at;result={outcome,foul};}
        else if(std::abs(*at-first)<.001F) {
            if(result.outcome!=outcome)result.outcome=Outcome::Tie;
            result.disqualification|=foul;
        }
    };
    event(p.finish_fraction,Outcome::PlayerWon,false);event(o.finish_fraction,Outcome::OpponentWon,false);
    event(p.disqualification_fraction,Outcome::OpponentWon,true);event(o.disqualification_fraction,Outcome::PlayerWon,true);
    return result;
}
class Session {
    Layout layout_;
    int laps_=0;
    bool frozen_=false,running_=false;
    Outcome outcome_=Outcome::None;
public:
    Progress player,opponent;
    void Reset(){*this={};}
    template<class Random> bool Freeze(const Layout& layout,Policy policy,Random random) {
        if(frozen_ || !layout.Ready() || !policy.Valid())return false;
        const int selected=policy.minimum==policy.maximum?policy.minimum:random(policy.minimum,policy.maximum+1);
        if(selected<policy.minimum || selected>policy.maximum)return false;
        layout_=layout;laps_=selected;frozen_=true;return true;
    }
    bool Start(Vector3 player_position,Vector3 opponent_position,double now) {
        if(!frozen_ || running_ || outcome_!=Outcome::None)return false;
        Progress p,o;
        if(!p.Start(layout_,laps_,player_position,now) || !o.Start(layout_,laps_,opponent_position,now,2))return false;
        player=p;opponent=o;running_=true;return true;
    }
    Outcome Update(Vector3 player_position,Vector3 opponent_position,double now) {
        if(!running_)return Outcome::None;
        const auto p=player.Update(layout_,player_position,now),o=opponent.Update(layout_,opponent_position,now);
        outcome_=Decide(p,o).outcome;
        if(outcome_==Outcome::None)return outcome_;
        running_=false;return outcome_;
    }
    const Layout& Course() const {return layout_;}
    int Laps() const {return laps_;}
    View Hud() const {return {running_,player.DisplayLap(),opponent.DisplayLap(),laps_};}
};
}
