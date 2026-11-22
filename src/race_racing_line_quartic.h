#pragma once
#include "race_racing_line_geometry.h"

namespace horse_races::racing_line::detail {
struct QuarticSystem {
    static constexpr double curvature_floor2=1e-6;
    const std::vector<Vec2>& normal;
    const std::vector<Row>& chord_rows;
    const std::vector<Row>& local_rows;
    const std::vector<double>& lo;
    const std::vector<double>& hi;
    std::vector<double>& d;
    Result& out;
    const std::size_t n,bandwidth;
    Band h,reduced;
    std::vector<double> b,rhs,lower,diag;
    std::vector<int> clamped; // 0 free, +1 at hi, -1 at lo, 2 pinned to zero.
    QuarticSystem(const std::vector<Vec2>& frame,const std::vector<Row>& chords,
        const std::vector<Row>& local,const std::vector<double>& low,const std::vector<double>& high,
        std::vector<double>& offsets,std::size_t bw,Result& result)
        :normal(frame),chord_rows(chords),local_rows(local),lo(low),hi(high),d(offsets),out(result),
         n(offsets.size()),bandwidth(bw),b(n,0),rhs(n),clamped(n,0){}
    double ResidualSquared(const Row& row,const std::vector<double>& offsets) const {
        const double k=row.Evaluate(offsets,normal);
        return k*k+curvature_floor2;

    }
    double Objective(const std::vector<double>& offsets) const {
        double value=0;
        for(const auto& row:chord_rows){const double k2=ResidualSquared(row,offsets);value+=row.mass*k2*k2;}
        for(const auto& row:local_rows){const double k2=ResidualSquared(row,offsets);value+=row.mass*k2*k2;}
        return value;

    }
    void Assemble() {
        h.Reset(n,bandwidth);std::fill(b.begin(),b.end(),0.0);
        double scale=0;
        auto add=[&](const detail::Row& row) {
            double jacobian[5]{};const double k=row.Evaluate(d,normal,jacobian);
            const double gradient=row.mass*(k*k+curvature_floor2)*k;
            const double weight=row.mass*(3*k*k+curvature_floor2);
            // Gradient/4 and positive Gauss-Newton Hessian/4.
            for(int p=0;p<row.count;++p) {
                const auto i=row.node[p];const double ai=jacobian[p];
                b[i]-=gradient*ai;
                for(int q2=0;q2<row.count;++q2) {
                    const auto j=row.node[q2];if(j<i)continue;
                    h.at(i,j-i)+=weight*ai*jacobian[q2];
                }
            }
        };
        for(const auto& row:chord_rows)add(row);
        for(const auto& row:local_rows)add(row);
        for(std::size_t i=0;i<n;++i)scale=std::max(scale,h.at(i,0));
        const double eps=std::max(1e-12,scale*1e-5);
        for(std::size_t i=0;i<n;++i)h.at(i,0)+=eps;
        // Solve for absolute offsets: H*x = H*d - grad(F)/4. Including
        // the ridge in H*d makes it a proximal step, not a pull to zero.
        for(std::size_t i=0;i<n;++i) {
            b[i]+=h.at(i,0)*d[i];
            for(std::size_t k=1;k<=bandwidth;++k) {
                if(i+k<n)b[i]+=h.at(i,k)*d[i+k];
                if(i>=k)b[i]+=h.at(i-k,k)*d[i-k];
            }
        }

    }
    // The same bounded active-set solve; scratch storage belongs to one build.
    bool SolveActive(int solve_limit) {
        detail::ScopedMs timing(out.linear_ms);
        auto value=[&](std::size_t i)->double{return clamped[i]==2?0:(clamped[i]>0?hi[i]:lo[i]);};
        for(int it=0;it<solve_limit;++it) {
            ++out.iterations;
            reduced=h;rhs=b;
            for(std::size_t i=0;i<n;++i)if(clamped[i]) {
                const double v=value(i);
                for(std::size_t k=1;k<=bandwidth;++k) {
                    if(i+k<n && !clamped[i+k])rhs[i+k]-=h.at(i,k)*v;
                    if(i>=k && !clamped[i-k])rhs[i-k]-=h.at(i-k,k)*v;
                }
            }
            for(std::size_t i=0;i<n;++i)if(clamped[i]) {
                reduced.at(i,0)=1;rhs[i]=value(i);
                for(std::size_t k=1;k<=bandwidth;++k){if(i+k<n)reduced.at(i,k)=0;if(i>=k)reduced.at(i-k,k)=0;}
            }
            if(!detail::Solve(reduced,lower,diag,rhs))return false;
            d=rhs;
            bool changed=false;
            for(std::size_t i=1;i+1<n;++i)if(!clamped[i]) {
                if(d[i]>hi[i]+1e-6){clamped[i]=1;changed=true;}
                else if(d[i]<lo[i]-1e-6){clamped[i]=-1;changed=true;}
            }
            if(!changed)for(std::size_t i=1;i+1<n;++i)if(clamped[i]==1 || clamped[i]==-1) {
                double grad=h.at(i,0)*d[i]-b[i];
                for(std::size_t k=1;k<=bandwidth;++k){if(i+k<n)grad+=h.at(i,k)*d[i+k];if(i>=k)grad+=h.at(i-k,k)*d[i-k];}
                if((clamped[i]==1 && grad>1e-12) || (clamped[i]==-1 && grad<-1e-12)){clamped[i]=0;changed=true;}
            }
            if(!changed)return true;
        }
        return true; // Cap reached: the clamp below and the verification keep the result legal.

    }
};
} // namespace horse_races::racing_line::detail
