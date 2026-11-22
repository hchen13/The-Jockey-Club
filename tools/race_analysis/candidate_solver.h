// Offline comparisons: the production K1999 core, the previous quartic solver,
// their hybrid, and externally optimized seeds under the same physical checks.
// File I/O is confined to this analysis adapter.
#pragma once
#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>
#include "../../src/race_racing_line.h"
#include "bounded_output.h"
namespace horse_races::analysis_candidates {
struct Optimizer {
    std::string kind="current",seed_file,constraints_file;
    void Validate() const {
        if(kind!="current"&&kind!="legacy"&&kind!="k1999"&&kind!="hybrid"&&kind!="seed")
            throw std::runtime_error("unknown analysis algorithm");
        if((kind=="seed")!=!seed_file.empty())throw std::runtime_error("seed only allowed for seed verification");
        if(kind=="current"&&!constraints_file.empty())
            throw std::runtime_error("current uses the complete production pipeline; use --algorithm k1999 to export seed constraints");
    }
    const char* Name() const {return kind=="current"||kind=="k1999"?"k1999":kind=="legacy"?"quartic":kind.c_str();}
    template<class Normals,class Bounds,class Offsets,class Constraints,class Make,class Project>
    bool operator()(const course::Path& centreline,const racing_line::Params& params,
        const Normals& normal,const Bounds& lo,const Bounds& hi,Offsets& d,
        Constraints& constraints,Make&& make_constraints,Project&& project_actual,int& completed_sweeps) const {
    if(kind=="legacy")return true;
    using namespace racing_line;
    const auto n=d.size();

    if(kind=="seed") {
        const auto& file=seed_file;
        std::ifstream f(file);std::vector<std::pair<double,double>> seed;double a,b2;
        while(f>>a>>b2)seed.emplace_back(a,b2);
        if(seed.size()!=n)throw std::runtime_error("seed node count differs from solver input");
        for(std::size_t i=0;i<n;++i)if(!std::isfinite(seed[i].second)||std::abs(seed[i].first-centreline.metres[i])>1e-4)
            throw std::runtime_error("seed geometry differs from solver input");
        if(seed.size()>1) {
            for(std::size_t i=0;i<n;++i) {
                const double s=centreline.metres[i];auto it=std::upper_bound(seed.begin(),seed.end(),std::make_pair(s,1e30));
                if(it==seed.begin())d[i]=it->second;else if(it==seed.end())d[i]=seed.back().second;
                else {const auto prev=it-1;const double t=(s-prev->first)/(it->first-prev->first);d[i]=prev->second*(1-t)+it->second*t;}
                d[i]=std::clamp(d[i],lo[i],hi[i]);
            }
            if(params.obstacles)project_actual(3);
        }
    }
    else racing_line::detail::K1999Optimizer{}(centreline,params,normal,lo,hi,d,
        constraints,make_constraints,project_actual,completed_sweeps);

    if(!constraints_file.empty()) {
        const auto& path=constraints_file;
        make_constraints();analysis_output::Buffer output;std::ostream f(&output);f.precision(12);
        f<<"{\"nodes\":[";
        for(std::size_t i=0;i<n;++i) {
            if(i)f<<',';
            f<<'['<<centreline.metres[i]<<','<<centreline.points[i].x<<','<<centreline.points[i].y<<','<<normal[i].x<<','<<normal[i].y<<','<<lo[i]<<','<<hi[i]<<','<<d[i]<<']';
        }
        f<<"],\"constraints\":[";
        for(std::size_t j=0;j<constraints.size();++j) {
            if(j)f<<',';const auto& c=constraints[j];
            f<<'['<<c.i<<','<<c.j<<','<<c.a<<','<<c.b<<','<<c.lo<<','<<c.hi<<']';
        }
        f<<"]}";
        if(!f)throw std::runtime_error("solver constraints exceed 32 MiB capacity; previous output preserved");
        analysis_output::Publish(path,output.Bytes());
    }

    return kind=="hybrid";
    }
};
}
