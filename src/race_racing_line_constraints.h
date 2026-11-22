#pragma once
#include "race_racing_line_corridor.h"

namespace horse_races::racing_line::detail {
struct Constraint {
    std::size_t i=0,j=0;
    double a=0,b=0,lo=0,hi=0;
    double Value(const std::vector<double>& d) const{return a*d[i]+b*d[j];}
};
struct SectionBound {
    float along=0;
    obstacle_map::Section section;
    double lo=0,hi=0;
    std::size_t row=0;
    bool recorded=false;
    double incoming_lo=0,incoming_hi=0;
    bool incoming_recorded=false;
    std::size_t source=0;
    bool source_cached=false;
    double incoming_from_lo=NAN,incoming_from_hi=NAN;
    double seed=0;
};
// A branch state is a measured connection strip, not a guessed overlap of
// free islands in two independent rows. The extra zero state means recorded
// only; its cost is compared lexicographically before lateral distance, so
// an available measured outer bypass always wins over giving up clearance.
struct BranchState {
    obstacle_map::Interval from,span;
    bool recorded=false;
    double lost=1e30,shape=1e30,at=0;
    int previous=-1;
    obstacle_map::Interval physical_from,physical_span;
};
inline bool BestIntersection(obstacle_map::Interval a,obstacle_map::Interval b,
    const std::vector<obstacle_map::Interval>& free,obstacle_map::Interval& out) {
    bool found=false;double best=1e30;
    for(auto f:free) {
        const float lo=std::max({a.lo,b.lo,f.lo}),hi=std::min({a.hi,b.hi,f.hi});
        if(lo>hi)continue;
        const double nearest_offset=std::clamp(0.0,static_cast<double>(lo),static_cast<double>(hi));
        if(!found||nearest_offset*nearest_offset<best){out={lo,hi};best=nearest_offset*nearest_offset;found=true;}
    }
    return found;
}
// Return row and connection bounds in their own actual frames. A virtual
// recorded strip is never certified by this routine. All allocations and
// the longitudinal branch selection happen once per preparation/rebuild.
// `keep` is positive-proof body clearance; known-obstacle tracking reserve
// remains an independent NegativeClearance veto on every submitted chord.
inline void ObstacleSections(const course::Path& centre,const obstacle_map::Map& map,
    float keep,float box,std::vector<SectionBound>& sections,
    std::vector<SectionBound>& rows,std::vector<unsigned char>& recorded,bool& unconstrained,
    const obstacle_gap_view::View* estimates=nullptr) {
    const std::size_t m=map.rows.size();sections.clear();rows.clear();recorded.assign(centre.points.size()-1,0);
    unconstrained=m>0;
    if(!m){std::fill(recorded.begin(),recorded.end(),static_cast<unsigned char>(1));return;}
    std::vector<std::vector<obstacle_map::Interval>> free(m),physical_free(m),legal(m);
    std::vector<std::vector<BranchState>> states(m);
    std::vector<obstacle_map::Section> frames(m);
    auto full_width=[&](const std::vector<obstacle_map::Interval>& spans) {
        return std::any_of(spans.begin(),spans.end(),[&](auto span){return span.lo<=-box&&span.hi>=box;});
    };
    for(std::size_t r=0;r<m;++r){
        frames[r]=map.SectionFor(centre,r);
        legal[r]=CorridorSpans(centre,frames[r],map.Along(r),box,map.width);
        const auto clear=map.rows[r].ClearIntervals(keep);
        physical_free[r]=obstacle_map::Intersect(clear,{{-map.width,map.width}});
        free[r]=obstacle_map::Intersect(physical_free[r],legal[r]);
        if(!full_width(clear))unconstrained=false;
    }
    states[0].push_back({{0,0},{0,0},false,0,0,0,-1});
    for(std::size_t r=1;r<m;++r) {
        const auto links=obstacle_gap_view::EffectiveStrips(map,estimates,r,keep);
        bool wide=false;
        for(auto link:links) {
          // A swept connection cannot extend the independent endpoint proof.
          // These normalized islands remain separate; iterating intersections
          // avoids allocating another pair of vectors for every connection.
          for(auto measured_from:physical_free[r-1]) {
            const obstacle_map::Interval certified_from{std::max(link.from.lo,measured_from.lo),std::min(link.from.hi,measured_from.hi)};
            if(certified_from.lo>certified_from.hi)continue;
          for(auto measured_to:physical_free[r]) {
            const obstacle_map::Interval certified_to{std::max(link.to.lo,measured_to.lo),std::min(link.to.hi,measured_to.hi)};
            if(certified_to.lo>certified_to.hi)continue;
            strip_geometry::Patch physical{frames[r-1],frames[r],certified_from,certified_to};
            if(!strip_geometry::ClipRegular(physical))continue;
            const auto a=obstacle_map::Intersect({physical.entry},legal[r-1]);
            const auto b=obstacle_map::Intersect({physical.exit},legal[r]);
            if(a.empty()||b.empty())continue;
            // Detect inactive physical constraints before trimming a fold
            // in the section coordinates. The ordinary corridor solver
            // already enforces geometric order; an open survey must not
            // switch its numerical solve. Actual cuts and whole chords
            // still pass the same final clearance checks below.
            wide|=full_width({certified_from})&&full_width({certified_to});
            // A folded far edge must not discard the usable centre of a
            // strip. Select branches on its positive-order sub-patch.
            for(auto entry:a)for(auto exit:b) {
                strip_geometry::Patch patch{frames[r-1],frames[r],entry,exit};
                if(strip_geometry::ClipRegular(patch)) {
                    BranchState state;state.from=patch.entry;state.span=patch.exit;
                    state.physical_from=physical.entry;state.physical_span=physical.exit;states[r].push_back(state);
                }
            }
          }
          }
        }
        if(!wide)unconstrained=false;
        states[r].push_back({{0,0},{0,0},true});
        const double length=std::max(.01F,map.Along(r)-map.Along(r-1));
        for(auto& current:states[r])for(std::size_t p=0;p<states[r-1].size();++p) {
            const auto& previous=states[r-1][p];if(previous.lost>=1e29)continue;
            obstacle_map::Interval overlap;
            const bool local_recorded=current.recorded||previous.recorded;
            if(local_recorded) {
                if(previous.span.lo>0||previous.span.hi<0||current.from.lo>0||current.from.hi<0)continue;
                overlap={0,0};
            } else if(!BestIntersection(previous.span,current.from,free[r-1],overlap))continue;
            const double entry=std::clamp(previous.at,static_cast<double>(overlap.lo),static_cast<double>(overlap.hi));
            const double at=std::clamp(entry,static_cast<double>(current.span.lo),static_cast<double>(current.span.hi));
            const double lost=previous.lost+(current.recorded?length:0);
            const double shape=previous.shape+length*(entry*entry+entry*at+at*at)/3+
                .25*((entry-previous.at)*(entry-previous.at)+(at-entry)*(at-entry));
            if(lost<current.lost-1e-6 || (std::abs(lost-current.lost)<=1e-6&&shape<current.shape)) {
                current.lost=lost;current.shape=shape;current.at=at;current.previous=static_cast<int>(p);
            }
        }
    }
    std::vector<int> chosen(m,0);int last=-1;double lost=1e30,shape=1e30;
    for(std::size_t p=0;p<states.back().size();++p) {
        const auto& s=states.back()[p];
        if(s.span.lo>0||s.span.hi<0||(!s.recorded&&!obstacle_map::Contains(free.back(),0)))continue;
        if(s.lost<lost-1e-6||(std::abs(s.lost-lost)<=1e-6&&s.shape<shape)){last=static_cast<int>(p);lost=s.lost;shape=s.shape;}
    }
    if(last<0){std::fill(recorded.begin(),recorded.end(),static_cast<unsigned char>(1));return;}
    for(std::size_t r=m;r-->0;){chosen[r]=last;last=states[r][last].previous;if(r&&last<0){std::fill(recorded.begin(),recorded.end(),static_cast<unsigned char>(1));return;}}
    auto retain=[&](float from,float to) {
        const auto first=centre.Segment(from),last_segment=centre.Segment(to);
        for(std::size_t i=first;i<=last_segment&&i<recorded.size();++i)recorded[i]=1;
    };
    rows.assign(m,{});
    for(std::size_t r=0;r<m;++r) {
        auto& bound=rows[r];bound.along=map.Along(r);bound.section=frames[r];bound.row=r;
        const auto& incoming=states[r][chosen[r]];
        const auto& outgoing=r+1<m?states[r+1][chosen[r+1]]:incoming;
        obstacle_map::Interval span;
        bound.incoming_lo=incoming.physical_span.lo;bound.incoming_hi=incoming.physical_span.hi;bound.incoming_recorded=incoming.recorded;
        bound.incoming_from_lo=incoming.physical_from.lo;bound.incoming_from_hi=incoming.physical_from.hi;
        const auto departing=r+1<m?outgoing.physical_from:incoming.physical_span;
        bound.recorded=incoming.recorded||outgoing.recorded||!BestIntersection(incoming.physical_span,departing,physical_free[r],span);
        if(bound.recorded){bound.lo=bound.hi=0;retain(r?map.Along(r-1):0,r+1<m?map.Along(r+1):centre.Length());}
        else {bound.lo=span.lo;bound.hi=span.hi;bound.seed=std::clamp(incoming.at,bound.lo,bound.hi);}
    }
    sections.reserve(centre.points.size()+m+1);
    std::size_t native=1;
    for(std::size_t r=0;r<m;++r) {
        if(r) {
            const auto& link=states[r][chosen[r]];const auto& a=rows[r-1];const auto& b=rows[r];
            if(link.recorded)retain(a.along,b.along);
            while(native<centre.metres.size()&&centre.metres[native]<=a.along)++native;
            for(;native<centre.metres.size()&&centre.metres[native]<b.along;++native) {
                const float t=(centre.metres[native]-a.along)/(b.along-a.along);
                SectionBound mid;mid.along=a.along+(b.along-a.along)*t;mid.row=r;
                const float nx=a.section.nx+(b.section.nx-a.section.nx)*t,ny=a.section.ny+(b.section.ny-a.section.ny)*t;
                const float norm=std::hypot(nx,ny);
                mid.section.centre={a.section.centre.x+(b.section.centre.x-a.section.centre.x)*t,
                    a.section.centre.y+(b.section.centre.y-a.section.centre.y)*t,
                    a.section.centre.z+(b.section.centre.z-a.section.centre.z)*t};
                if(norm>.01F){mid.section.nx=nx/norm;mid.section.ny=ny/norm;}
                mid.lo=(link.physical_from.lo+(link.physical_span.lo-link.physical_from.lo)*t)*norm;
                mid.hi=(link.physical_from.hi+(link.physical_span.hi-link.physical_from.hi)*t)*norm;
                mid.recorded=link.recorded||norm<=.01F;sections.push_back(mid);
            }
        }
        sections.push_back(rows[r]);
    }
    if(rows.back().along<centre.Length()) {
        auto tail=rows.back();tail.along=centre.Length();tail.section=obstacle_map::Map::SectionAt(centre,tail.along);
        // A last cross section is not a swept measurement of the remaining
        // sub-3m tail. Preserve its original chord and report it, rather than
        // extending a physical clearance certificate past the sampled map.
        recorded.back()=1;tail.lo=tail.hi=0;tail.recorded=tail.incoming_recorded=true;
        tail.incoming_lo=tail.incoming_hi=0;
        tail.incoming_from_lo=tail.incoming_from_hi=0;
        sections.push_back(tail);rows.push_back(tail);
    }
    for(auto& section:sections){section.source=centre.Segment(section.along);section.source_cached=true;}
}
inline strip_geometry::Patch Strip(const SectionBound& from,const SectionBound& to) {
    return {from.section,to.section,
        {static_cast<float>(std::isfinite(to.incoming_from_lo)?to.incoming_from_lo:to.incoming_lo),
         static_cast<float>(std::isfinite(to.incoming_from_hi)?to.incoming_from_hi:to.incoming_hi)},
        {static_cast<float>(to.incoming_lo),static_cast<float>(to.incoming_hi)}};
}
inline bool InStrip(Vector3 point,const SectionBound& from,const SectionBound& to) {
    return !to.incoming_recorded&&strip_geometry::Inside(point,Strip(from,to),.002);
}
struct Crossing {std::size_t segment=0;double lateral=0,a=0,b=0;bool valid=false;};
// Intersect the displaced polyline with the section's actual normal plane.
// Differentiating the intersection includes its motion along the chord;
// using only a fixed interpolation t would constrain the wrong physical cut.
inline Crossing Cross(const course::Path& centre,const std::vector<Vec2>& normals,
    const std::vector<double>& d,const SectionBound& bound,float window,bool derivatives=true) {
    Crossing best;double nearest=1e30;
    const auto source=bound.source_cached?bound.source:centre.Segment(bound.along);
    const auto first=source>2?source-2:0,last=std::min(d.size()-2,source+2);
    const double nx=bound.section.nx,ny=bound.section.ny,tx=ny,ty=-nx;
    auto visit=[&](std::size_t i) {
        const double ax=centre.points[i].x+d[i]*normals[i].x,ay=centre.points[i].y+d[i]*normals[i].y;
        const double vx=centre.points[i+1].x+d[i+1]*normals[i+1].x-ax,vy=centre.points[i+1].y+d[i+1]*normals[i+1].y-ay;
        const double denominator=tx*vx+ty*vy;if(std::abs(denominator)<1e-8)return;
        const double t=-(tx*(ax-bound.section.centre.x)+ty*(ay-bound.section.centre.y))/denominator;
        if(t< -1e-5||t>1+1e-5)return;
        const double reference=centre.metres[i]+std::clamp(t,0.0,1.0)*(centre.metres[i+1]-centre.metres[i]);
        const double distance=std::abs(reference-bound.along);if(distance>=nearest)return;
        nearest=distance;
        best={i,nx*(ax+t*vx-bound.section.centre.x)+ny*(ay+t*vy-bound.section.centre.y),0,0,true};
        if(derivatives) {
            const double tangent=(nx*vx+ny*vy)/denominator;
            best.a=(1-t)*(nx*normals[i].x+ny*normals[i].y-tangent*(tx*normals[i].x+ty*normals[i].y));
            best.b=t*(nx*normals[i+1].x+ny*normals[i+1].y-tangent*(tx*normals[i+1].x+ty*normals[i+1].y));
        }
    };
    for(std::size_t i=first;i<=last&&i+1<d.size();++i)visit(i);
    // The node order/skew bounds normally keep a cut within these five
    // chords. A wider search is only needed for an unusually tight fold.
    if(!best.valid&&window>0) {
        const auto begin=centre.Segment(bound.along-window),end=centre.Segment(bound.along+window);
        for(std::size_t i=begin;i<=end&&i+1<d.size();++i) {
            if(i>=first&&i<=last)continue;
            visit(i);
        }
    }
    return best;
}
} // namespace horse_races::racing_line::detail
