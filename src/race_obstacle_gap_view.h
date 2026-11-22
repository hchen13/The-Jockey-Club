#pragma once
#include "race_obstacle_map.h"
#include "race_strip_geometry.h"

namespace horse_races::obstacle_gap_view {
inline constexpr float kMaxBracketMetres=12;
struct Strip {obstacle_map::Interval from,to;};
struct View {
    obstacle_map::Map map;
    std::vector<unsigned char> estimated_rows,estimated_links;
    std::vector<std::vector<Strip>> links;
    std::vector<float> link_min_normal;
    std::size_t row_count=0,link_count=0;
    bool Estimated() const{return row_count||link_count;}
};
// Equal ordered islands remain separated throughout linear interpolation.
inline std::vector<Strip> Pair(const std::vector<obstacle_map::Interval>& from,
                               const std::vector<obstacle_map::Interval>& to) {
    if(from.empty()||from.size()!=to.size())return {};
    std::vector<Strip> out;out.reserve(from.size());
    for(std::size_t i=0;i<from.size();++i) {
        if(from[i].hi<=from[i].lo||to[i].hi<=to[i].lo)return {};
        if(i&&(from[i-1].hi>=from[i].lo||to[i-1].hi>=to[i].lo))return {};
        out.push_back({from[i],to[i]});
    }
    return out;
}
inline std::vector<Strip> MeasuredStrips(const obstacle_map::Map& map,std::size_t row,float clearance=0) {
    std::vector<Strip> out;
    for(auto p:map.ConnectionStrips(row,clearance))out.push_back({p.from,p.to});
    return out;
}
// Estimated strips have no certified body radius; apply full clearance.
inline std::vector<Strip> EffectiveStrips(const obstacle_map::Map& raw,const View* view,
                                         std::size_t row,float clearance=0) {
    if(!view||row>=view->estimated_links.size()||!view->estimated_links[row])return MeasuredStrips(raw,row,clearance);
    if(!std::isfinite(clearance)||clearance<0)return {};
    const float norm=row<view->link_min_normal.size()?view->link_min_normal[row]:1;
    if(!(norm>1e-5F))return {};
    const float inset=clearance/norm;
    std::vector<Strip> out;
    for(auto strip:view->links[row]) {
        strip.from.lo+=inset;strip.from.hi-=inset;
        strip.to.lo+=inset;strip.to.hi-=inset;
        if(strip.from.lo<=strip.from.hi&&strip.to.lo<=strip.to.hi)out.push_back(strip);
    }
    return out;
}
// Preparation-only: raw observations bracket each short gap. Estimated
// sections and tapered links never enter persistence, learning or scheduling.
inline View Build(const obstacle_map::Map& raw,const course::Path& path,
                  const std::vector<float>& recorded_starts) {
    View out;out.map=raw;const auto n=raw.rows.size();
    out.estimated_rows.assign(n,0);out.estimated_links.assign(n,0);out.links.resize(n);out.link_min_normal.assign(n,1);
    // A missing native certificate is unknown. Nearby native geometry does
    // not authorize inventing a bridge or a cross section between samples.
    if(raw.native_mode||raw.native_loaded)return out;
    if(!n||path.points.size()<2||!std::is_sorted(recorded_starts.begin(),recorded_starts.end()))return out;
    std::vector<std::size_t> part(n),left(n),right(n);
    for(std::size_t r=0;r<n;++r) {
        part[r]=static_cast<std::size_t>(std::upper_bound(recorded_starts.begin(),recorded_starts.end(),raw.Along(r))-recorded_starts.begin());
        left[r]=right[r]=r;
    }
    auto impact=[&](std::size_t r){
        return !raw.rows[r].impacts.empty()||!raw.rows[r].connection_impacts.empty()||!raw.rows[r].authored_sections.empty()||!raw.rows[r].authored_links.empty();
    };
    auto geometry=[&](std::size_t a,std::size_t b) {
        if(part[a]!=part[b]||raw.Along(b)-raw.Along(a)>kMaxBracketMetres+.0001F)return false;
        for(std::size_t r=a+1;r<=b;++r) {
            const auto x=raw.SectionFor(path,r-1),y=raw.SectionFor(path,r);
            const float normx=std::hypot(x.nx,x.ny),normy=std::hypot(y.nx,y.ny);
            const float nx=x.nx+y.nx,ny=x.ny+y.ny;
            if(normx<.5F||normy<.5F||std::hypot(nx,ny)<.001F||
               (y.centre.x-x.centre.x)*ny-(y.centre.y-x.centre.y)*nx<=.00001F)return false;
        }
        return true;
    };
    auto wholly_unknown=[&](std::size_t r) {
        const auto& row=raw.rows[r];
        return !row.Surveyed()&&!row.HasDetailed()&&!row.HasConnections()&&
            row.reason==obstacle_map::kNone&&row.intervals.empty()&&row.connections.empty()&&row.unknown.empty();
    };
    for(std::size_t a=0;a<n;) {
        if(!wholly_unknown(a)){++a;continue;}
        std::size_t end=a+1;while(end<n&&part[end]==part[a]&&wholly_unknown(end))++end;
        if(a&&end<n&&part[a-1]==part[a]&&part[end]==part[a]&&
           raw.rows[a-1].Surveyed()&&raw.rows[end].Surveyed()&&geometry(a-1,end)) {
            const auto paired=Pair(raw.rows[a-1].ClearIntervals(),raw.rows[end].ClearIntervals());
            if(!paired.empty())for(std::size_t r=a;r<end;++r) {
                const float t=(raw.Along(r)-raw.Along(a-1))/(raw.Along(end)-raw.Along(a-1));
                auto& row=out.map.rows[r];row.detailed=true;row.legacy=false;
                for(auto strip:paired)row.intervals.push_back({strip.from.lo+t*(strip.to.lo-strip.from.lo),
                    strip.from.hi+t*(strip.to.hi-strip.from.hi)});
                row.RefreshWidths();out.estimated_rows[r]=1;++out.row_count;
                left[r]=a-1;right[r]=end;
            }
        }
        a=end;
    }
    // A long run of measured sections is not one giant missing-section gap.
    for(std::size_t r=1;r<n;++r) {
        if(part[r-1]!=part[r]||raw.rows[r].HasConnections()||impact(r-1)||impact(r))continue;
        if(!out.map.rows[r-1].Surveyed()||!out.map.rows[r].Surveyed())continue;
        const auto a=std::min(left[r-1],left[r]),b=std::max(right[r-1],right[r]);
        if(!geometry(a,b))continue;
        auto paired=Pair(out.map.rows[r-1].ClearIntervals(),out.map.rows[r].ClearIntervals());
        if(paired.empty())continue;
        const float norm=strip_geometry::MinimumNormalLength(raw.SectionFor(path,r-1),raw.SectionFor(path,r));
        if(!(norm>1e-5F))continue;
        out.link_min_normal[r]=norm<1?std::nextafter(norm,0.0F):norm;
        out.links[r]=std::move(paired);out.estimated_links[r]=1;++out.link_count;
    }
    return out;
}
}
