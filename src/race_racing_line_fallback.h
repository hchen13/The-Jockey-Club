#pragma once
#include "race_racing_line_space.h"

namespace horse_races::racing_line::detail {
inline std::vector<double> BranchSeed(const course::Path& path,const std::vector<Vec2>& normal,
    const std::vector<SectionBound>& rows) {
    std::vector<double> seed(path.points.size(),0);
    if(rows.size()<2)return seed;
    std::size_t r=1;
    for(std::size_t i=1;i+1<seed.size();++i) {
        while(r+1<rows.size()&&rows[r].along<path.metres[i])++r;
        const auto& a=rows[r-1];const auto& b=rows[r];
        if(a.recorded||b.recorded||!(b.along>a.along))continue;
        const double t=std::clamp((path.metres[i]-a.along)/(b.along-a.along),0.0F,1.0F);
        const double x=(1-t)*(a.section.centre.x+a.seed*a.section.nx)+t*(b.section.centre.x+b.seed*b.section.nx);
        const double y=(1-t)*(a.section.centre.y+a.seed*a.section.ny)+t*(b.section.centre.y+b.seed*b.section.ny);
        seed[i]=(x-path.points[i].x)*normal[i].x+(y-path.points[i].y)*normal[i].y;
    }
    return seed;
}
// A recorded chord conflicting with negative evidence enters this bounded
// search: 24/48 m each way, <=64 nodes, <=33 candidates/node. Every changed
// chord needs independent continuous positive proof; unknown is never a bypass.
// If the artificial right boundary cannot rejoin, carry a checked offset to
// the next window; the actual course endpoints always remain fixed.
class FallbackRepair {
    const course::Path& path;
    const Params& params;
    const std::vector<Vec2>& normals;
    const std::vector<double> lower,upper;
    NegativeClearance negative;
    std::optional<PositiveClearance> positive;
    Vector3 Point(std::size_t i,double d) const {
        const auto p=path.points[i];return {static_cast<float>(p.x+d*normals[i].x),static_cast<float>(p.y+d*normals[i].y),p.z};
    }
    struct State {double d=0,cost=1e30;float along=0;Vector3 point{};int previous=-1;double secondary=1e30;};
    std::vector<State> Candidates(std::size_t i,double current,bool anchored) const {
        std::vector<double> values;
        auto add=[&](double value) {
            if(values.size()>=33||!std::isfinite(value)||value<lower[i]||value>upper[i])return;
            for(double old:values)if(std::abs(old-value)<1e-4)return;
            values.push_back(value);
        };
        add(current);
        if(!anchored) {
            add(std::clamp(0.0,lower[i],upper[i]));
            const auto& map=*params.obstacles;const float keep=params.ProofClearance();
            const auto row=map.Index(path.metres[i]);
            for(std::size_t r=row>1?row-2:0;r<std::min(map.rows.size(),row+3)&&values.size()<9;++r) {
                const auto frame=map.SectionFor(path,r);
                if(r)for(const auto& strip:map.ConnectionStrips(r,keep)) {
                    for(auto end:{std::pair{map.SectionFor(path,r-1),strip.from},std::pair{frame,strip.to}}) {
                        const double lateral=.5*(end.second.lo+end.second.hi);
                        const double x=end.first.centre.x+lateral*end.first.nx-path.points[i].x;
                        const double y=end.first.centre.y+lateral*end.first.ny-path.points[i].y;
                        add(x*normals[i].x+y*normals[i].y);
                    }
                    if(values.size()>=9)break;
                }
                for(auto span:map.rows[r].ClearIntervals(keep)) {
                    if(span.hi-span.lo<.006F)continue;
                    const double lateral=std::clamp(0.0,static_cast<double>(span.lo+.003F),static_cast<double>(span.hi-.003F));
                    const double x=frame.centre.x+lateral*frame.nx-path.points[i].x;
                    const double y=frame.centre.y+lateral*frame.ny-path.points[i].y;
                    add(x*normals[i].x+y*normals[i].y);
                    if(values.size()>=9)break;
                }
            }
            for(int k=0;k<=23;++k)add(lower[i]+(upper[i]-lower[i])*k/23);
        }
        std::vector<State> out;
        for(double d:values) {
            const auto point=Point(i,d);
            const auto projection=ProjectNear(path,point,path.metres[i],std::max(60.0F,3*params.Usable()+30));
            if(projection.valid&&projection.distance<=params.Usable()-.01F)
                out.push_back({d,1e30,projection.along,point,-1});
        }
        return out;
    }
    bool Window(std::size_t bad,float radius,std::vector<double>& d,
        std::vector<unsigned char>& recorded,std::vector<double>& lo,std::vector<double>& hi,bool open_end=false) {
        std::size_t first=bad,last=bad+1;
        while(first&&bad-first<31&&path.metres[bad]-path.metres[first-1]<=radius)--first;
        while(last+1<d.size()&&last-bad<32&&path.metres[last+1]-path.metres[bad+1]<=radius)++last;
        std::vector<std::vector<State>> layers(last-first+1);
        for(std::size_t i=first;i<=last;++i)layers[i-first]=Candidates(i,d[i],i==first||(i==last&&!open_end)||i+1==d.size());
        if(layers.front().empty())return false;
        layers.front().front().cost=layers.front().front().secondary=0;
        auto bend=[](Vector3 a,Vector3 b,Vector3 c) {
            const double ux=b.x-a.x,uy=b.y-a.y,vx=c.x-b.x,vy=c.y-b.y;
            const double ab=std::hypot(ux,uy),bc=std::hypot(vx,vy),ac=std::hypot(ux+vx,uy+vy);
            const double denominator=(ab*bc+ux*vx+uy*vy)*ac;
            if(!(denominator>1e-12))return 1e30;
            const double k=4*(ux*vy-uy*vx)/denominator;
            return .5*(ab+bc)*k*k*k*k;
        };
        for(std::size_t i=first+1;i<=last;++i) {
            auto& current=layers[i-first];const auto& previous=layers[i-first-1];
            for(auto& to:current)for(std::size_t j=0;j<previous.size();++j) {
                const auto& from=previous[j];if(from.cost>=1e29||!(to.along>from.along))continue;
                const auto a=path.points[i-1],b=path.points[i];
                const double dx=to.point.x-from.point.x,dy=to.point.y-from.point.y;
                if(dx*(b.x-a.x)+dy*(b.y-a.y)<=.01||std::hypot(dx,dy)<.05)continue;
                const double length=path.metres[i]-path.metres[i-1];
                // Keep one best prefix per candidate: O(nodes*candidates^2),
                // a bounded local approximation, not a global curvature solve.
                // Include both joins so the repair cannot hide a corner at
                // its first or last anchor. Lateral travel only breaks ties.
                double cost=from.cost;
                if(i>first+1||first) {
                    const auto before=i>first+1?layers[i-first-2][from.previous].point:Point(first-1,d[first-1]);
                    cost+=bend(before,from.point,to.point);
                }
                if(i==last&&last+1<d.size())cost+=bend(from.point,to.point,Point(last+1,d[last+1]));
                const double secondary=from.secondary+length*(from.d*from.d+from.d*to.d+to.d*to.d)/3+
                    4*(to.d-from.d)*(to.d-from.d);
                if(cost>to.cost+1e-18||(std::abs(cost-to.cost)<=1e-18&&secondary>=to.secondary)||
                    !negative.Allows(from.point,to.point,i-1)||
                    ((from.d!=0||to.d!=0)&&!PositiveAllows(from.point,to.point,i-1)))continue;
                to.cost=cost;to.secondary=secondary;to.previous=static_cast<int>(j);
            }
        }
        int chosen=-1;double best=1e30,secondary=1e30;
        for(std::size_t j=0;j<layers.back().size();++j) {
            const auto& state=layers.back()[j];
            if(state.cost<best-1e-18||(std::abs(state.cost-best)<=1e-18&&state.secondary<secondary))
                {best=state.cost;secondary=state.secondary;chosen=static_cast<int>(j);}
        }
        if(chosen<0||best>=1e29)return false;
        const double old_last=d[last];
        for(std::size_t i=last+1;i-->first;) {
            const auto& state=layers[i-first][chosen];d[i]=retained[i]=state.d;
            lo[i]=hi[i]=state.d;chosen=state.previous;
        }
        for(std::size_t i=first;i<last;++i)recorded[i]=1;
        // A changed boundary also changes its OUTGOING chord. Keep it in the
        // forward repair scan and fix its other endpoint at the current value.
        if(last+1<d.size()&&d[last]!=old_last) {
            recorded[last]=1;retained[last+1]=d[last+1];lo[last+1]=hi[last+1]=d[last+1];
        }
        return true;
    }
public:
    std::vector<double> retained;
    FallbackRepair(const course::Path& reference,const Params& policy,const std::vector<Vec2>& normal,
        const std::vector<double>& lo,const std::vector<double>& hi)
        :path(reference),params(policy),normals(normal),lower(lo),upper(hi),negative(reference,policy),retained(lo.size(),0){}
    bool PositiveAllows(Vector3 a,Vector3 b,std::size_t source) {
        if(!negative.Allows(a,b,source))return false;
        if(!positive)positive.emplace(path,params);
        return positive->Covers(a,b,source);
    }
    bool Repair(std::vector<double>& d,std::vector<unsigned char>& recorded,
        std::vector<double>& lo,std::vector<double>& hi) {
        if(!params.obstacles)return true;
        for(std::size_t i=0;i+1<d.size();++i)if(recorded[i]&&
            (!negative.Allows(Point(i,d[i]),Point(i+1,d[i+1]),i)||
             ((d[i]!=0||d[i+1]!=0)&&!PositiveAllows(Point(i,d[i]),Point(i+1,d[i+1]),i)))) {
            if(!Window(i,24,d,recorded,lo,hi)&&!Window(i,48,d,recorded,lo,hi)&&
                !Window(i,24,d,recorded,lo,hi,true))return false;
        }
        return true;
    }
};
} // namespace horse_races::racing_line::detail
