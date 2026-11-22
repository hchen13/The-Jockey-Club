#pragma once
#include "race_racing_line_geometry.h"

namespace horse_races::racing_line::detail {
// A binding section can lock both scalar coordinates even when moving its
// endpoints together would reduce curvature. Move along that section's
// tangent, retaining every node box and the other linearized sections. The
// existing actual-section projection and final positive/negative checks still
// decide feasibility; this step grants no new clearance certificate.
template<class Bounds,class Offsets,class Constraints,class Incidence,class Rows>
double ImproveAdjacentCurvature(const course::Path& path,const std::vector<Vec2>& normals,
    const Bounds& lo,const Bounds& hi,Offsets& d,const Constraints& constraints,const Incidence& inc,const Rows& rows) {
    if(d.size()<3)return 0;
    double movement=0;
    for(const auto& edge:constraints) {
        const auto i=edge.i,j=edge.j;
        if(i>=d.size()||j>=d.size()||j!=i+1||lo[i]==hi[i]||lo[j]==hi[j])continue;
        // Use the scalar solver's convergence tolerance only to select work.
        // Inactive/open sections must not alter the unconstrained solution.
        const double value=edge.Value(d);
        if(value>edge.lo+1e-5&&value<edge.hi-1e-5)continue;
        const double norm=std::max(std::abs(edge.a),std::abs(edge.b));if(norm<1e-12)continue;
        const double vi=edge.b/norm,vj=-edge.a/norm;
        double lower=-2,upper=2;
        auto direction=[&](std::size_t k){return k==i?vi:k==j?vj:0.;};
        for(auto node:{i,j})if(std::abs(direction(node))>1e-12) {
            double a=(lo[node]-d[node])/direction(node),b=(hi[node]-d[node])/direction(node);
            if(a>b)std::swap(a,b);lower=std::max(lower,a);upper=std::min(upper,b);
        }
        for(auto node:{i,j})for(const auto& c:inc[node]) {
            const double current=d[node]+c.ratio*d[c.other];
            const double rate=direction(node)+c.ratio*direction(c.other);
            // A refreshed nonlinear cut can leave a tiny inset residual.
            // Never increase it while escaping a scalar coordinate lock.
            const double low=std::min(c.low,current),high=std::max(c.high,current);
            if(std::abs(rate)<1e-12)continue;
            double a=(low-current)/rate,b=(high-current)/rate;if(a>b)std::swap(a,b);
            lower=std::max(lower,a);upper=std::min(upper,b);
        }
        if(lower>=upper)continue;
        const auto first=i>1?i-1:1,last=std::min(d.size()-2,j+1);
        double before=0,gradient=0,hessian=0;
        for(std::size_t k=first;k<=last;++k) {
            double jac[5]{};const auto& row=rows[k];const double curvature=row.TurningCurvature(d,normals,jac);
            double derivative=0;for(int p=0;p<row.count;++p)derivative+=jac[p]*direction(row.node[p]);
            const double mass=.5*(path.metres[k+1]-path.metres[k-1]),k2=curvature*curvature+1e-6;
            before+=mass*k2*k2;gradient+=mass*k2*curvature*derivative;
            hessian+=mass*(3*curvature*curvature+1e-6)*derivative*derivative;
        }
        if(!std::isfinite(before)||!std::isfinite(gradient)||!std::isfinite(hessian)||hessian<1e-14)continue;
        // Same displacement bound and damping as a normal K1999 update.
        double step=std::clamp(.6*std::clamp(-gradient/hessian,-2.,2.),lower,upper);
        if(std::abs(step)<1e-7)continue;
        const double a=d[i],b=d[j];bool accepted=false;
        for(int backtrack=0;backtrack<4;++backtrack,step*=.5) {
            d[i]=a+step*vi;d[j]=b+step*vj;double score=0;
            for(std::size_t k=first;k<=last;++k) {
                const auto curvature=rows[k].TurningCurvature(d,normals);const double k2=curvature*curvature+1e-6;
                score+=.5*(path.metres[k+1]-path.metres[k-1])*k2*k2;
            }
            if(score<before){accepted=true;movement=std::max(movement,std::max(std::abs(step*vi),std::abs(step*vj)));break;}
        }
        if(!accepted){d[i]=a;d[j]=b;}
    }
    return movement;
}
} // namespace horse_races::racing_line::detail
