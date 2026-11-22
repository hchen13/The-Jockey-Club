#pragma once
#include "race_following.h"
#include "race_obstacle_map_model.h"
#include "race_strip_geometry.h"
#include <array>

namespace horse_races::native_passing {
// A bounded choice of offsets for the EXISTING native recording task. This
// owns neither pace nor geometry: absent positive room evidence it grants no
// new lateral move. The constants are control priors, not native shape proofs.
inline constexpr float kOffset=1.8F,kMargin=1.5F,kBody=.5F,kTriggerDistance=3.F;
inline constexpr double kDecisionSeconds=.25;
inline constexpr std::size_t kMaxSegments=32,kMaxRows=48,kMaxStrips=16;
inline constexpr std::size_t kMaxLegalSegments=64;
namespace detail {
inline Vector3 Node(const race_pace::Plan& plan,std::size_t i,float offset) {
    // Native node frame uses this node and its predecessor (node zero uses
    // the first segment), not the outgoing normal plus a corner connector.
    const auto before=i?i-1:0,after=i?i:1;
    const auto a=plan.path.points[before],b=plan.path.points[after];
    const float length=std::hypot(b.x-a.x,b.y-a.y);
    const auto p=plan.path.points[i];
    return length>.01F?Vector3{p.x-(b.y-a.y)/length*offset,p.y+(b.x-a.x)/length*offset,p.z}:p;
}
inline Vector3 NativeAt(const race_pace::Plan& plan,float line,float offset) {
    line=std::clamp(line,0.F,plan.path.Length());const auto i=plan.path.Segment(line);
    const float t=(line-plan.path.metres[i])/(plan.path.metres[i+1]-plan.path.metres[i]);
    const auto a=Node(plan,i,offset),b=Node(plan,i+1,offset);
    return {a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t};
}
struct Frame {Vector3 point{};float x=1,y=0,ox=0,oy=1;};
inline Frame At(const race_pace::Plan& plan,float along) {
    const auto line=std::clamp(plan.ToLine(along),0.F,plan.path.Length());
    const auto i=plan.path.Segment(line);const auto a=plan.path.points[i],b=plan.path.points[i+1];
    const float length=std::hypot(b.x-a.x,b.y-a.y);
    const auto origin=race_pace::At(plan.path,line),shifted=NativeAt(plan,line,1);
    return {origin,length>0?(b.x-a.x)/length:0,length>0?(b.y-a.y)/length:0,shifted.x-origin.x,shifted.y-origin.y};
}
inline Vector3 Shift(Vector3 p,float x,float y,float left) {return {p.x-y*left,p.y+x*left,p.z};}
struct Axis {bool valid=false;float lateral=0;};
struct Row {
    obstacle_map::Section frame{};
    float along=0;
    bool recovery_connection_negative=false;
    std::vector<obstacle_map::Interval> clear;
    std::vector<obstacle_map::Interval> blocked;
    std::vector<obstacle_map::Passage> strips;
};
struct Window {
    struct LegalSegment {Vector3 a{};float dx=0,dy=0,inverse=0;};
    const race_pace::Plan* plan=nullptr;
    std::array<LegalSegment,kMaxLegalSegments> legal_segments{};std::size_t legal_count=0;
    std::array<Row,kMaxRows> rows{};std::size_t count=0,first_segment=0,last_segment=0;
    std::array<strip_geometry::PreparedPatch,kMaxRows*kMaxStrips> patches{};std::size_t patch_count=0;
    float begin=0,end=0,usable=0;
    bool Build(const course::Path& legal,float half_width,const race_pace::Plan& source,
        const obstacle_map::Map* map,float along,float speed,float inspection_end,float short_return=0) {
        plan=nullptr;legal_count=patch_count=count=0;
        if(!map||map->geometry_mismatch||map->rows.size()<2||legal.points.size()<2||
           source.path.points.size()<2||!std::isfinite(half_width)||half_width<=kMargin||
           !std::isfinite(along)||!std::isfinite(speed)||speed<0||!std::isfinite(inspection_end))return false;
        begin=std::clamp(along,0.F,legal.Length());
        end=std::min(legal.Length(),std::max(begin+(short_return>0?short_return:std::max(35.F,speed*3.F)),inspection_end));
        if(end<=begin+.1F)return false;
        const auto legal_first=legal.Segment(std::max(0.F,begin-8));
        const auto legal_last=legal.Segment(std::min(legal.Length(),end+8));
        if(legal_last-legal_first+1>kMaxLegalSegments)return false;
        for(auto i=legal_first;i<=legal_last;++i) {
            const auto a=legal.points[i],b=legal.points[i+1];
            const float dx=b.x-a.x,dy=b.y-a.y,square=dx*dx+dy*dy;
            if(!destination::Finite(a)||!destination::Finite(b)||square<.0001F)return false;
            legal_segments[legal_count++]={a,dx,dy,1.F/square};
        }
        const auto first=map->Index(begin);auto last=map->Index(end);
        if(last+1<map->rows.size()&&map->Along(last)<end-.001F)++last;
        if(last<first||last-first+1>kMaxRows||map->Along(first)>begin+.01F||map->Along(last)<end-.01F)return false;
        first_segment=source.path.Segment(source.ToLine(map->Along(first)));
        last_segment=source.path.Segment(source.ToLine(map->Along(last)));
        if(first_segment>0)--first_segment;
        if(last_segment+2<source.path.points.size())++last_segment;
        if(last_segment<first_segment||last_segment-first_segment+1>kMaxSegments)return false;
        plan=&source;usable=half_width-kMargin;count=last-first+1;
        for(std::size_t k=0;k<count;++k) {
            const auto index=first+k;const auto& measured=map->rows[index];auto& row=rows[k];
            // Legacy widths alone are not a swept connection certificate.
            if(!measured.HasDetailed()||measured.legacy)return false;
            row.frame=map->SectionFor(legal,index);row.along=map->Along(index);row.clear=measured.ClearIntervals(kBody);
            if(row.clear.empty()||row.clear.size()>kMaxStrips)return false;
            row.blocked=measured.BlockedIntervals(map->width,kBody,map->native_mode,row.clear);
            if(row.blocked.size()>kMaxStrips)return false;
            row.recovery_connection_negative=!measured.connection_impacts.empty()||!measured.authored_links.empty();
            if(k) {
                if(!measured.HasConnections()&&measured.native_strips.empty()&&measured.passages.empty())return false;
                row.strips=map->ConnectionStrips(index,kBody);
                if(row.strips.empty()||row.strips.size()>kMaxStrips)return false;
            }
        }
        // Cache the local positive-space patches once per decision. Clip to
        // both endpoint observations and the sporting reserve; an incoming
        // strip must not resurrect a later section impact or course edge.
        for(std::size_t k=1;k<count;++k)for(const auto& strip:rows[k].strips)
            for(const auto entry:rows[k-1].clear)for(const auto exit:rows[k].clear) {
                const obstacle_map::Interval a{std::max({strip.from.lo,entry.lo,-usable}),std::min({strip.from.hi,entry.hi,usable})};
                const obstacle_map::Interval b{std::max({strip.to.lo,exit.lo,-usable}),std::min({strip.to.hi,exit.hi,usable})};
                if(a.lo>a.hi||b.lo>b.hi)continue;
                const auto patch=strip_geometry::Prepare({rows[k-1].frame,rows[k].frame,a,b});
                if(!patch.regular)continue;
                if(patch_count==patches.size())return false;
                patches[patch_count++]=patch;
            }
        if(!patch_count)return false;
        return true;
    }
    Axis Cross(const obstacle_map::Section& frame,float offset) const {
        Axis result;float best=INFINITY;
        for(auto i=first_segment;i<=last_segment;++i) {
            const auto p=Node(*plan,i,offset),q=Node(*plan,i+1,offset);
            const float before=(p.x-frame.centre.x)*-frame.ny+(p.y-frame.centre.y)*frame.nx;
            const float delta=(q.x-p.x)*-frame.ny+(q.y-p.y)*frame.nx;
            if(std::abs(delta)<1e-5F)continue;
            const float t=-before/delta;if(t<-.001F||t>1.001F)continue;
            const Vector3 hit{p.x+t*(q.x-p.x),p.y+t*(q.y-p.y),p.z+t*(q.z-p.z)};
            const float d=std::hypot(hit.x-frame.centre.x,hit.y-frame.centre.y);
            if(d>=best||std::abs(hit.z-frame.centre.z)>3.5F)continue;
            best=d;result={true,(hit.x-frame.centre.x)*frame.nx+(hit.y-frame.centre.y)*frame.ny};
        }
        return result;
    }
    // Every cross-section and every incoming strip must contain the ENTIRE
    // lateral band, so disconnected free islands cannot authorize a cut.
    float Offset(float along,float from,float to,float transition) const {
        return from+(to-from)*std::clamp((along-begin)/transition,0.F,1.F);
    }
    bool Band(float from,float to,float transition) const {
        float previous_lo=0,previous_hi=0;
        for(std::size_t k=0;k<count;++k) {
            const auto a=Cross(rows[k].frame,Offset(rows[k].along,from,to,transition)),b=Cross(rows[k].frame,to);
            if(!a.valid||!b.valid)return false;
            const float lo=std::min(a.lateral,b.lateral),hi=std::max(a.lateral,b.lateral);
            if(lo< -usable||hi>usable)return false;
            bool room=false;for(auto free:rows[k].clear)if(free.lo<=lo&&free.hi>=hi){room=true;break;}
            if(!room)return false;
            if(k) {
                bool connected=false;
                for(const auto& strip:rows[k].strips)
                    if(strip.from.lo<=previous_lo&&strip.from.hi>=previous_hi&&strip.to.lo<=lo&&strip.to.hi>=hi) {
                        const auto patch=strip_geometry::Prepare({rows[k-1].frame,rows[k].frame,strip.from,strip.to});
                        if(patch.regular){connected=true;break;}
                    }
                if(!connected)return false;
            }
            previous_lo=lo;previous_hi=hi;
        }
        return true;
    }
    // Row membership alone misses a chord cutting across an unmeasured bend.
    // Resolve exact chord coverage by the bounded local strip union.
    bool LegalChord(Vector3 a,Vector3 b) const {
        // Cross-section membership alone cannot prove the space BETWEEN
        // sparse survey rows. A capsule is convex: both ends of each short
        // chord in one legal segment's capsule certify its whole interior.
        // Four bounded pieces permit a corner's capsule union without a
        // whole-course projection or accepting isolated point samples.
        const float radius2=usable*usable;
        auto point=[&](float t){return Vector3{a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t};};
        for(int piece=0;piece<4;++piece) {
            const auto p=point(piece*.25F),q=point((piece+1)*.25F);bool proven=false;
            for(std::size_t i=0;i<legal_count;++i) {
                const auto& s=legal_segments[i];
                const auto inside=[&](Vector3 v) {
                    const float t=std::clamp(((v.x-s.a.x)*s.dx+(v.y-s.a.y)*s.dy)*s.inverse,0.F,1.F);
                    const float x=v.x-s.a.x-t*s.dx,y=v.y-s.a.y-t*s.dy;
                    return x*x+y*y<=radius2;
                };
                if(inside(p)&&inside(q)){proven=true;break;}
            }
            if(!proven)return false;
        }
        return true;
    }
    bool Chord(Vector3 a,Vector3 b) const {
        if(!LegalChord(a,b))return false;
        std::array<strip_geometry::Range,kMaxRows*kMaxStrips*2> covered{};std::size_t used=0;
        for(std::size_t k=0;k<patch_count;++k) {
            const auto spans=strip_geometry::CoveredIntervals(a,b,patches[k]);
            for(std::size_t n=0;n<spans.count;++n) {
                if(used==covered.size())return false;
                covered[used++]=spans.ranges[n];
            }
        }
        std::sort(covered.begin(),covered.begin()+used,[](auto x,auto y){return x.lo<y.lo;});
        double end=0;for(std::size_t i=0;i<used;++i) {
            if(covered[i].lo>end+1e-6)return false;
            end=std::max(end,covered[i].hi);if(end>=1-1e-6)return true;
        }
        return false;
    }
    float Reference(float line) const {
        if(plan->reference.size()!=plan->path.metres.size())return line;
        const auto i=plan->path.Segment(line);
        const float t=(line-plan->path.metres[i])/(plan->path.metres[i+1]-plan->path.metres[i]);
        return plan->reference[i]+t*(plan->reference[i+1]-plan->reference[i]);
    }
    bool Line(float from,float to,float transition) const {
        const float low=plan->ToLine(begin),high=plan->ToLine(end);
        Vector3 previous{};bool had=false;
        for(auto i=first_segment;i<=last_segment;++i) {
            const float start=std::max(low,plan->path.metres[i]),finish=std::min(high,plan->path.metres[i+1]);
            if(finish<=start)continue;
            const auto p=NativeAt(*plan,start,Offset(Reference(start),from,to,transition));
            const auto q=NativeAt(*plan,finish,Offset(Reference(finish),from,to,transition));
            if(had&&!Chord(previous,p))return false;
            const float cut=plan->ToLine(begin+transition);
            if(cut>start&&cut<finish) {
                const auto middle=NativeAt(*plan,cut,to);
                if(!Chord(p,middle)||!Chord(middle,q))return false;
            } else if(!Chord(p,q))return false;
            previous=q;had=true;
        }
        return had;
    }
    static float Transition(float actual,float target,float speed) {
        return std::max(6.F,std::max(0.F,speed)*std::max(.75F,std::abs(target-actual)/1.8F+.25F));
    }
    bool Return(float actual,const following::Mount& self) const {
        constexpr float target=0;
        const float transition=Transition(actual,target,self.speed);
        if(!Band(actual,target,transition)||!Line(actual,target,transition)||!Line(target,target,transition))return false;
        const auto entry=At(*plan,std::min(end,begin+transition));
        return Chord(self.position,entry.point);
    }
    bool Allows(float actual,float target,const following::Mount& self) const {
        // The setter is ONE scalar for all native targets, not a spatial
        // d(s) trajectory. Prove the full constant-offset band and exact
        // native chords before granting a nonzero request.
        if(!Band(actual,target,INFINITY)||!Line(actual,actual,1)||!Line(target,target,1))return false;
        const auto entry=At(*plan,std::min(end,begin+Transition(actual,target,self.speed)));
        return Chord(self.position,{entry.point.x+entry.ox*target,entry.point.y+entry.oy*target,entry.point.z});
    }
    bool Recover(float actual,const following::Mount& self) const {
        // Revocation is not a fresh passing grant. If drift has already put
        // the horse outside its reserve/support, requiring a safe t=0 would
        // latch the old outward request forever. Only restore a certified
        // original line, with monotone reduction of the current violation
        // and no newly crossed known obstacle. Unknown remains unknown.
        if(!Band(0,0,1)||!Line(0,0,1))return false;
        const auto& row=rows[0];const auto base=Cross(row.frame,0);if(!base.valid)return false;
        const float current=(self.position.x-row.frame.centre.x)*row.frame.nx+
            (self.position.y-row.frame.centre.y)*row.frame.ny;
        const bool outside=std::abs(current)>usable+.01F||!obstacle_map::Contains(row.clear,current);
        if(!outside||std::abs(actual)<.05F)return false;
        const float low=std::min(current,base.lateral),high=std::max(current,base.lateral);
        if(std::max(0.F,std::abs(base.lateral)-usable)>std::max(0.F,std::abs(current)-usable))return false;
        for(const auto obstacle:row.blocked) {
            if(obstacle.hi<=low||obstacle.lo>=high)continue;
            if(current<obstacle.lo||current>obstacle.hi)return false;
            // Starting inside a known impact may leave through the nearest
            // edge, but must not traverse deeper through its blocked band.
            if(base.lateral<current&&current-obstacle.lo>obstacle.hi-current+.001F)return false;
            if(base.lateral>current&&obstacle.hi-current>current-obstacle.lo+.001F)return false;
        }
        const float transition=Transition(actual,0,self.speed);
        float previous_violation=std::max(0.F,std::abs(current)-usable);
        for(std::size_t k=1;k<count;++k) {
            const auto destination=Cross(rows[k].frame,0);
            const auto starting=Cross(rows[k].frame,Offset(rows[k].along,actual,0,transition));
            if(!destination.valid||!starting.valid)return false;
            const float violation=std::max(0.F,std::abs(starting.lateral)-usable);
            if(violation>previous_violation+.01F)return false;
            previous_violation=violation;
            const float lo=std::min(destination.lateral,starting.lateral),hi=std::max(destination.lateral,starting.lateral);
            for(const auto obstacle:rows[k].blocked)if(obstacle.lo<hi&&obstacle.hi>lo)return false;
            // This exceptional revocation does not infer a swept route
            // through an incoming impact or authored link. Conservatively
            // refuse the exception while any such negative lies in the merge.
            if(rows[k].along<=begin+transition+3.F&&rows[k].recovery_connection_negative)return false;
        }
        return true;
    }
};
inline bool Neighbor(const following::Mount& self,const following::Mount& other,const Frame& frame,
    float& gap,float& lateral,float& longitudinal,float& sideways,bool any_heading=false) {
    if(!other.horse||other.horse==self.horse||!destination::Finite(other.position)||
       !destination::Finite(other.forward)||!std::isfinite(other.speed)||other.speed<0||
       !std::isfinite(other.along))return false;
    const bool heights=self.route_height_valid&&other.route_height_valid;
    const float height=other.position.z-self.position.z-(heights?other.route_height-self.route_height:0);
    if(!std::isfinite(height)||std::abs(height)>3.5F)return false;
    const float length=std::hypot(other.forward.x,other.forward.y);if(length<.5F)return false;
    const float alignment=(other.forward.x*frame.x+other.forward.y*frame.y)/length;
    if(!any_heading&&alignment<.5F)return false;
    const float dx=other.position.x-self.position.x,dy=other.position.y-self.position.y;
    gap=dx*frame.x+dy*frame.y;lateral=-dx*frame.y+dy*frame.x;
    longitudinal=other.speed*alignment;
    sideways=other.speed*(-other.forward.x*frame.y+other.forward.y*frame.x)/length;
    return std::abs(gap)<=65;
}
enum class TrafficPurpose {NewPass,ActivePass,Merge};
inline bool TrafficClear(const following::Mount& self,std::span<const following::Mount> roster,
    const Frame& frame,float actual,float target,float free_speed,TrafficPurpose purpose,int passing_blocker=0) {
    const float projection=-frame.ox*frame.y+frame.oy*frame.x;
    actual*=projection;target*=projection;
    const float lo=std::min(actual,target)-1.4F,hi=std::max(actual,target)+1.4F;
    for(const auto& other:roster) {
        float gap,lateral,speed,sideways;if(!Neighbor(self,other,frame,gap,lateral,speed,sideways,true))continue;
        lateral+=actual;
        // Swept local body occupancy: adjacent horses keep their space even
        // while they are not our leader. Include observed short lateral drift.
        const float future_lateral=lateral+sideways*1.5F;
        const bool crosses=std::min(lateral,future_lateral)<=hi&&std::max(lateral,future_lateral)>=lo;
        const float future_gap=gap+(speed-self.speed)*1.5F;
        // A pass is initiated only within three metres of its selected
        // forward blocker. Requiring 5.5 m clearance from that same horse
        // would make every such pass impossible. Permit only a lateral move
        // away from this one racer; physical collision remains native. Other
        // horses, oncoming traffic and all merges retain the full checks.
        const float other_heading=std::hypot(other.forward.x,other.forward.y);
        const float alignment=(other.forward.x*frame.x+other.forward.y*frame.y)/other_heading;
        const float self_heading=std::hypot(self.forward.x,self.forward.y);
        const bool heading_valid=destination::Finite(self.forward)&&std::isfinite(self_heading)&&self_heading>=.5F;
        // A previous target may have changed relative position or direction as
        // horses turn. Do not exempt a horse now behind or approaching us.
        // Without a usable heading retain the established task-frame fallback.
        const bool heading_front_and_aligned=!heading_valid ||
            (((other.position.x-self.position.x)*self.forward.x+(other.position.y-self.position.y)*self.forward.y)>0 &&
            (other.forward.x*self.forward.x+other.forward.y*self.forward.y)/(other_heading*self_heading)>=.5F);
        const bool moving_away=purpose!=TrafficPurpose::Merge && other.horse==passing_blocker &&
            alignment>=.5F && heading_front_and_aligned && gap>0 && gap<5.5F &&
            std::abs(target-lateral)+.001F>=std::abs(actual-lateral);
        if(!moving_away&&crosses&&std::min(gap,future_gap)<5.5F&&std::max(gap,future_gap)>-5.5F)return false;
        if(!moving_away&&std::abs(lateral-target)<1.4F) {
            if(gap<0&&gap> -10.F-std::max(0.F,speed-self.speed)*1.5F)return false;
            // A slow horse makes a NEW destination unattractive. Once we
            // occupy the side, this is not an emergency: instantaneous gait
            // changes must not repeatedly steer us back into its lane.
            // Swept body occupancy and fast rear traffic above still apply.
            if(gap>=0&&gap<40&&speed<free_speed-.5F&&purpose==TrafficPurpose::NewPass)return false;
        }
    }
    return true;
}
}
struct Session {
    int horse=0,blocker=0;const Vector3* geometry=nullptr;
    float left_offset=0,target=0,inspection_speed=0;double next_decision=-1;
    void Reset(){*this={};}
    float Update(const course::Path& legal,float half_width,const race_pace::Plan& plan,
        const obstacle_map::Map* map,const following::Mount& self,std::span<const following::Mount> roster,
        double now,float inspection_end=0,float free_target_mps=0,bool allow_new=true) {
        if(!self.horse||!destination::Finite(self.position)||!std::isfinite(self.speed)||self.speed<0||
           !std::isfinite(self.along)||!std::isfinite(now)||plan.path.points.size()<2){Reset();return 0;}
        const bool replaced=geometry&&(geometry!=plan.path.points.data()||horse!=self.horse);
        if(geometry!=plan.path.points.data()||horse!=self.horse){Reset();geometry=plan.path.points.data();horse=self.horse;}
        if(replaced){next_decision=now+kDecisionSeconds;return 0;}
        if(now<next_decision)return left_offset;
        next_decision=now+kDecisionSeconds;
        const auto frame=detail::At(plan,self.along);
        if(std::hypot(frame.x,frame.y)<.5F)return left_offset;
        const float projection=-frame.ox*frame.y+frame.oy*frame.x;
        if(projection<.5F)return left_offset;
        const float actual=(-(self.position.x-frame.point.x)*frame.y+(self.position.y-frame.point.y)*frame.x)/projection;
        const float free_speed=std::max(self.speed,
            free_target_mps>0&&std::isfinite(free_target_mps)?free_target_mps:plan.dynamics.top_mps);
        // Native avoidance can react to the horse's heading before the
        // private waypoint tangent turns. Reuse its observed forward vector
        // to recognise that front rider, without changing the task line or
        // weakening the independent space/traffic certificate below.
        auto heading=frame;
        const float heading_length=std::hypot(self.forward.x,self.forward.y);
        const bool heading_valid=destination::Finite(self.forward)&&std::isfinite(heading_length)&&heading_length>=.5F;
        if(heading_valid){heading.x=self.forward.x/heading_length;heading.y=self.forward.y/heading_length;}
        int ahead=0;float nearest=kTriggerDistance*kTriggerDistance+.001F;bool keep=false;
        for(const auto& other:roster) {
            float gap,lateral,speed,sideways;
            const float dx=other.position.x-self.position.x,dy=other.position.y-self.position.y;
            const float distance2=dx*dx+dy*dy;
            const bool close=std::isfinite(distance2)&&distance2<=kTriggerDistance*kTriggerDistance;
            const bool in_front=!heading_valid || dx*heading.x+dy*heading.y>0;
            if(detail::Neighbor(self,other,frame,gap,lateral,speed,sideways)) {
                if(other.horse==blocker&&gap> -6)keep=true;
                // Preserve the existing planned-line recognition as well.
                if(close&&in_front&&gap>0&&distance2<nearest&&std::abs(actual*projection+lateral)<1.4F&&free_speed>speed+.5F) {
                    ahead=other.horse;nearest=distance2;
                }
            }
            if(heading_valid&&detail::Neighbor(self,other,heading,gap,lateral,speed,sideways)&&
                close&&gap>0&&distance2<nearest&&std::abs(lateral)<1.4F&&free_speed>speed+.5F) {
                ahead=other.horse;nearest=distance2;
            }
        }
        if(target==0&&left_offset==0&&(!ahead||!allow_new)){inspection_speed=0;return 0;}
        const auto complete=[&] {target=left_offset=inspection_speed=0;blocker=0;return 0.F;};
        // A selected destination whose first emitted scalar was refused is
        // still pending, not an occupied side. Its first grant still needs
        // this same front blocker within the current three-metre observation.
        if(target!=0&&left_offset==0&&(!allow_new||ahead!=blocker))return complete();
        // Keep one inspection speed for this active episode. Free intent is
        // already known before native avoidance slows the horse. A transient
        // slowdown must not shrink the window, grant a side, then revoke it
        // again as speed recovers. Reset it when the side episode ends.
        inspection_speed=target!=0||left_offset!=0?std::max(inspection_speed,free_speed):free_speed;
        detail::Window window;
        if(!window.Build(legal,half_width,plan,map,self.along,inspection_speed,inspection_end)) {
            // The original unshifted task already owns its distant route.
            // Missing future offset evidence may still permit a positively
            // measured local merge back, instead of extending the old side
            // request into an approaching narrow/unknown section forever.
            if(left_offset==0)return complete();
            window={};
            if(!window.Build(legal,half_width,plan,map,self.along,self.speed,0,
                detail::Window::Transition(actual,0,self.speed)+3.F)||
               !detail::TrafficClear(self,roster,frame,actual,0,free_speed,detail::TrafficPurpose::Merge)||
               (!window.Return(actual,self)&&!(left_offset*actual>0&&window.Recover(actual,self))))return left_offset;
            // Zero restores the original native task; it does not teleport
            // the horse. Do not keep sending a smaller nonzero scalar toward
            // distant nodes whose side room has just lost its certificate.
            return complete();
        }
        auto allowed=[&](float candidate,bool returning) {
            return detail::TrafficClear(self,roster,frame,actual,candidate,free_speed,
                returning?detail::TrafficPurpose::Merge:detail::TrafficPurpose::NewPass,returning?0:ahead)&&
                (returning?(window.Return(actual,self)||(left_offset*actual>0&&window.Recover(actual,self))):window.Allows(actual,candidate,self));
        };
        const auto active_safe=[&](float candidate) {
            return detail::TrafficClear(self,roster,frame,actual,candidate,free_speed,detail::TrafficPurpose::ActivePass,blocker)&&
                window.Allows(actual,candidate,self);
        };
        if(target!=0) {
            // Pace eligibility controls new grants. It does not invalidate
            // the safety of an already occupied side or force a merge.
            if(!keep&&!ahead&&allowed(0,true))return complete();
            else if(!active_safe(target)) {
                // The intended full offset may lose its distant certificate
                // while the smaller emitted offset remains supported. Hold
                // only with full-window proof AND a certified safe return.
                if(left_offset!=0&&active_safe(left_offset)&&allowed(0,true))return left_offset;
                if(allowed(0,true))return complete();
                else return left_offset;
            }
        } else if(left_offset!=0) {
            if(!allowed(0,true))return left_offset;
            return complete();
        } else if(ahead&&allow_new) {
            // Retain current real-side preference. Entity parity is only the
            // centreline tie break; actual side occupancy always takes priority.
            const float sign=std::abs(actual)>.3F?(actual>0?1.F:-1.F):(self.horse%2?1.F:-1.F);
            for(float candidate:{sign*kOffset,-sign*kOffset}) {
                if(std::abs(left_offset)>.05F&&candidate*left_offset<0)continue;
                if(std::abs(actual)>.5F&&candidate*actual<0)continue;
                if(allowed(candidate,false)){target=candidate;blocker=ahead;break;}
            }
        }
        // Native receives a gentle ramp, never an instantaneous opposite-side
        // jump. The full destination band was certified before this first step.
        constexpr float step=.45F;
        const float emitted=left_offset+std::clamp(target-left_offset,-step,step);
        // A horse may already be farther out than the chosen target while
        // this session still requests zero. Its first ramp value can then
        // lie OUTSIDE the actual-to-target band. Certify the value actually
        // sent, including the occupied space swept on the way to that value.
        // The ramp is a transient sweep, not the final passing lane. The
        // distant slow leader was checked against the selected destination;
        // actual/closing body occupancy and fast rear traffic still veto it.
        if(!detail::TrafficClear(self,roster,frame,actual,emitted,free_speed,detail::TrafficPurpose::ActivePass,blocker) ||
           !window.Allows(actual,emitted,self))return left_offset;
        left_offset=emitted;
        if(std::abs(left_offset)<.001F)left_offset=0;
        return left_offset;
    }
};
} // namespace horse_races::native_passing
