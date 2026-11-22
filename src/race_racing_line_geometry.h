#pragma once
#include "race_racing_line_types.h"

namespace horse_races::racing_line::detail {
struct Vec2 {double x=0,y=0;};
struct ScopedMs {
    double& total;const double* nested;
    double nested_before;
    std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();
    explicit ScopedMs(double& value,const double* exclude=nullptr):total(value),nested(exclude),nested_before(exclude?*exclude:0){}
    ~ScopedMs(){total+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()-(nested?*nested-nested_before:0);}
};
// Symmetric banded matrix, upper storage: at(i,k) is H[i][i+k], k=0..bandwidth.
struct Band {
    std::size_t n=0,bandwidth=0;
    std::vector<double> upper;
    void Reset(std::size_t size,std::size_t bw){n=size;bandwidth=bw;upper.assign(n*(bw+1),0.0);}
    double& at(std::size_t i,std::size_t k){return upper[k*n+i];}
    double at(std::size_t i,std::size_t k) const {return upper[k*n+i];}
};
// LDL^T factorisation with half-bandwidth `bandwidth`, O(n*bandwidth^2), then
// two banded substitutions. `lower` and `diag` are scratch; rhs becomes x.
inline bool Factor(const Band& a,std::vector<double>& lower,std::vector<double>& diag) {
    const std::size_t n=a.n,bw=a.bandwidth;
    if(n<3)return false;
    lower.assign(n*(bw+1),0.0);diag.assign(n,0.0);
    auto L=[&](std::size_t i,std::size_t k)->double&{return lower[k*n+i];}; // L[i][i-k]
    for(std::size_t i=0;i<n;++i) {
        const std::size_t first=i>bw?i-bw:0;
        for(std::size_t j=first;j<i;++j) {
            double v=a.at(j,i-j);
            for(std::size_t p=first;p<j;++p)v-=L(i,i-p)*diag[p]*L(j,j-p);
            L(i,i-j)=v/diag[j];
        }
        double d=a.at(i,0);
        for(std::size_t p=first;p<i;++p)d-=L(i,i-p)*L(i,i-p)*diag[p];
        if(!(d>1e-18) || !std::isfinite(d))return false;
        diag[i]=d;
    }
    return true;
}
inline bool Substitute(std::size_t n,std::size_t bw,const std::vector<double>& lower,
    const std::vector<double>& diag,std::vector<double>& rhs) {
    if(rhs.size()!=n || diag.size()!=n || lower.size()!=n*(bw+1))return false;
    auto L=[&](std::size_t i,std::size_t k){return lower[k*n+i];};
    for(std::size_t i=0;i<n;++i){double v=rhs[i];const std::size_t first=i>bw?i-bw:0;for(std::size_t p=first;p<i;++p)v-=L(i,i-p)*rhs[p];rhs[i]=v;}
    for(std::size_t i=0;i<n;++i)rhs[i]/=diag[i];
    for(std::size_t i=n;i-->0;){double v=rhs[i];for(std::size_t k=1;k<=bw && i+k<n;++k)v-=L(i+k,k)*rhs[i+k];rhs[i]=v;}
    return std::all_of(rhs.begin(),rhs.end(),[](double v){return std::isfinite(v);});
}
inline bool Solve(const Band& a,std::vector<double>& lower,std::vector<double>& diag,std::vector<double>& rhs) {
    return Factor(a,lower,diag) && Substitute(a.n,a.bandwidth,lower,diag,rhs);
}
// Nearest point on the centreline within +-window metres of `along`, walking
// only the segments in that window. The corridor judge sweeps the WHOLE line
// (circuit::Layout::StrictProjection over 0..Length), whose nearest distance
// can only be smaller, so a line that passes this local test is legal there
// too; the local window is what keeps the order check honest on a course that
// overlaps itself (recorded laps share one ring).
inline course::Projection ProjectNear(const course::Path& path,Vector3 p,float along,float window) {
    course::Projection best;
    if(path.points.size()<2)return best;
    const std::size_t first=path.Segment(along-window),last=path.Segment(along+window);
    for(std::size_t i=first;i<=last && i+1<path.points.size();++i) {
        const auto& a=path.points[i];const auto& b=path.points[i+1];
        const float dx=b.x-a.x,dy=b.y-a.y,length2=dx*dx+dy*dy;
        if(!(length2>0))continue;
        const float t=std::clamp(((p.x-a.x)*dx+(p.y-a.y)*dy)/length2,0.0F,1.0F);
        const float distance=std::hypot(p.x-a.x-t*dx,p.y-a.y-t*dy);
        if(distance<best.distance)best={path.metres[i]+t*(path.metres[i+1]-path.metres[i]),distance,i,true};
    }
    return best;
}
// The same +-8 m chord estimator race_pace::Plan uses, with its sign: + when
// the line turns left (its centre of curvature lies on the left normal).
inline float SignedChordCurvature(const course::Path& path,std::size_t i) {
    if(i==0 || i+1>=path.points.size())return 0;
    const auto a=race_pace::At(path,path.metres[i]-kChordMetres),b=path.points[i],c=race_pace::At(path,path.metres[i]+kChordMetres);
    const float ab=destination::Distance(a,b),bc=destination::Distance(b,c),ac=destination::Distance(a,c);
    const float cross=(b.x-a.x)*(c.y-b.y)-(b.y-a.y)*(c.x-b.x);
    return 2*cross/std::max(.01F,ab*bc*ac);
}
// The frame per node: the left normal from a +-4 m chord, so a 0.35 m
// recording hiccup does not swing it. Shared with race_line_pins.h, whose
// recorded offsets must be laid on the very same normals.
inline void Frame(const course::Path& centreline,std::vector<Vec2>& normal) {
    const std::size_t n=centreline.points.size();
    normal.assign(n,{});
    for(std::size_t i=0;i<n;++i) {
        const auto a=race_pace::At(centreline,centreline.metres[i]-4),b=race_pace::At(centreline,centreline.metres[i]+4);
        double tx=b.x-a.x,ty=b.y-a.y,len=std::hypot(tx,ty);
        if(len<1e-6) {
            const auto& p=centreline.points[i?i-1:0];const auto& q=centreline.points[i+1<n?i+1:n-1];
            tx=q.x-p.x;ty=q.y-p.y;len=std::hypot(tx,ty);if(len<1e-9){tx=1;ty=0;len=1;}
        }
        normal[i]={-ty/len,tx/len};
    }
}
inline void Measure(const course::Path& path,const race_pace::Dynamics& dynamics,float& peak,float& mean,double& seconds) {
    race_pace::Plan plan;peak=mean=0;seconds=0;
    if(!plan.Build(path,dynamics))return;
    double sum=0;std::size_t count=0;
    for(std::size_t i=1;i+1<plan.curvature.size();++i){sum+=plan.curvature[i];++count;}
    peak=plan.peak_curvature;mean=count?static_cast<float>(sum/count):0;seconds=plan.nominal_seconds;
}
inline float PeakNodeAngleDegrees(const course::Path& path) {
    float peak=0;
    for(std::size_t i=1;i+1<path.points.size();++i) {
        const auto a=path.points[i-1],b=path.points[i],c=path.points[i+1];
        const double ux=b.x-a.x,uy=b.y-a.y,vx=c.x-b.x,vy=c.y-b.y;
        const double denominator=std::hypot(ux,uy)*std::hypot(vx,vy);
        if(denominator>1e-9)
            peak=std::max(peak,static_cast<float>(std::acos(std::clamp((ux*vx+uy*vy)/denominator,-1.0,1.0))*180/3.141592653589793));
    }
    return peak;
}
// One three-point bend. u=B-A and v=C-B are affine in lateral offsets.
// At most five route nodes: two at each chord end and the middle node.
struct Row {
    std::size_t node[5]{};double du[5]{},dv[5]{};int count=0;
    double mass=0;Vec2 u,v;
    void Add(std::size_t j,double a,double b) {
        for(int k=0;k<count;++k)if(node[k]==j){du[k]+=a;dv[k]+=b;return;}
        node[count]=j;du[count]=a;dv[count]=b;++count;
    }
    template<int derivative_index=-1>
    double Evaluate(const std::vector<double>& offsets,const std::vector<Vec2>& normal,double* jacobian=nullptr) const {
        Vec2 a=u,b=v;
        for(int p=0;p<count;++p) {
            const auto j=node[p];const double d=offsets[j];
            a.x+=du[p]*d*normal[j].x;a.y+=du[p]*d*normal[j].y;
            b.x+=dv[p]*d*normal[j].x;b.y+=dv[p]*d*normal[j].y;
        }
        const Vec2 c{a.x+b.x,a.y+b.y};
        const double aa=a.x*a.x+a.y*a.y,bb=b.x*b.x+b.y*b.y,cc=c.x*c.x+c.y*c.y;
        if(aa<1e-8||bb<1e-8||cc<1e-8)return std::numeric_limits<double>::infinity();
        const double denominator=std::sqrt(aa*bb*cc),k=2*(a.x*b.y-a.y*b.x)/denominator;
        if(jacobian)for(int p=0;p<count;++p) {
            if constexpr(derivative_index>=0)if(p!=derivative_index)continue;
            const auto n=normal[node[p]];
            const Vec2 da{du[p]*n.x,du[p]*n.y},db{dv[p]*n.x,dv[p]*n.y};
            const double numerator=2*(da.x*b.y-da.y*b.x+a.x*db.y-a.y*db.x);
            const double log_denominator=(a.x*da.x+a.y*da.y)/aa+(b.x*db.x+b.y*db.y)/bb+
                (c.x*(da.x+db.x)+c.y*(da.y+db.y))/cc;
            jacobian[p]=numerator/denominator-k*log_denominator;
        }
        return k;
    }
    // K1999 moves the middle node with fixed endpoints. Circumcircle
    // curvature is 2*sin(turn)/chord: beyond 90 degrees its derivative points
    // towards a sharper bend. This small-angle equivalent remains monotone
    // throughout (-pi,pi), without atan2 or additional optimizer passes.
    template<int derivative_index=-1>
    double TurningCurvature(const std::vector<double>& offsets,const std::vector<Vec2>& normal,double* jacobian=nullptr) const {
        Vec2 a=u,b=v;
        for(int p=0;p<count;++p) {
            const auto j=node[p];const double d=offsets[j];
            a.x+=du[p]*d*normal[j].x;a.y+=du[p]*d*normal[j].y;
            b.x+=dv[p]*d*normal[j].x;b.y+=dv[p]*d*normal[j].y;
        }
        const Vec2 c{a.x+b.x,a.y+b.y};
        const double aa=a.x*a.x+a.y*a.y,bb=b.x*b.x+b.y*b.y,cc=c.x*c.x+c.y*c.y;
        if(aa<1e-8||bb<1e-8||cc<1e-8)return std::numeric_limits<double>::infinity();
        const double cross=a.x*b.y-a.y*b.x,dot=a.x*b.x+a.y*b.y;
        const double chord=std::sqrt(cc),product=std::sqrt(aa*bb),denominator=product+dot;
        if(!(denominator>0))return std::numeric_limits<double>::infinity();
        const double k=4*cross/(denominator*chord);
        if(jacobian)for(int p=0;p<count;++p) {
            if constexpr(derivative_index>=0)if(p!=derivative_index)continue;
            const auto n=normal[node[p]];
            const Vec2 da{du[p]*n.x,du[p]*n.y},db{dv[p]*n.x,dv[p]*n.y};
            const double dcross=da.x*b.y-da.y*b.x+a.x*db.y-a.y*db.x;
            const double ddot=da.x*b.x+da.y*b.y+a.x*db.x+a.y*db.y;
            const double ddenominator=product*((a.x*da.x+a.y*da.y)/aa+(b.x*db.x+b.y*db.y)/bb)+ddot;
            jacobian[p]=4*dcross/(denominator*chord)-k*(ddenominator/denominator+
                (c.x*(da.x+db.x)+c.y*(da.y+db.y))/cc);
        }
        return k;
    }
};
} // namespace horse_races::racing_line::detail
