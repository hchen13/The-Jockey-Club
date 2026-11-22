#pragma once
#include "race_strip_geometry.h"
#include <optional>

namespace horse_races::obstacle_map {
struct LinkPoint {double x=0,y=0;};
// Preserve the complete negative ruled image, including folds. Its six
// Bezier control points provide the same conservative hull for every caller.
template<class Point=LinkPoint>
inline std::vector<Point> ConnectionNegativeHull(const strip_geometry::Patch& patch) {
    std::vector<Point> points;
    const Point c{patch.from.centre.x,patch.from.centre.y},d{double(patch.to.centre.x)-c.x,double(patch.to.centre.y)-c.y};
    const Point n{patch.from.nx,patch.from.ny},e{double(patch.to.nx)-n.x,double(patch.to.ny)-n.y};
    for(auto edge:{std::pair<double,double>{patch.entry.lo,patch.exit.lo},{patch.entry.hi,patch.exit.hi}}) {
        const double delta=edge.second-edge.first;
        const Point a{c.x+n.x*edge.first,c.y+n.y*edge.first};
        const Point b{d.x+e.x*edge.first+n.x*delta,d.y+e.y*edge.first+n.y*delta},q{e.x*delta,e.y*delta};
        points.push_back(a);points.push_back({a.x+b.x*.5,a.y+b.y*.5});points.push_back({a.x+b.x+q.x,a.y+b.y+q.y});
    }
    std::sort(points.begin(),points.end(),[](auto a,auto b){return a.x<b.x||(a.x==b.x&&a.y<b.y);});
    const auto cross=[](Point a,Point b,Point c){return (b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);};
    std::vector<Point> hull;
    for(auto p:points){while(hull.size()>1&&cross(hull[hull.size()-2],hull.back(),p)<=0)hull.pop_back();hull.push_back(p);}
    const auto lower=hull.size();
    for(std::size_t i=points.size()-1;i-->0;){const auto p=points[i];while(hull.size()>lower&&cross(hull[hull.size()-2],hull.back(),p)<=0)hull.pop_back();hull.push_back(p);}
    if(hull.size()>1)hull.pop_back();return hull;
}
template<class Point>
inline std::optional<strip_geometry::Range> ConnectionHullRange(Vector3 a,Vector3 b,const std::vector<Point>& hull) {
    if(hull.empty())return {};
    double lo=0,hi=1;
    if(hull.size()<3) {
        for(int axis=0;axis<2;++axis) {
            const double start=axis?a.y:a.x,delta=(axis?b.y:b.x)-start;
            const double p=axis?hull.front().y:hull.front().x,q=axis?hull.back().y:hull.back().x;
            const double lower=std::min(p,q),upper=std::max(p,q);
            if(std::abs(delta)<1e-12){if(start<lower||start>upper)return {};continue;}
            double x=(lower-start)/delta,y=(upper-start)/delta;if(x>y)std::swap(x,y);
            lo=std::max(lo,x);hi=std::min(hi,y);if(lo>hi)return {};
        }
    } else for(std::size_t i=0;i<hull.size();++i) {
        const auto p=hull[i],q=hull[(i+1)%hull.size()];const double dx=q.x-p.x,dy=q.y-p.y;
        const double value=dx*(a.y-p.y)-dy*(a.x-p.x),slope=dx*(b.y-a.y)-dy*(b.x-a.x);
        if(std::abs(slope)<1e-12){if(value<0)return {};continue;}
        const double t=-value/slope;if(slope>0)lo=std::max(lo,t);else hi=std::min(hi,t);
        if(lo>hi)return {};
    }
    return strip_geometry::Range{lo,hi};
}
// Shared effective link negative, prepared outside the driving frame. A
// surviving actual centre-axis strip may supersede older impact inflation;
// native/probe clearance and legacy physical footprints cannot do so.
class ConnectionBlocker {
    std::vector<std::vector<LinkPoint>> impacts,authored;
    std::vector<strip_geometry::PreparedPatch> axes;
    mutable std::vector<strip_geometry::Range> coverage;
    bool invalid=false;
public:
    ConnectionBlocker(const Row& row,const Section& from,const Section& to,float clearance) {
        if(row.connection_impacts.empty()&&row.authored_links.empty())return;
        const float norm=strip_geometry::MinimumNormalLength(from,to);
        if(!std::isfinite(clearance)||clearance<0||!std::isfinite(norm)){invalid=true;return;}
        for(auto span:row.connection_impacts) {
            if(!std::isfinite(span.lo)||!std::isfinite(span.hi)||span.lo>span.hi){invalid=true;continue;}
            const float grow=clearance/std::max(.01F,norm);span.lo-=grow;span.hi+=grow;
            impacts.push_back(ConnectionNegativeHull({from,to,span,span}));
        }
        for(auto span:row.authored_links) {
            if(!std::isfinite(span.lo)||!std::isfinite(span.hi)||span.lo>span.hi){invalid=true;continue;}
            const float grow=clearance/std::max(.01F,norm);span.lo-=grow;span.hi+=grow;
            authored.push_back(ConnectionNegativeHull({from,to,span,span}));
        }
        if(row.passage_axes)for(const auto& p:row.passages) {
            const strip_geometry::Patch patch{from,to,p.from,p.to};
            // A folded/degenerate original strip supplies no positive proof.
            if(strip_geometry::detail::Regular(patch))axes.push_back(strip_geometry::Prepare(patch));
        }
        coverage.reserve(axes.size()*16); // At most 64 persisted strips, 16 cuts each.
    }
    bool Blocks(Vector3 a,Vector3 b) const {
        if(invalid||!destination::Finite(a)||!destination::Finite(b))return true;
        for(const auto& hull:authored)if(ConnectionHullRange(a,b,hull))return true;
        coverage.clear();
        for(const auto& axis:axes) {
            const auto covered=strip_geometry::CoveredIntervals(a,b,axis);
            for(std::size_t i=0;i<covered.count;++i)coverage.push_back(covered.ranges[i]);
        }
        std::sort(coverage.begin(),coverage.end(),[](auto x,auto y){return x.lo<y.lo||(x.lo==y.lo&&x.hi<y.hi);});
        for(const auto& hull:impacts)if(const auto hit=ConnectionHullRange(a,b,hull)) {
            double end=hit->lo;bool reached=false;
            for(auto safe:coverage) {
                if(safe.hi<end-1e-8)continue;
                if(safe.lo>end+1e-8)break;
                end=std::max(end,safe.hi);reached=true;
                if(end>=hit->hi-1e-8)break;
            }
            // One overlapping positive patch is insufficient: every point
            // in this negative hit range must belong to the actual axis union.
            if(!reached||end<hit->hi-1e-8)return true;
        }
        return false;
    }
};
} // namespace horse_races::obstacle_map
