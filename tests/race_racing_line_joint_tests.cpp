// Adjacent-variable active-set regression. This intentionally enters the
// fine-scale state directly; it does not claim that every multiscale seed
// reaches the same vertex. Production geometry and inequalities are used.
#include "../src/race_racing_line_constraints.h"
#include "../src/race_racing_line_joint.h"
#include <chrono>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

using namespace horse_races;
namespace detail = racing_line::detail;
namespace {
std::size_t evaluations=0,circumcircle_evaluations=0;
struct CountRow : detail::Row {
    double Evaluate(const std::vector<double>& d,const std::vector<detail::Vec2>& n,double* jac=nullptr) const {
        ++circumcircle_evaluations;return detail::Row::Evaluate(d,n,jac);
    }
    double TurningCurvature(const std::vector<double>& d,const std::vector<detail::Vec2>& n,double* jac=nullptr) const {
        ++evaluations;return detail::Row::TurningCurvature(d,n,jac);
    }
};
struct BoundInc {std::size_t other;double ratio,low,high;};
using Incidence=std::vector<std::vector<BoundInc>>;
void Check(bool condition,const std::string& message) {if(!condition)throw std::runtime_error(message);}
struct Fixture {
    course::Path path;
    std::vector<detail::Vec2> normal;
    std::vector<double> lo,hi,d;
    std::vector<detail::Constraint> constraints;
    std::vector<CountRow> rows;
    Incidence inc;
    std::size_t i=0,j=0;
    Fixture(std::size_t size,std::size_t at,double spacing,double amplitude,double ratio1=.01,double ratio2=.2,bool mirrored=false)
        :i(at),j(at+1) {
        Check(at>1&&at+2<size,"fixture requires four affected interior rows");
        std::vector<Vector3> points;
        for(std::size_t k=0;k<size;++k)points.push_back({static_cast<float>(spacing*k),0,0});
        Check(path.Build(points,0),"path build");detail::Frame(path,normal);
        lo.assign(size,0);hi.assign(size,0);d.assign(size,0);rows.resize(size);
        for(auto k:{i,j}){lo[k]=-2*amplitude;hi[k]=2*amplitude;}
        d[j]=mirrored?-amplitude:amplitude;
        const double sign=mirrored?-1:1;
        // At (0,A) each one-dimensional feasible interval is a singleton.
        // Jointly moving along (+ratio1,-1) remains inside both halfspaces.
        constraints.push_back({i,j,sign,sign*ratio1,ratio1*amplitude,1e6});
        constraints.push_back({i,j,sign,sign*ratio2,-1e6,ratio2*amplitude});
        for(std::size_t k=1;k+1<size;++k) {
            auto& row=rows[k];row.Add(k-1,-1,0);row.Add(k,1,-1);row.Add(k+1,0,1);
            row.u={path.points[k].x-path.points[k-1].x,path.points[k].y-path.points[k-1].y};
            row.v={path.points[k+1].x-path.points[k].x,path.points[k+1].y-path.points[k].y};
        }
        Incidences();
    }
    void Incidences(bool boxes=false) {
        inc.assign(d.size(),{});
        auto add=[&](std::size_t i,std::size_t j,double a,double b,double lo,double hi) {
            if(std::abs(a)<1e-12)return;
            double l=lo/a,h=hi/a;if(l>h)std::swap(l,h);
            inc[i].push_back({j,b/a,l,h});
        };
        for(const auto& c:constraints) {
            if(c.i==c.j)add(c.i,c.i,c.a+c.b,0,c.lo,c.hi);
            else{add(c.i,c.j,c.a,c.b,c.lo,c.hi);add(c.j,c.i,c.b,c.a,c.lo,c.hi);}
        }
        // Deliberately optional: the helper receives lo/hi explicitly, so
        // direct API callers should not need to repeat them as unary rows.
        if(boxes)for(std::size_t k=0;k<d.size();++k)add(k,k,1,0,lo[k],hi[k]);
    }
    std::pair<double,double> ScalarRange(std::size_t node) const {
        double l=lo[node],h=hi[node];
        for(const auto& c:inc[node]){l=std::max(l,c.low-c.ratio*d[c.other]);h=std::min(h,c.high-c.ratio*d[c.other]);}
        return {l,h};
    }
    double Score() const {
        double score=0;
        for(std::size_t k=1;k+1<d.size();++k){const auto c=rows[k].TurningCurvature(d,normal);const auto c2=c*c+1e-6;score+=.5*(path.metres[k+1]-path.metres[k-1])*c2*c2;}
        return score;
    }
    double Violation() const {
        double worst=0;
        for(std::size_t k=0;k<d.size();++k)worst=std::max({worst,lo[k]-d[k],d[k]-hi[k]});
        for(const auto& c:constraints){const auto v=c.Value(d);worst=std::max({worst,c.lo-v,v-c.hi});}
        return worst;
    }
    std::vector<double> Violations() const {
        std::vector<double> values;
        for(std::size_t k=0;k<d.size();++k)values.push_back(std::max({0.,lo[k]-d[k],d[k]-hi[k]}));
        for(const auto& c:constraints){const auto v=c.Value(d);values.push_back(std::max({0.,c.lo-v,v-c.hi}));}
        return values;
    }
    double PeakAngle() const {
        std::vector<Vector3> points=path.points;
        for(std::size_t k=0;k<d.size();++k){points[k].x+=static_cast<float>(normal[k].x*d[k]);points[k].y+=static_cast<float>(normal[k].y*d[k]);}
        course::Path shifted;Check(shifted.Build(points,0),"shifted path build");
        return detail::PeakNodeAngleDegrees(shifted);
    }
    double Joint() {return detail::ImproveAdjacentCurvature(path,normal,lo,hi,d,constraints,inc,rows);}
};
struct Results {int pass=0,fail=0;};
template<class F> void Test(Results& results,const char* name,F&& f) {
    try{f();++results.pass;std::cout<<"PASS "<<name<<'\n';}
    catch(const std::exception& e){++results.fail;std::cout<<"FAIL "<<name<<": "<<e.what()<<'\n';}
}
void LockAndImprove(std::size_t at,double spacing,double amplitude,double r1,double r2,bool mirrored) {
    Fixture f(15,at,spacing,amplitude,r1,r2,mirrored);
    const auto xr=f.ScalarRange(f.i),yr=f.ScalarRange(f.j);
    Check(std::abs(xr.second-xr.first)<1e-12&&std::abs(yr.second-yr.first)<1e-12,"single-coordinate feasible intervals are not singleton");
    Check(f.Violation()<1e-10,"start violates unchanged safety domain");
    const auto before=f.Score(),angle=f.PeakAngle();
    const auto original_lo=f.lo,original_hi=f.hi;const auto original=f.d;
    const auto original_constraints=f.constraints;
    std::size_t calls=0;
    for(int pass=0;pass<10;++pass) {
        const auto score=f.Score();evaluations=0;f.Joint();calls+=evaluations;
        Check(f.Score()<=score+1e-14,"pair pass increased turning curvature score");
        Check(f.Violation()<1e-9,"pair left original feasible domain");
    }
    Check(f.Score()<.9*before,"joint direction did not materially improve locked state");
    Check(f.PeakAngle()<angle,"joint direction did not reduce peak node angle");
    Check(f.lo==original_lo&&f.hi==original_hi,"helper modified node bounds");
    for(std::size_t k=0;k<f.d.size();++k)if(k!=f.i&&k!=f.j)Check(f.d[k]==original[k],"helper moved unrelated fixed node");
    Check(f.constraints.size()==original_constraints.size(),"helper changed constraint count");
    for(std::size_t k=0;k<f.constraints.size();++k) {
        const auto& a=f.constraints[k];const auto& b=original_constraints[k];
        Check(a.i==b.i&&a.j==b.j&&a.a==b.a&&a.b==b.b&&a.lo==b.lo&&a.hi==b.hi,"helper changed safety constraint");
    }
    Check(calls<=20*f.constraints.size()*10,"exceeded four backtracks/four local rows per edge");
    std::cout<<"  lock at="<<at<<" spacing="<<spacing<<" amplitude="<<amplitude<<" mirror="<<mirrored
        <<" objective="<<before<<" -> "<<f.Score()<<" peak="<<angle<<" -> "<<f.PeakAngle()
        <<" final="<<f.d[f.i]<<','<<f.d[f.j]<<" row_evaluations="<<calls<<'\n';
}
}
int main() {
    std::cout.precision(10);Results results;
    Test(results,"generic adjacent scalar lock is escapable in same domain",[]{LockAndImprove(3,6,4,.01,.2,false);});
    Test(results,"shifted/scaled/mirrored family",[]{
        for(auto at:{2u,5u,9u})for(double scale:{.5,1.,2.})for(bool mirrored:{false,true})
            LockAndImprove(at,6*scale,4*scale,.02,.3,mirrored);
    });
    Test(results,"large fold cannot improve by circumcircle alias",[]{
        Fixture f(11,4,3,12);f.constraints[1].hi=100;f.Incidences(true);
        const auto before=f.Score(),initial_angle=f.PeakAngle();
        Check(initial_angle>140,"fixture did not create a large turning angle");
        // The raw circumcircle magnitude decreases beyond a right angle.
        // Demonstrate that tempting but worse direction geometrically.
        const auto original=f.d;const auto initial_circum=std::abs(f.rows[f.j].Evaluate(f.d,f.normal));
        f.d[f.i]=-.01;f.d[f.j]+=1;
        Check(f.Violation()<1e-9,"worse fold witness not feasible");
        Check(f.PeakAngle()>initial_angle&&f.Score()>before,"larger fold did not increase turning cost");
        Check(std::abs(f.rows[f.j].Evaluate(f.d,f.normal))<initial_circum,"circumcircle alias witness missing");
        f.d=original;circumcircle_evaluations=0;
        for(int pass=0;pass<10;++pass) {
            const auto score=f.Score(),angle=f.PeakAngle();f.Joint();
            Check(f.Score()<=score+1e-12,"accepted larger turning energy");
            Check(f.PeakAngle()<=angle+1e-5,"accepted a sharper large fold");
            Check(f.Violation()<1e-9,"large-fold repair left original safety domain");
        }
        Check(f.PeakAngle()<initial_angle&&f.Score()<before,"large fold was not reduced");
        Check(circumcircle_evaluations==0,"helper still selected steps with circumcircle curvature");
        std::cout<<"  fold peak="<<initial_angle<<" -> "<<f.PeakAngle()<<" turning_energy="<<before<<" -> "<<f.Score()<<'\n';
    });
    Test(results,"fixed node retained without redundant unary incidence",[]{
        Fixture f(11,4,6,4);f.lo[f.i]=f.hi[f.i]=0;f.Incidences();
        for(int pass=0;pass<10;++pass)f.Joint();
        Check(f.d[f.i]==0,"pair helper moved explicitly fixed node");Check(f.Violation()<1e-9,"pair helper violated explicit lo/hi");
    });
    Test(results,"tight explicit box retained without unary incidence",[]{
        Fixture f(11,4,6,4);f.lo[f.j]=3.9;f.Incidences();
        for(int pass=0;pass<10;++pass)f.Joint();Check(f.Violation()<1e-9,"pair helper crossed explicit node bound");
    });
    Test(results,"unary constraints and unrelated neighbouring bound retained",[]{
        Fixture f(11,4,6,4);f.constraints.push_back({f.j,f.j,1,0,3.8,4.2});
        f.constraints.push_back({f.j,f.j+1,1,.3,3.85,4.5});f.Incidences(true);
        for(int pass=0;pass<10;++pass)f.Joint();Check(f.Violation()<1e-9,"pair crossed unary or neighbouring section");
    });
    Test(results,"existing tiny linearization violation never increases",[]{
        for(bool mirror:{false,true}) {
            Fixture f(11,4,6,4,.01,.2,mirror);f.constraints[0].lo+=2e-5;f.Incidences(true);
            const auto initial=f.Violations();const auto before=f.Score();
            for(int pass=0;pass<10;++pass) {
                f.Joint();const auto after=f.Violations();
                for(std::size_t k=0;k<initial.size();++k)Check(after[k]<=initial[k]+1e-10,"pair increased one existing residual");
            }
            Check(f.Score()<before,"soft residual prevented all improvement");
        }
    });
    Test(results,"no active shared section leaves ordinary solve unchanged",[]{
        Fixture f(11,4,6,4);const auto original=f.d;
        for(auto& edge:f.constraints){edge.lo=-100;edge.hi=100;}f.Incidences(true);
        evaluations=0;Check(f.Joint()==0&&f.d==original,"inactive sections changed line");Check(evaluations==0,"inactive sections evaluated curvature");
        f.constraints.clear();f.Incidences(true);
        Check(f.Joint()==0&&f.d==original,"empty physical survey changed line");
    });
    Test(results,"bounded local work independent of total path length",[]{
        for(auto size:{32u,1024u,16384u}) {
            Fixture f(size,4,6,4);f.Incidences(true);evaluations=0;const auto start=std::chrono::steady_clock::now();
            for(int pass=0;pass<10;++pass)f.Joint();
            const auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
            Check(evaluations<=20*f.constraints.size()*10,"pair scans entire path curvature");
            Check(f.Violation()<1e-9,"bounded-work run left feasible domain");
            std::cout<<"  nodes="<<size<<" row_evaluations="<<evaluations<<" time_ms="<<ms<<'\n';
        }
    });
    Test(results,"work grows linearly with number of separate active pairs",[]{
        for(auto pairs:{4u,64u,512u}) {
            Fixture f(4*pairs+3,2,6,4);f.constraints.clear();
            for(std::size_t pair=0;pair<pairs;++pair) {
                const auto i=2+4*pair,j=i+1;f.d[j]=4;
                f.lo[i]=f.lo[j]=-8;f.hi[i]=f.hi[j]=8;
                f.constraints.push_back({i,j,1,.01,.04,1e6});
                f.constraints.push_back({i,j,1,.2,-1e6,.8});
            }
            f.Incidences(true);evaluations=0;const auto start=std::chrono::steady_clock::now();
            for(int pass=0;pass<10;++pass)f.Joint();
            const auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
            Check(evaluations<=20*f.constraints.size()*10,"exceeded fixed local work per pair");
            Check(f.Violation()<1e-9,"many-pair run left feasible domain");
            std::cout<<"  pairs="<<pairs<<" row_evaluations="<<evaluations<<" time_ms="<<ms<<'\n';
        }
    });
    std::cout<<"SUMMARY pass="<<results.pass<<" fail="<<results.fail<<'\n';return results.fail?1:0;
}
