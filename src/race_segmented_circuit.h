#pragma once
#include "race_circuit.h"
#include "race_course.h"
#include <array>

// One immutable instance: entry + a full ring, X full rings, then the shared
// ring with its surveyed exit. Expansion disambiguates spatially overlapping
// laps; only ordered crossings may advance to the next copy of the ring.
namespace horse_races::lap {
inline bool SameNodes(const std::vector<Vector3>& a,const std::vector<Vector3>& b) {
    return a.size()==b.size() && std::equal(a.begin(),a.end(),b.begin(),[](Vector3 p,Vector3 q){
        return p.x==q.x && p.y==q.y && p.z==q.z;});
}
struct Definition {
    int extra_min=0,extra_max=8;
    bool single_loop=false,spectators=true,allow_wolves=false,road_start=false;
    bool independent_laps=false; // entry=full first, shared=full regular, finish=full final.
    // One recorded run from a start to a finish, no ring anywhere. Uses the
    // same corridor, ordered gates, section-jump foul and finish handling as a
    // circuit - a lap engine works on an open line as readily as a closed one,
    // it just never comes back. Geometry lives in `shared`.
    bool single_trip=false;
    float strict_half_width=14,strict_height=6;
    // Rolling guidance around global qualified progress. 0 keeps the whole
    // current lap plus its ordered continuation, not an artificial lap endpoint.
    float guide_ahead=0,guide_behind=0;
    std::vector<Vector3> entry,shared,returning,finish;
    std::vector<std::size_t> required; // Indices in shared, excluding its endpoints.
    bool operator==(const Definition& b) const {
        return extra_min==b.extra_min && extra_max==b.extra_max && required==b.required &&
            single_loop==b.single_loop && independent_laps==b.independent_laps && single_trip==b.single_trip && spectators==b.spectators && allow_wolves==b.allow_wolves && road_start==b.road_start &&
            strict_half_width==b.strict_half_width && strict_height==b.strict_height &&
            guide_ahead==b.guide_ahead && guide_behind==b.guide_behind &&
            SameNodes(entry,b.entry) && SameNodes(shared,b.shared) &&
            SameNodes(returning,b.returning) && SameNodes(finish,b.finish);
    }
};
struct Plan {
    circuit::Layout course;
    std::array<horse_races::course::Path,3> profiles; // Surveyed first, regular, final shapes.
    std::vector<float> offsets;
    std::vector<std::size_t> lap_gate_ends; // Number of qualified gates at each nonfinal lap end.
    int extra=0;
    float guide_ahead=0,guide_behind=0;
    bool single_loop=false,single_trip=false,allow_wolves=false;
    int Total() const {return static_cast<int>(offsets.size());}
    int Profile(int zero_based_lap) const {return zero_based_lap==0?0:(zero_based_lap+1==Total()?2:1);}
    int Completed(const circuit::Progress& p) const {
        if(p.Finished())return Total();
        return static_cast<int>(std::upper_bound(lap_gate_ends.begin(),lap_gate_ends.end(),p.next_gate)-lap_gate_ends.begin());
    }
    int Current(const circuit::Progress& p) const {return std::min(Total()-1,Completed(p));}
    // Live corridor change on a frozen instance: the strict corridor and every
    // gate (the Veteran lap line is a Gate) are the only places the width lives.
    // Build's bounds apply; a refused width leaves the plan exactly as it was.
    bool SetHalfWidth(float half_width) {
        if(!std::isfinite(half_width) || half_width<5 || half_width>20)return false;
        course.strict_half_width=half_width;
        for(auto& gate:course.gates)gate.half_width=half_width;
        return true;
    }
    bool Build(const Definition& d,int selected) {
        *this={};
        // Authored gate indices are required except where the mode derives its
        // own: independent laps take them from the lap seams, a single trip
        // spaces them along the recorded line below.
        if(d.extra_min<0 || d.extra_max>circuit::kMaxLaps-2 || d.extra_max<d.extra_min || selected<d.extra_min || selected>d.extra_max ||
            (!d.independent_laps && !d.single_trip &&
                (d.required.size()<3 || d.required.size()>(d.single_loop?64U:4U))))return false;
        if(!std::isfinite(d.guide_ahead) || !std::isfinite(d.guide_behind) || d.guide_ahead<0 || d.guide_behind<0 ||
            (d.guide_ahead>0 && (d.guide_ahead<50 || d.guide_ahead>2000 || d.guide_behind>200)) ||
            (d.guide_ahead==0 && d.guide_behind>0))return false;
        guide_ahead=d.guide_ahead;guide_behind=d.guide_behind;
        // A section built here from a handful of points keeps its small bound; a section READ from a
        // route file holds as many nodes as the course itself may, and those three callers pass
        // course::kMaxNodes. How finely a ride was recorded is a property of the ride.
        auto section=[](const std::vector<Vector3>& nodes,std::size_t maximum=128) {
            if(nodes.size()<2 || nodes.size()>maximum)return false;
            for(std::size_t i=0;i<nodes.size();++i) {
                if(!destination::Finite(nodes[i]) || std::abs(nodes[i].x)>20000 || std::abs(nodes[i].y)>20000 ||
                    nodes[i].z< -500 || nodes[i].z>3000)return false;
                if(i) {const float len=destination::Distance(nodes[i-1],nodes[i]);if(len<.05F || len>250)return false;}
            }
            return true;
        };
        if(d.single_trip) {
            // Exactly one section, open at both ends, with the finish where the
            // recording stopped. Everything else a circuit rejects is rejected
            // here too, so a trip cannot smuggle in an unreviewed option.
            if(d.independent_laps || d.single_loop || d.road_start || d.allow_wolves || d.spectators ||
                d.extra_min!=0 || d.extra_max!=0 || selected!=0 ||
                !d.entry.empty() || !d.returning.empty() || !d.finish.empty() || !d.required.empty() ||
                !section(d.shared,course::kMaxNodes) ||
                !std::isfinite(d.strict_half_width) || d.strict_half_width<5 || d.strict_half_width>20 ||
                !std::isfinite(d.strict_height) || d.strict_height<2 || d.strict_height>8)return false;
            for(auto& profile:profiles)if(!profile.Build(d.shared) || profile.Length()<200)return false;
            // Gates every quarter of the way, then the recorded end as the
            // finish: the same shape the independent laps use per lap.
            std::vector<std::size_t> gates;
            for(float fraction:{.2F,.4F,.6F,.8F}) {
                const auto node=profiles[0].Segment(profiles[0].Length()*fraction)+1;
                if(!gates.empty() && node<=gates.back())return false;
                gates.push_back(node);
            }
            if(!course.Build(d.shared,gates,d.strict_half_width,d.strict_height,true))return false;
            course.strict_half_width=d.strict_half_width;course.strict_height=d.strict_height;
            course.destination_finish=true;
            offsets={0};single_trip=true;return true;
        }
        if(d.independent_laps) {
            if(d.single_loop || d.road_start || d.allow_wolves || !d.returning.empty() || !d.required.empty() ||
                !section(d.entry,course::kMaxNodes) || !section(d.shared,course::kMaxNodes) || !section(d.finish,course::kMaxNodes) ||
                !std::isfinite(d.strict_half_width) || d.strict_half_width<5 || d.strict_half_width>20 ||
                !std::isfinite(d.strict_height) || d.strict_height<2 || d.strict_height>8)return false;
            auto joined=[](Vector3 a,Vector3 b){return destination::Distance(a,b)<.01F && std::abs(a.z-b.z)<.01F;};
            if(!joined(d.entry.back(),d.shared.front()) || !joined(d.shared.back(),d.shared.front()) ||
                !joined(d.shared.back(),d.finish.front()))return false;
            const std::array<const std::vector<Vector3>*,3> shapes{&d.entry,&d.shared,&d.finish};
            for(std::size_t i=0;i<3;++i)if(!profiles[i].Build(*shapes[i]) || profiles[i].Length()<100)return false;
            std::vector<Vector3> expanded;std::vector<std::size_t> gates;
            float distance=0;
            for(int lap=0;lap<selected+2;++lap) {
                const int profile=lap==0?0:(lap==selected+1?2:1);
                const auto& shape=*shapes[profile];const auto& path=profiles[profile];
                const std::size_t base=expanded.empty()?0:expanded.size()-1;
                offsets.push_back(distance);distance+=path.Length();
                expanded.insert(expanded.end(),shape.begin()+(expanded.empty()?0:1),shape.end());
                // Three distributed qualification gates per independently surveyed lap.
                // The continuous strict corridor remains authoritative between them.
                for(float fraction:{.25F,.5F,.75F}) {
                    const auto node=base+path.Segment(path.Length()*fraction)+1;
                    if(!gates.empty() && node<=gates.back())return false;
                    gates.push_back(node);
                }
                if(profile!=2){gates.push_back(expanded.size()-1);lap_gate_ends.push_back(gates.size());}
            }
            if(!course.Build(expanded,gates,d.strict_half_width,d.strict_height,true))return false;
            course.strict_half_width=d.strict_half_width;course.strict_height=d.strict_height;
            course.destination_finish=true;extra=selected;return course.SetPhysicalLaps(lap_gate_ends);
        }
        if(d.single_loop) {
            if(d.extra_min!=0 || d.extra_max!=0 || selected!=0 || !d.entry.empty() ||
                !d.returning.empty() || !d.finish.empty() || !section(d.shared,course::kMaxNodes) ||
                !std::isfinite(d.strict_half_width) || d.strict_half_width<5 || d.strict_half_width>20 ||
                !std::isfinite(d.strict_height) || d.strict_height<2 || d.strict_height>8 ||
                !course.Build(d.shared,d.required,d.strict_half_width,d.strict_height))return false;
            for(auto& profile:profiles)if(!profile.Build(d.shared))return false;
            course.strict_half_width=d.strict_half_width;course.strict_height=d.strict_height;course.road_start=d.road_start;
            offsets={0};single_loop=true;allow_wolves=d.allow_wolves;return true;
        }
        if(!std::isfinite(d.strict_half_width) || d.strict_half_width<5 || d.strict_half_width>20 ||
            !std::isfinite(d.strict_height) || d.strict_height<2 || d.strict_height>8 || d.allow_wolves || d.road_start)return false;
        for(const auto* nodes:{&d.entry,&d.shared,&d.returning,&d.finish})if(!section(*nodes))return false;
        auto joined=[](Vector3 a,Vector3 b){return destination::Distance(a,b)<.01F && std::abs(a.z-b.z)<.01F;};
        if(!joined(d.entry.back(),d.shared.front()) || !joined(d.shared.back(),d.returning.front()) ||
            !joined(d.shared.back(),d.finish.front()) || !joined(d.returning.back(),d.shared.front()) ||
            !joined(d.finish.back(),d.entry.front()))return false;
        std::size_t previous=0;
        for(auto node:d.required){if(node<=previous || node+1>=d.shared.size())return false;previous=node;}
        auto append=[](std::vector<Vector3>& out,const std::vector<Vector3>& nodes) {
            out.insert(out.end(),nodes.begin()+(out.empty()?0:1),nodes.end());
        };
        std::array<std::vector<Vector3>,3> shape;
        append(shape[0],d.entry);append(shape[0],d.shared);append(shape[0],d.returning);
        append(shape[1],d.shared);append(shape[1],d.returning);
        append(shape[2],d.shared);append(shape[2],d.finish);
        for(std::size_t i=0;i<shape.size();++i)if(!profiles[i].Build(shape[i]))return false;
        std::vector<Vector3> expanded;
        std::vector<std::size_t> gates;
        std::vector<float> starts;
        float distance=0;
        for(int i=0;i<selected+2;++i) {
            const int profile=i==0?0:(i==selected+1?2:1);
            const std::size_t base=expanded.empty()?0:expanded.size()-1;
            starts.push_back(distance);distance+=profiles[profile].Length();
            const std::size_t shared_base=base+(i==0?d.entry.size()-1:0);
            append(expanded,shape[profile]);
            for(auto node:d.required)gates.push_back(shared_base+node);
            if(profile==2)gates.push_back(shared_base+d.shared.size()-1); // Qualify the final exit.
            else {gates.push_back(expanded.size()-1);lap_gate_ends.push_back(gates.size());}
        }
        if(!course.Build(expanded,gates,d.strict_half_width,d.strict_height))return false;
        course.strict_half_width=d.strict_half_width;course.strict_height=d.strict_height;
        course.destination_finish=true;offsets=std::move(starts);extra=selected;return course.SetPhysicalLaps(lap_gate_ends);
    }
};
class Session {
    bool frozen_=false,running_=false;
public:
    Plan plan;
    circuit::Progress player,opponent;
    bool rival_gate_recovery=false;
    bool disqualified_result=false;
    void Reset(){*this={};}
    void Stop(){running_=false;}
    bool Frozen() const {return frozen_;}
    bool Running() const {return running_;}
    template<class Random> bool Freeze(const Definition& definition,Random random) {
        if(frozen_)return false;
        Plan checked;
        if(!checked.Build(definition,definition.extra_max))return false;
        const int x=definition.extra_min==definition.extra_max?definition.extra_min:
            random(definition.extra_min,definition.extra_max+1);
        if(!checked.Build(definition,x))return false;
        plan=std::move(checked);frozen_=true;return true;
    }
    bool Start(Vector3 p,Vector3 o,double now) {
        if(!frozen_ || running_ || player.total_laps)return false;
        circuit::Progress a,b;
        if(!a.Start(plan.course,1,p,now) || !b.Start(plan.course,1,o,now,2))return false;
        player=a;opponent=b;running_=true;return true;
    }
    circuit::Outcome Update(Vector3 p,Vector3 o,double now) {
        if(!running_)return circuit::Outcome::None;
        const auto a=player.Update(plan.course,p,now),b=opponent.Update(plan.course,o,now);
        const auto decision=circuit::Decide(a,b);
        if(decision.outcome==circuit::Outcome::None)return decision.outcome;
        running_=false;
        disqualified_result=decision.disqualification;return decision.outcome;
    }
    // The trial's host put back on the line: his progress resumes there and a
    // disqualification already decided is set aside; the race goes on.
    bool ResumeOpponent(Vector3 position,double now) {
        if(!opponent.Resume(plan.course,position,now))return false;
        running_=true;disqualified_result=false;return true;
    }
    void Export(const circuit::Progress& source,Vector3 position,course::Progress& out) const {
        const auto& path=plan.course.path;
        const float along=source.Metres(plan.course);
        const auto local=course::Project(path,position,std::max(0.0F,along-20),std::min(path.Length(),along+20));
        out.current={along,local.distance,path.Segment(along),local.valid};
        out.nearest=out.current;out.furthest=std::max(out.furthest,along);
        out.continuity_lost=source.continuity_lost || source.off_course;
    }
    std::size_t RivalTarget(float speed, std::optional<Vector3> position={}) {
        if(opponent.off_course)return plan.course.path.Segment(opponent.Metres(plan.course));
        if(position && !opponent.Finished()) {
            const auto& gate=plan.course.gates[opponent.next_gate];
            const float side=(position->x-gate.centre.x)*gate.forward.x+(position->y-gate.centre.y)*gate.forward.y;
            if(side>3 && std::abs(opponent.Metres(plan.course)-gate.along)<2 &&
                destination::Distance(*position,gate.centre)<60)rival_gate_recovery=true;
            else if(side< -5)rival_gate_recovery=false;
            if(rival_gate_recovery)return plan.course.path.Segment(std::max(0.0F,gate.along-20));
        }
        course::Progress proxy;proxy.current.along=opponent.Metres(plan.course);proxy.current.valid=true;
        proxy.current.segment=plan.course.path.Segment(proxy.current.along);
        const auto desired=course::LookAhead(plan.course.path,proxy,speed,true);
        if(opponent.Finished())return plan.course.path.points.size()-1;
        // Put the traffic goal beyond the next gate, but never skip a whole leg.
        // Recovery may move this target backwards when a gate was actually missed.
        const auto cap=plan.course.path.Segment(plan.course.gates[opponent.next_gate].along+15)+1;
        const auto limited=std::min(desired,cap);
        // A high-speed horizon may not turn a bend into a straight line through
        // its inside wall. Keep spatial bends, not the recording's speed; native
        // traffic steering still executes the goal. This only ever pulls the
        // goal CLOSER, never further, so it cannot send a rival somewhere new.
        //
        // Written for Charles's mountain lap and gated on that mode, but keeping
        // a goal on the road is how any rival is made to hold a line - it is not
        // a property of one race shape. Every judged recorded course gets it.
        //
        // The tolerance comes from the corridor, so a route without one would
        // clamp the goal to the very next node and reduce the rival to crawling
        // from node to node. Silently. Leave such a route alone instead.
        if(!(plan.course.strict_half_width>0) || !(plan.course.strict_height>0))return limited;
        const auto& path=plan.course.path;
        const auto first=path.Segment(opponent.Metres(plan.course));
        const auto origin=position.value_or(path.points[first]);
        std::size_t safe=std::min(first+1,path.points.size()-1);
        for(std::size_t end=safe+1;end<=limited;++end) {
            const auto b=path.points[end];const float dx=b.x-origin.x,dy=b.y-origin.y;
            const float length2=dx*dx+dy*dy;if(length2<.01F)break;
            bool follows=true;
            for(std::size_t i=first+1;i<end;++i) {
                const auto p=path.points[i];
                const float t=std::clamp(((p.x-origin.x)*dx+(p.y-origin.y)*dy)/length2,0.0F,1.0F);
                if(std::hypot(p.x-origin.x-t*dx,p.y-origin.y-t*dy)>plan.course.strict_half_width*.5F ||
                    std::abs(p.z-origin.z-t*(b.z-origin.z))>plan.course.strict_height*.5F){follows=false;break;}
            }
            if(!follows)break;safe=end;
        }
        return std::min(safe,limited);
    }
    circuit::View Hud() const {
        if(!running_)return {};
        return {true,plan.Current(player)+1,plan.Current(opponent)+1,plan.Total()};
    }
};
}
