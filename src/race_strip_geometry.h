#pragma once
#include "race_obstacle_map.h"
#include <array>

namespace horse_races::strip_geometry {
struct Patch {
    obstacle_map::Section from,to;
    obstacle_map::Interval entry,exit;
};
struct Range {double lo=0,hi=0;};
struct Coverage {std::array<Range,16> ranges{};std::size_t count=0;};
struct PreparedPatch {
    Patch patch;
    bool regular=false;
    double min_x=INFINITY,min_y=INFINITY,max_x=-INFINITY,max_y=-INFINITY;
};
namespace detail {
struct V {double x=0,y=0;};
inline V operator+(V a,V b){return {a.x+b.x,a.y+b.y};}
inline V operator-(V a,V b){return {a.x-b.x,a.y-b.y};}
inline V operator*(V a,double t){return {a.x*t,a.y*t};}
inline double Dot(V a,V b){return a.x*b.x+a.y*b.y;}
inline double Cross(V a,V b){return a.x*b.y-a.y*b.x;}
inline V XY(Vector3 p){return {p.x,p.y};}
struct Roots {std::array<double,2> value{};int count=0;};
inline Roots Solve(double a,double b,double c) {
    Roots out;const double eps=1e-13*std::max({1.0,std::abs(a),std::abs(b),std::abs(c)});
    if(std::abs(a)<=eps){if(std::abs(b)>eps)out.value[out.count++]=-c/b;return out;}
    double d=b*b-4*a*c;const double tolerance=1e-13*std::max(1.0,b*b+std::abs(4*a*c));
    if(d< -tolerance)return out;d=std::max(0.0,d);
    const double q=-.5*(b+std::copysign(std::sqrt(d),b));
    if(std::abs(q)<=eps)out.value[out.count++]=-b/(2*a);
    else {out.value[out.count++]=q/a;out.value[out.count++]=c/q;}
    return out;
}
inline bool Regular(const Patch& p) {
    const V d=XY(p.to.centre)-XY(p.from.centre),n{p.from.nx,p.from.ny},e{double(p.to.nx)-p.from.nx,double(p.to.ny)-p.from.ny};
    // Jacobian is affine over the (t,l) trapezoid. Its four vertices suffice;
    // an inverted/folded patch supplies no clearance certificate.
    for(auto edge:{std::pair<double,obstacle_map::Interval>{0,p.entry},{1,p.exit}})
        for(double l:{edge.second.lo,edge.second.hi}) {
            const double j=Cross(d+e*l,n+e*edge.first);
            if(!std::isfinite(j)||j<=1e-10)return false;
        }
    return p.entry.lo<=p.entry.hi&&p.exit.lo<=p.exit.hi;
}
inline bool Inside(V point,const Patch& p,double tolerance) {
    const V d=XY(p.to.centre)-XY(p.from.centre),n{p.from.nx,p.from.ny},e{double(p.to.nx)-p.from.nx,double(p.to.ny)-p.from.ny};
    const V x=point-XY(p.from.centre);
    const auto roots=Solve(-Cross(d,e),Cross(x,e)-Cross(d,n),Cross(x,n));
    for(int i=0;i<roots.count;++i) {
        const double t=roots.value[i];if(t< -tolerance||t>1+tolerance)continue;
        const V normal=n+e*t;const double norm=Dot(normal,normal);if(!(norm>1e-12))continue;
        const double l=Dot(x-d*t,normal)/norm;
        const double lo=p.entry.lo+t*(p.exit.lo-p.entry.lo),hi=p.entry.hi+t*(p.exit.hi-p.entry.hi);
        if(l>=lo-tolerance&&l<=hi+tolerance)return true;
    }
    return false;
}
}
inline float MinimumNormalLength(const obstacle_map::Section& from,const obstacle_map::Section& to) {
    const detail::V n{from.nx,from.ny},e{double(to.nx)-from.nx,double(to.ny)-from.ny};
    const double norm=detail::Dot(e,e),t=norm>1e-20?std::clamp(-detail::Dot(n,e)/norm,0.0,1.0):0;
    return static_cast<float>(std::sqrt(detail::Dot(n+e*t,n+e*t)));
}
inline bool Inside(Vector3 point,const Patch& patch,double tolerance=1e-8) {
    return detail::Regular(patch)&&detail::Inside(detail::XY(point),patch,tolerance);
}
// Retain the positive-Jacobian sub-trapezoid over the full longitudinal
// interval. J(t,l)=a+b*t+c*l is affine, so clipping each endpoint's lateral
// interval preserves every interior point of the resulting linear rails.
inline bool ClipRegular(Patch& patch) {
    if(detail::Regular(patch))return true;
    using detail::V;
    const V d=detail::XY(patch.to.centre)-detail::XY(patch.from.centre),n{patch.from.nx,patch.from.ny},
        e{double(patch.to.nx)-patch.from.nx,double(patch.to.ny)-patch.from.ny};
    const double a=detail::Cross(d,n),b=detail::Cross(d,e),c=detail::Cross(e,n);
    const double lateral=std::max({std::abs(double(patch.entry.lo)),std::abs(double(patch.entry.hi)),
        std::abs(double(patch.exit.lo)),std::abs(double(patch.exit.hi))});
    const double epsilon=1e-8*std::max(1.0,std::abs(a)+std::abs(b)+std::abs(c)*lateral);
    if(!std::isfinite(epsilon))return false;
    auto clipped=patch;
    auto cut=[&](obstacle_map::Interval& span,double base) {
        if(!std::isfinite(base)||!std::isfinite(c))return false;
        if(c==0)return base>epsilon&&span.lo<=span.hi;
        const double edge=(epsilon-base)/c;
        if(c>0&&span.lo<=edge)span.lo=std::nextafter(static_cast<float>(edge),INFINITY);
        if(c<0&&span.hi>=edge)span.hi=std::nextafter(static_cast<float>(edge),-INFINITY);
        return span.lo<span.hi;
    };
    if(!cut(clipped.entry,a)||!cut(clipped.exit,a+b)||!detail::Regular(clipped))return false;
    patch=clipped;return true;
}
inline PreparedPatch Prepare(const Patch& source,double side_tolerance_metres=0) {
    PreparedPatch out;out.patch=source;
    if(!std::isfinite(side_tolerance_metres)||side_tolerance_metres<0)return out;
    if(side_tolerance_metres>0) {
        // The maximum interpolated normal length occurs at an endpoint.
        // This gives at most the requested physical transverse tolerance
        // everywhere, without relaxing either longitudinal end plane.
        const double norm=std::max(std::hypot(source.from.nx,source.from.ny),std::hypot(source.to.nx,source.to.ny));
        if(!(norm>1e-10))return out;
        const double side=side_tolerance_metres/norm;
        auto low=[&](float value){return std::nextafter(static_cast<float>(value-side),INFINITY);};
        auto high=[&](float value){return std::nextafter(static_cast<float>(value+side),-INFINITY);};
        out.patch.entry={low(source.entry.lo),high(source.entry.hi)};
        out.patch.exit={low(source.exit.lo),high(source.exit.hi)};
    }
    out.regular=ClipRegular(out.patch);if(!out.regular)return out;
    using detail::V;const auto& p=out.patch;
    const V c=detail::XY(p.from.centre),d=detail::XY(p.to.centre)-c,n{p.from.nx,p.from.ny},e{double(p.to.nx)-p.from.nx,double(p.to.ny)-p.from.ny};
    auto include=[&](V point){out.min_x=std::min(out.min_x,point.x);out.min_y=std::min(out.min_y,point.y);
        out.max_x=std::max(out.max_x,point.x);out.max_y=std::max(out.max_y,point.y);};
    for(auto edge:{std::pair<double,double>{p.entry.lo,p.exit.lo},{p.entry.hi,p.exit.hi}}) {
        const double delta=edge.second-edge.first;
        const V a=c+n*edge.first,b=d+e*edge.first+n*delta,q=e*delta;
        // A quadratic Bezier lies in its three-control-point convex hull.
        include(a);include(a+b*.5);include(a+b+q);
    }
    return out;
}
inline bool Inside(Vector3 point,const PreparedPatch& patch,double tolerance=1e-8) {
    return patch.regular&&detail::Inside(detail::XY(point),patch.patch,tolerance);
}
// Exact boundary partition of an output chord for one regular ruled patch.
// The caller unions these intervals across adjacent patches. No sampling
// pitch or per-frame work is involved; all storage here has a fixed bound.
inline Coverage CoveredIntervals(Vector3 start,Vector3 finish,const PreparedPatch& prepared) {
    Coverage out;if(!prepared.regular)return out;
    if(std::max(start.x,finish.x)<prepared.min_x-1e-8||std::min(start.x,finish.x)>prepared.max_x+1e-8||
       std::max(start.y,finish.y)<prepared.min_y-1e-8||std::min(start.y,finish.y)>prepared.max_y+1e-8)return out;
    const auto& p=prepared.patch;
    using detail::V;const V origin=detail::XY(start),v=detail::XY(finish)-origin;
    const double length2=detail::Dot(v,v);
    if(length2<1e-20){if(detail::Inside(origin,p,1e-8))out.ranges[out.count++]={0,1};return out;}
    std::array<double,16> cuts{};std::size_t count=2;cuts[0]=0;cuts[1]=1;
    auto add=[&](double u){if(std::isfinite(u)&&u>=-1e-10&&u<=1+1e-10&&count<cuts.size())cuts[count++]=std::clamp(u,0.0,1.0);};
    auto curve=[&](V a,V b,V q) {
        const double ca=detail::Cross(q,v),cb=detail::Cross(b,v),cc=detail::Cross(a-origin,v);
        const auto roots=detail::Solve(ca,cb,cc);
        for(int i=0;i<roots.count;++i)if(roots.value[i]>=-1e-10&&roots.value[i]<=1+1e-10) {
            const double t=std::clamp(roots.value[i],0.0,1.0);
            add(detail::Dot(a+b*t+q*(t*t)-origin,v)/length2);
        }
        if(std::abs(ca)+std::abs(cb)+std::abs(cc)<1e-10) {
            // Collinear boundary: include its projected extrema as well.
            add(detail::Dot(a-origin,v)/length2);add(detail::Dot(a+b+q-origin,v)/length2);
            const double qa=detail::Dot(q,v),qb=detail::Dot(b,v);
            if(std::abs(qa)>1e-20) {const double t=-qb/(2*qa);if(t>0&&t<1)add(detail::Dot(a+b*t+q*(t*t)-origin,v)/length2);}
        }
    };
    const V c=detail::XY(p.from.centre),d=detail::XY(p.to.centre)-c,n{p.from.nx,p.from.ny},e{double(p.to.nx)-p.from.nx,double(p.to.ny)-p.from.ny};
    for(auto edge:{std::pair<double,double>{p.entry.lo,p.exit.lo},{p.entry.hi,p.exit.hi}}) {
        const double delta=edge.second-edge.first;
        curve(c+n*edge.first,d+e*edge.first+n*delta,e*delta);
    }
    curve(c+n*p.entry.lo,n*(p.entry.hi-p.entry.lo),{});
    curve(c+d+(n+e)*p.exit.lo,(n+e)*(p.exit.hi-p.exit.lo),{});
    std::sort(cuts.begin(),cuts.begin()+static_cast<std::ptrdiff_t>(count));
    for(std::size_t i=1;i<count;++i) {
        if(cuts[i]-cuts[i-1]<=1e-12)continue;
        const double mid=(cuts[i]+cuts[i-1])*.5;
        if(detail::Inside(origin+v*mid,p,1e-10)) {
            if(out.count&&std::abs(out.ranges[out.count-1].hi-cuts[i-1])<=1e-12)out.ranges[out.count-1].hi=cuts[i];
            else out.ranges[out.count++]={cuts[i-1],cuts[i]};
        }
    }
    return out;
}
inline Coverage CoveredIntervals(Vector3 start,Vector3 finish,const Patch& patch) {
    return CoveredIntervals(start,finish,Prepare(patch));
}
}
