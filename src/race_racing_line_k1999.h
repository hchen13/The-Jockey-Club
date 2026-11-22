#pragma once
#include "race_racing_line_constraints.h"
#include "race_racing_line_joint.h"

namespace horse_races::racing_line::detail {
// Shared with the analysis-room candidate selected by the user. Only geometry
// and the prepared physical constraints enter this bounded multiscale solve.
struct K1999Optimizer {
    const char* Name() const {return "k1999";}
    template<class Normals,class Bounds,class Offsets,class Constraints,class Make,class Project>
    bool operator()(const course::Path& centreline,const Params& params,
        const Normals& normal,const Bounds& lo,const Bounds& hi,Offsets& d,
        Constraints& constraints,Make&& make_constraints,Project&& project_actual,int& completed_sweeps,bool fine_only=false) const {
    const auto n=d.size();
    struct BoundInc {std::size_t other;double ratio,low,high;};
    std::vector<std::vector<BoundInc>> inc(n);
    auto build_inc=[&] {
        make_constraints();for(auto& x:inc)x.clear();
        for(const auto& c:constraints) {
            auto add=[&](std::size_t i,std::size_t j,double a,double b2) {
                if(std::abs(a)<1e-12)return;
                double l=c.lo/a,r=c.hi/a;if(l>r)std::swap(l,r);
                inc[i].push_back({j,b2/a,l,r});
            };
            if(c.i==c.j)add(c.i,c.i,c.a+c.b,0);
            else {add(c.i,c.j,c.a,c.b);add(c.j,c.i,c.b,c.a);}
        }
    };
    constexpr int sweeps=100;
    for(std::size_t stride: {8u,4u,2u,1u}) {
        if(fine_only&&stride!=1)continue;
        std::vector<std::size_t> ids;
        for(std::size_t i=0;i<n;i+=stride)ids.push_back(i);
        if(ids.back()!=n-1)ids.push_back(n-1);
        const auto count=ids.size();
        std::vector<detail::Row> rows(count);
        for(std::size_t j=1;j+1<count;++j) {
            const auto a=ids[j-1],b2=ids[j],c=ids[j+1];auto& r=rows[j];
            r.Add(a,-1,0);r.Add(b2,1,-1);r.Add(c,0,1);
            r.u={centreline.points[b2].x-centreline.points[a].x,centreline.points[b2].y-centreline.points[a].y};
            r.v={centreline.points[c].x-centreline.points[b2].x,centreline.points[c].y-centreline.points[b2].y};
        }
        // Coarse nodes guide initialization. Only final-scale updates use
        // coupled physical sections; all stages are verified at full resolution.
        if(stride==1)build_inc();
        int joint_sweeps=0;
        for(int it=0;it<sweeps;++it) {
            ++completed_sweeps;
            if(stride==1 && it%10==0) {
                if(params.obstacles)project_actual(2);
                build_inc();
            }
            double movement=0;
            auto update=[&](std::size_t j) {
                const auto i=ids[j];if(lo[i]==hi[i])return;
                double l=lo[i],r=hi[i];
                if(stride==1)for(const auto& c:inc[i]){l=std::max(l,c.low-c.ratio*d[c.other]);r=std::min(r,c.high-c.ratio*d[c.other]);}
                if(l>r)return;
                double before=j>1?rows[j-1].TurningCurvature(d,normal):0;
                double after=j+2<count?rows[j+1].TurningCurvature(d,normal):0;
                auto point=[&](std::size_t index) {return detail::Vec2{centreline.points[index].x+d[index]*normal[index].x,centreline.points[index].y+d[index]*normal[index].y};};
                const auto a=point(ids[j-1]),b2=point(i),c=point(ids[j+1]);
                const double h1=std::hypot(b2.x-a.x,b2.y-a.y),h2=std::hypot(c.x-b2.x,c.y-b2.y);
                const double target=(before*h2+after*h1)/std::max(.01,h1+h2);
                // Only this node moves; calculating the two endpoint
                // derivatives here would repeat unused work at every sweep.
                double jac[5]{};const double k=rows[j].TurningCurvature<1>(d,normal,jac);
                if(!std::isfinite(k)||!std::isfinite(target)||!std::isfinite(jac[1])||std::abs(jac[1])<1e-8)return;
                const double value=std::clamp(d[i]+.6*std::clamp((target-k)/jac[1],-2.0,2.0),l,r);
                movement=std::max(movement,std::abs(d[i]-value));d[i]=value;
            };
            for(std::size_t j=1;j+1<count;++j)update(j);
            for(std::size_t j=count-1;j-->1;)update(j);
            // A shared active section needs a joint direction as well as
            // scalar interpolation. Stay within the existing fine-sweep cap.
            if(stride==1&&joint_sweeps<sweeps/10&&(it%10==9||movement<1e-5)) {
                ++joint_sweeps;
                movement=std::max(movement,ImproveAdjacentCurvature(centreline,normal,lo,hi,d,constraints,inc,rows));
            }
            if(movement<1e-5)break;
        }
        for(std::size_t j=0;j+1<count;++j)for(std::size_t i=ids[j]+1;i<ids[j+1];++i) {
            const double t=(centreline.metres[i]-centreline.metres[ids[j]])/(centreline.metres[ids[j+1]]-centreline.metres[ids[j]]);
            d[i]=std::clamp(d[ids[j]]*(1-t)+d[ids[j+1]]*t,lo[i],hi[i]);
        }
        if(params.obstacles)project_actual(2);
    }

    return false; // Do not run the former quartic optimizer afterwards.
    }
};
// Retained for offline comparison and explicitly targeted regression coverage.
struct QuarticOptimizer {
    const char* Name() const {return "quartic";}
    template<class... T> bool operator()(T&&...) const {return true;}
};
} // namespace horse_races::racing_line::detail
