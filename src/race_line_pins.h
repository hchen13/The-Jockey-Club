#pragma once
#include "race_racing_line.h"
#include "race_racing_line_negative.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

// Stretches of a corridor course where the rivals ride the USER'S line, not a
// computed one. The two right-angle bends inside Saint Denis on the camp duel
// (2026-09-19/20): the computed line cut them under the arcades, the host fell
// there four times in one trial and the place was slowed to 19 km/h, while the
// user rides both bends flat out along a line the solver never finds. The
// user's answer: "this stretch he does not compute; he rides mine".
//
// A stretch is the user's own ride, recorded by the F11 route recorder (one
// point per metre of travel) and kept with the STATION, as world points, in
//   <game>\TheJockeyClub\courses\<station>.pins.txt
// (tools/pin_line_from_capture.py writes it from a capture). Nothing here is
// in centreline metres: the points are projected onto the centreline in force
// when the course is bound, so a stretch recorded again elsewhere on the same
// line - the Clemens Point splice of 2026-09-19 changed every metre after it -
// leaves the pinned bends good. A point that projects beyond the corridor
// refuses its stretch: the recording is of some other road.
//
// Build supplies the recorded offsets as constraints BEFORE K1999, so the
// optimizer shapes the approach and exit with the same pins in force. A later
// blend must not overwrite that jointly solved geometry. Apply remains for
// terrain overlays and the explicit aggression-zero compatibility path.
// Recorded offsets are boxed to the usable width. The same corridor, order
// and obstacle checks still apply; incompatible optional constraints retry
// the ordinary computed line. Required centreline stretches refuse instead.
// Only the legacy Apply overlay can pull its
// offsets towards the centreline to satisfy geometry. Historical pins never
// grant permission to override current obstacle evidence.
namespace horse_races::line_pins {
inline constexpr float kBlendMetres=20;    // Either side of a stretch, the line eases between the computed and the recorded offset.
inline constexpr float kMinimumMetres=10;  // A shorter stretch is a slip of the recorder's finger.
inline constexpr float kBeyondMetres=2;    // A recorded point this far outside the corridor: the wrong road, refused.
inline constexpr float kStepMetres=.5F;    // Recorded points closer than this along the centreline are one point.

struct Segment {std::string note;std::vector<Vector3> points;bool required_centerline=false;};
// The file: "pins <station>", then "segment <note>" lines each followed by
// its "point x y z" lines. "centerline <note>" identifies a mandatory
// recorded-centre stretch using the same world anchors. "#"/";" are comments.
struct File {
    std::string station;
    std::vector<Segment> segments;
    bool required_requested=false;
    bool HasRequired() const {return required_requested||std::any_of(segments.begin(),segments.end(),
        [](const auto& s){return s.required_centerline;});}
    std::size_t Points() const {std::size_t n=0;for(const auto& s:segments)n+=s.points.size();return n;}
    bool Parse(const std::string& text,std::string* error=nullptr) {
        station.clear();segments.clear();required_requested=false;
        // Retain the requirement even when an earlier optional ride is malformed.
        // Binding must never turn a damaged mandatory file into an ordinary line.
        std::istringstream directives(text);std::string directive;
        while(std::getline(directives,directive)) {
            std::istringstream words(directive);std::string word;words>>word;
            required_requested|=word=="centerline";
        }
        std::istringstream in(text);std::string line;int number=0;
        auto fail=[&](const std::string& why){if(error)*error="line "+std::to_string(number)+": "+why;station.clear();segments.clear();return false;};
        while(std::getline(in,line)) {
            ++number;
            if(!line.empty() && line.back()=='\r')line.pop_back();
            const auto first=line.find_first_not_of(" \t");
            if(first==std::string::npos || line[first]=='#' || line[first]==';')continue;
            std::istringstream words(line.substr(first));std::string word;words>>word;
            if(word=="pins") {
                if(!station.empty())return fail("a second header");
                words>>station;if(station.empty())return fail("no station after 'pins'");
            } else if(word=="segment"||word=="centerline") {
                required_requested|=word=="centerline";
                if(station.empty())return fail("'segment' before the 'pins <station>' header");
                std::string note;std::getline(words,note);
                const auto start=note.find_first_not_of(" \t");
                segments.push_back({start==std::string::npos?std::string():note.substr(start),{},word=="centerline"});
            } else if(word=="point") {
                if(segments.empty())return fail("'point' before any 'segment'");
                Vector3 p{};
                if(!(words>>p.x>>p.y>>p.z) || !std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z))return fail("a point needs three finite numbers");
                segments.back().points.push_back(p);
            } else return fail("unknown word '"+word+"'");
        }
        if(station.empty())return fail("no 'pins <station>' header");
        return true;
    }
    std::string Serialize() const {
        std::ostringstream o;o.precision(6);o<<std::fixed;
        o<<"pins "<<station<<'\n';
        for(const auto& s:segments) {
            o<<(s.required_centerline?"centerline ":"segment ")<<s.note<<'\n';
            for(const auto& p:s.points)o<<"point "<<p.x<<' '<<p.y<<' '<<p.z<<'\n';
        }
        return o.str();
    }
};

struct Pin {float along=0,offset=0;};   // Centreline metres; signed metres, + left of travel.
struct Stretch {
    std::vector<Pin> pins;   // Ordered by along, strictly increasing.
    float from=0,to=0;       // pins.front().along .. pins.back().along.
    std::string note;
    float max_offset=0;
    bool required_centerline=false;
    bool Covers(float along) const {return along>=from && along<=to;}
    // The recorded offset at centreline metres `along`, linear between pins; the end pin's beyond the ends.
    float Offset(float along) const {
        if(pins.empty())return 0;
        if(along<=pins.front().along)return pins.front().offset;
        if(along>=pins.back().along)return pins.back().offset;
        const auto it=std::upper_bound(pins.begin(),pins.end(),along,[](float a,const Pin& p){return a<p.along;});
        const auto& b=*it;const auto& a=*(it-1);
        const float span=b.along-a.along;
        return span>0?a.offset+(b.offset-a.offset)*(along-a.along)/span:a.offset;
    }
};
struct Projection {
    std::vector<Stretch> stretches;     // Accepted, ordered by `from`, non-overlapping.
    std::vector<std::string> refused;   // One reason per refused segment.
    bool required_refused=false;
    std::size_t Pins() const {std::size_t n=0;for(const auto& s:stretches)n+=s.pins.size();return n;}
};
// The signed lateral (+ left of travel) of `p` about centreline segment `i`.
inline float Lateral(const course::Path& centreline,std::size_t i,Vector3 p) {
    const auto& a=centreline.points[i];const auto& b=centreline.points[std::min(i+1,centreline.points.size()-1)];
    const float dx=b.x-a.x,dy=b.y-a.y;
    return dx*(p.y-a.y)-dy*(p.x-a.x);
}
// Every segment of `file` onto `centreline`, for a corridor of `half_width`.
// The first point is found on the whole line, the rest within a window of
// the previous one (the line may pass a place twice); a point running back
// along the line is dropped, one beyond the corridor by kBeyondMetres
// refuses the segment, and so does a stretch shorter than kMinimumMetres or
// one overlapping a stretch already accepted.
inline Projection Project(const course::Path& centreline,const File& file,float half_width) {
    Projection out;
    if(centreline.points.size()<2 || !(half_width>0))return out;
    const float window=std::max(60.0F,3*half_width+30);
    for(const auto& s:file.segments) {
        auto refuse=[&](const std::string& why){out.refused.push_back("'"+s.note+"': "+why);out.required_refused|=s.required_centerline;};
        if(s.points.size()<3){refuse("fewer than three points");continue;}
        Stretch st;st.note=s.note;st.required_centerline=s.required_centerline;bool bad=false;std::string why;
        for(std::size_t k=0;k<s.points.size() && !bad;++k) {
            const Vector3 p=s.points[k];
            if(!destination::Finite(p)){bad=true;why="point "+std::to_string(k)+" is not finite";break;}
            const auto pr=st.pins.empty()?course::Project(centreline,p):racing_line::detail::ProjectNear(centreline,p,st.pins.back().along,window);
            if(!pr.valid){bad=true;why="point "+std::to_string(k)+" finds no centreline";break;}
            if(s.required_centerline&&std::abs(p.z-obstacle_map::Map::At(centreline,pr.along).z)>kBeyondMetres) {
                bad=true;why="point "+std::to_string(k)+" identifies a different recorded height layer";break;
            }
            if(pr.distance>(s.required_centerline?kBeyondMetres:half_width+kBeyondMetres)) {
                char b[180];
                if(s.required_centerline)std::snprintf(b,sizeof b,"point %zu lies %.1f m from the required recorded centre, beyond %.1f m anchor tolerance",k,static_cast<double>(pr.distance),static_cast<double>(kBeyondMetres));
                else std::snprintf(b,sizeof b,"point %zu lies %.1f m from the centreline, outside the %.0f m corridor: another road",k,static_cast<double>(pr.distance),static_cast<double>(half_width));
                bad=true;why=b;break;
            }
            if(!st.pins.empty() && pr.along<st.pins.back().along+kStepMetres)continue;   // Standing, or a step back.
            const float offset=s.required_centerline?0:(Lateral(centreline,pr.segment,p)>=0?pr.distance:-pr.distance);
            st.pins.push_back({pr.along,offset});
            st.max_offset=std::max(st.max_offset,std::abs(offset));
        }
        if(bad){refuse(why);continue;}
        if(st.pins.size()<3){refuse("fewer than three points along the centreline");continue;}
        st.from=st.pins.front().along;st.to=st.pins.back().along;
        if(st.to-st.from<kMinimumMetres){refuse("shorter than "+std::to_string(static_cast<int>(kMinimumMetres))+" m along the centreline");continue;}
        bool overlaps=false;
        for(const auto& other:out.stretches)if(st.from<=other.to+kBlendMetres && other.from<=st.to+kBlendMetres)overlaps=true;
        if(overlaps){refuse("overlaps another stretch (or lies within its blend)");continue;}
        out.stretches.push_back(std::move(st));
    }
    std::sort(out.stretches.begin(),out.stretches.end(),[](const Stretch& a,const Stretch& b){return a.from<b.from;});
    return out;
}
inline const Stretch* Covering(const std::vector<Stretch>& stretches,float along) {
    for(const auto& s:stretches)if(s.Covers(along))return &s;
    return nullptr;
}
inline bool Covers(const std::vector<Stretch>& stretches,float along){return Covering(stretches,along)!=nullptr;}
inline bool Required(const std::vector<Stretch>& stretches) {
    return std::any_of(stretches.begin(),stretches.end(),[](const auto& s){return s.required_centerline;});
}
// A required centre stretch owns both ends of every intersecting chord,
// including the two boundary chords; constraining interior nodes alone lets
// interpolation cut sideways inside the authored entrance/exit.
inline bool OwnsNode(const course::Path& centre,std::size_t i,const Stretch& s) {
    if(!s.required_centerline)return s.Covers(centre.metres[i]);
    const auto before=i?i-1:i,after=std::min(i+1,centre.metres.size()-1);
    return centre.metres[before]<=s.to&&centre.metres[after]>=s.from;
}
struct Applied {
    int pinned=0,blended=0,clamped=0,rounds=0;
    float max_offset=0;
    bool ok=false;
    std::string why;
};
// Recheck provenance after an optional overlay: it may have changed an
// authored node. This only narrows a caller's existing success flag; matching
// an ordinary fallback by coincidence must not upgrade it to authored.
inline bool Matches(const course::Path& centreline,const racing_line::Params& params,
    const std::vector<Stretch>& stretches,const racing_line::Result& out,bool required_only=false) {
    if(required_only&&!Required(stretches))return true;
    const auto n=centreline.points.size();
    if(n<3||stretches.empty()||!params.Valid()||!(params.Usable()>0)||!out.verified||
       centreline.metres.size()!=n||out.offsets.size()!=n||out.reference.size()!=n||
       out.path.points.size()!=n||out.path.metres.size()!=n)return false;
    const float box=std::max(0.0F,params.Usable()-.02F);bool matched=false;
    for(std::size_t i=0;i<n;++i) {
        if(!std::isfinite(centreline.metres[i])||out.reference[i]!=centreline.metres[i]||
           !std::isfinite(out.offsets[i])||!std::isfinite(out.path.metres[i])||
           !destination::Finite(centreline.points[i])||!destination::Finite(out.path.points[i]))return false;
        if(i&&(!(centreline.metres[i]>centreline.metres[i-1])||!(out.path.metres[i]>out.path.metres[i-1])))return false;
        if(i==0||i+1==n)continue;
        for(const auto& stretch:stretches)if((!required_only||stretch.required_centerline)&&OwnsNode(centreline,i,stretch)) {
            const float expected=stretch.required_centerline?0:stretch.Offset(centreline.metres[i]);
            if(!std::isfinite(expected)||std::abs(out.offsets[i]-std::clamp(expected,-box,box))>.001F)return false;
            if(stretch.required_centerline&&destination::Distance(out.path.points[i],centreline.points[i])>.001F)return false;
            matched=true;
            break;
        }
    }
    return matched;
}
// Lay `stretches` over a built line (racing_line::Build's Result for
// `centreline` under `params`). On success the Result is rewritten - path,
// offsets, max_offset, the "after" measures - with pinned_nodes set; on
// failure it is untouched and `why` says what could not be made legal.
inline Applied Apply(const course::Path& centreline,const racing_line::Params& params,const std::vector<Stretch>& stretches,racing_line::Result& out) {
    Applied a;
    const std::size_t n=centreline.points.size();
    if(stretches.empty()){a.ok=true;a.why="no stretches";return a;}
    if(Required(stretches)){a.why="required recorded centreline must constrain the solve before optimization";return a;}
    if(n<3 || out.offsets.size()!=n || out.reference.size()!=n || !params.Valid()){a.why="the line and the centreline disagree";return a;}
    for(std::size_t i=0;i<n;++i)if(out.reference[i]!=centreline.metres[i]){a.why="the line's reference metres are not the centreline's";return a;}
    const float usable=params.Usable();
    if(!(usable>0)){a.why="no usable width";return a;}
    const float box=std::max(0.0F,usable-.02F),limit=usable-.01F;
    std::vector<racing_line::detail::Vec2> normal;
    racing_line::detail::Frame(centreline,normal);
    // 1. The offsets: recorded inside a stretch, eased over the blend outside it.
    std::vector<float> offsets=out.offsets;
    std::vector<char> touched(n,0);
    for(std::size_t i=1;i+1<n;++i) {
        const float r=centreline.metres[i];
        for(const auto& s:stretches) {
            if(r<s.from-kBlendMetres || r>s.to+kBlendMetres)continue;
            float target=0,w=1;
            if(s.Covers(r)){target=s.Offset(r);++a.pinned;}
            else {
                const float d=r<s.from?s.from-r:r-s.to;
                const float t=std::clamp(1-d/kBlendMetres,0.0F,1.0F);
                w=t*t*(3-2*t);   // Smooth: no kink where the blend begins or ends.
                target=r<s.from?s.pins.front().offset:s.pins.back().offset;++a.blended;
            }
            if(std::abs(target)>box){target=std::clamp(target,-box,box);++a.clamped;}
            offsets[i]=w*target+(1-w)*offsets[i];touched[i]=1;
            break;
        }
    }
    // 2. Corridor/order post-conditions first, followed by an independent
    //    obstacle check. Never restore an old pin through a newly learned hit.
    std::vector<Vector3> q(n);
    const float window=std::max(60.0F,3*usable+30);
    auto beyond=[&](Vector3 m,std::size_t segment) {
        const auto& p=centreline.points[segment];const auto& b2=centreline.points[segment+1];
        const float dx=b2.x-p.x,dy=b2.y-p.y,length2=dx*dx+dy*dy;
        const float t=length2>0?std::clamp(((m.x-p.x)*dx+(m.y-p.y)*dy)/length2,0.0F,1.0F):0.0F;
        return std::hypot(m.x-p.x-t*dx,m.y-p.y-t*dy)>limit;
    };
    bool verified=false;
    for(int round=0;round<12 && !verified;++round) {
        ++a.rounds;
        for(std::size_t i=0;i<n;++i) {
            const auto& p=centreline.points[i];
            q[i]={static_cast<float>(p.x+offsets[i]*normal[i].x),static_cast<float>(p.y+offsets[i]*normal[i].y),p.z};
        }
        std::vector<char> bad(n,0);bool ok=true;float previous=-1;
        for(std::size_t i=0;i<n;++i) {
            const auto node=racing_line::detail::ProjectNear(centreline,q[i],centreline.metres[i],window);
            if(!node.valid || node.distance>limit || !(node.along>previous)){bad[i]=1;ok=false;}
            if(node.valid)previous=node.along;
            if(i) {
                const float len=destination::Distance(q[i-1],q[i]);
                if(len<.05F || len>250.0F){bad[i-1]=bad[i]=1;ok=false;}
                const auto& c0=centreline.points[i-1];const auto& c1=centreline.points[i];
                if((q[i].x-q[i-1].x)*(c1.x-c0.x)+(q[i].y-q[i-1].y)*(c1.y-c0.y)<=.01F){bad[i-1]=bad[i]=1;ok=false;}
                const int samples=static_cast<int>(std::ceil(len/params.verify_step));
                for(int s2=1;s2<samples;++s2) {
                    const float t=static_cast<float>(s2)/samples;
                    const Vector3 m{q[i-1].x+(q[i].x-q[i-1].x)*t,q[i-1].y+(q[i].y-q[i-1].y)*t,q[i-1].z+(q[i].z-q[i-1].z)*t};
                    if(beyond(m,i-1)){bad[i-1]=bad[i]=1;ok=false;break;}
                }
            }
        }
        if(ok){verified=true;break;}
        // Offenders and their neighbours half way back to the centreline -
        // only where the pins reached; a computed node elsewhere was proven
        // by Build and is left alone unless it is itself the offender.
        std::vector<float> next=offsets;bool changed=false;
        for(std::size_t i=0;i<n;++i)if(bad[i]) {
            for(std::size_t j=(i?i-1:0);j<=i+1 && j<n;++j)if((touched[j] || bad[j]) && j!=0 && j+1!=n && std::abs(offsets[j])>1e-4F){next[j]=offsets[j]*.5F;changed=true;}
        }
        if(!changed){a.why="a node no pin reached fails the corridor check";return a;}
        offsets=next;
    }
    if(!verified){a.why="the recorded offsets could not be made legal in 12 rounds";return a;}
    const racing_line::detail::NegativeClearance negative(centreline,params);
    for(std::size_t i=1;i<n;++i)if(!negative.Allows(q[i-1],q[i],i-1)) {
        a.why="recorded overlay conflicts with current obstacle evidence at "+
            std::to_string(centreline.metres[i-1])+" m";
        return a;
    }
    auto retained=out.recorded_chords;
    if(params.obstacles&&offsets!=out.offsets) {
        std::vector<racing_line::detail::SectionBound> sections,rows;
        std::vector<unsigned char> mask;bool open=false;
        racing_line::detail::ObstacleSections(centreline,*params.obstacles,params.ProofClearance(),
            limit,sections,rows,mask,open);
        const std::vector<unsigned char> none(n-1,0);
        racing_line::detail::ChordVerifier proof(centreline,params,rows,none,usable,limit);
        if(retained.size()!=n-1)retained.assign(n-1,0);
        for(std::size_t i=0;i+1<n;++i)if(offsets[i]!=out.offsets[i]||offsets[i+1]!=out.offsets[i+1]) {
            const bool measured=proof.Measured(q[i],q[i+1],i);
            if(!measured&&(offsets[i]!=0||offsets[i+1]!=0)) {
                a.why="recorded overlay lacks positive physical coverage at "+std::to_string(centreline.metres[i])+" m";return a;
            }
            retained[i]=!measured;
        }
    }
    course::Path path;
    if(!path.Build(q)){a.why="the pinned line does not build as a path";return a;}
    // Pins/blends are checked against corridor and order here. The planner's
    // measured obstacle certificate applies to its original polyline only.
    if(offsets!=out.offsets)out.obstacle_verified=false;
    out.path=std::move(path);out.offsets=std::move(offsets);
    out.recorded_chords=std::move(retained);
    out.recorded_segments=static_cast<int>(std::count(out.recorded_chords.begin(),out.recorded_chords.end(),1));
    out.max_offset=0;for(float o:out.offsets)out.max_offset=std::max(out.max_offset,std::abs(o));
    racing_line::detail::Measure(out.path,params.dynamics,out.peak_curvature_after,out.mean_curvature_after,out.nominal_seconds_after);
    out.length_after=out.path.Length();
    out.pinned_nodes=a.pinned;out.verified=true;
    a.max_offset=out.max_offset;a.ok=true;
    return a;
}
// One production entry for authored pins and computed geometry. Refusing
// optional pins retries the ordinary planner under the same physical inputs;
// it does not make the whole race fail or override a newly learned impact.
template<class Optimizer=racing_line::detail::K1999Optimizer>
inline bool Build(const course::Path& centreline,const racing_line::Params& params,
    const std::vector<Stretch>& stretches,racing_line::Result& out,Applied* applied=nullptr,Optimizer optimizer={}) {
    Applied a;const bool required=Required(stretches);
    auto finish=[&](bool ok){if(!ok&&required)out={};if(applied)*applied=a;return ok;};
    auto ordinary=[&] {if(required){a.why="required recorded centreline is incompatible; no computed fallback";return false;}
        return racing_line::Build(centreline,params,out,optimizer);};
    const auto n=centreline.points.size();
    if(!params.Valid()||n<2||centreline.metres.size()!=n)return finish(ordinary());
    if((params.fixed_offsets&&params.fixed_offsets->size()!=n)||
       (params.fallback_offsets&&params.fallback_offsets->size()!=n))return finish(ordinary());
    if(stretches.empty()) {a.ok=true;a.why="no stretches";return finish(ordinary());}
    for(const auto& s:stretches)if(s.required_centerline&&
        (s.pins.size()<3||!std::isfinite(s.from)||!std::isfinite(s.to)||s.to-s.from<kMinimumMetres)) {
        a.why="required recorded centreline has invalid anchors";return finish(false);
    }
    // Exactly zero historically overlays the user's ride on the identity.
    // Keep that explicit setting's existing easing and refusal semantics.
    if(params.Aggression()==0&&!required) {
        if(!ordinary())return finish(false);
        a=Apply(centreline,params,stretches,out);
        out.authored_constraints=a.ok&&Matches(centreline,params,stretches,out);
        return finish(true);
    }
    std::vector<float> fixed(n,NAN);
    if(params.fixed_offsets)fixed=*params.fixed_offsets;
    const float box=std::max(0.0F,params.Usable()-.02F);
    if(fixed.size()==n)for(std::size_t i=1;i+1<n;++i)for(const auto& stretch:stretches)if(OwnsNode(centreline,i,stretch)) {
        const float target=stretch.required_centerline?0:stretch.Offset(centreline.metres[i]);
        if(!std::isfinite(target)){a.why="a recorded offset is not finite";return finish(ordinary());}
        if(stretch.required_centerline&&std::isfinite(fixed[i])&&std::abs(fixed[i]-target)>.001F) {
            a.why="required recorded centreline conflicts with an existing fixed offset";return finish(false);
        }
        fixed[i]=std::clamp(target,-box,box);++a.pinned;if(fixed[i]!=target)++a.clamped;break;
    }
    // Authored transitions cannot become a retained side bypass through an
    // unknown connection. The same solver must prove their measured joins.
    auto constrained=params;constrained.fixed_offsets=&fixed;
    racing_line::Result candidate;
    bool accepted=a.pinned>0&&racing_line::Build(centreline,constrained,candidate,optimizer);
    if(accepted)for(std::size_t i=0;i<n;++i)if(std::isfinite(fixed[i])&&
        (!std::isfinite(candidate.offsets[i])||std::abs(candidate.offsets[i]-fixed[i])>.001F))accepted=false;
    accepted=accepted&&Matches(centreline,params,stretches,candidate);
    if(accepted) {
        out=std::move(candidate);out.authored_constraints=true;a.ok=true;a.rounds=out.verification_rounds;a.max_offset=out.max_offset;
        return finish(true);
    }
    a.why="recorded pins conflict with current physical or geometric constraints; computed line retained";
    a.pinned=a.clamped=0;
    if(required) {
        std::vector<Stretch> mandatory;
        for(const auto& s:stretches)if(s.required_centerline)mandatory.push_back(s);
        if(mandatory.size()<stretches.size()&&Build(centreline,params,mandatory,out,&a,optimizer)) {
            if(params.Aggression()==0) {
                // Preserve the explicit zero-utilization legacy ride overlay
                // away from mandatory centre stretches. It cannot replace
                // or shift a required boundary chord.
                std::vector<Stretch> optional;
                for(const auto& s:stretches)if(!s.required_centerline)optional.push_back(s);
                auto candidate=out;const auto overlaid=Apply(centreline,params,optional,candidate);
                if(overlaid.ok&&Matches(centreline,params,mandatory,candidate,true)) {
                    out=std::move(candidate);out.authored_constraints=Matches(centreline,params,stretches,out);
                    a.pinned+=overlaid.pinned;a.blended+=overlaid.blended;
                    a.max_offset=out.max_offset;a.why="required centre and legacy zero-utilization ride retained";
                    return finish(true);
                }
            }
            out.authored_constraints=false;
            a.why="required recorded centreline accepted; optional recorded pins refused";
            return finish(true);
        }
    }
    return finish(ordinary());
}
} // namespace horse_races::line_pins
