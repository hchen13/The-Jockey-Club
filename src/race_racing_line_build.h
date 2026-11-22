#pragma once
#include "race_racing_line_k1999.h"
#include "race_racing_line_quartic.h"
#include "race_racing_line_verification.h"
#include "race_racing_line_fallback.h"
#include "race_racing_line_seam.h"

namespace horse_races::racing_line {
// Build the line for `centreline` inside a corridor of params.half_width.
// Returns false for unusable input or unresolved known negative evidence.
// Missing certificates retain only the exact recording. A non-recorded chord
// requires positive coverage; known obstacles remain independent vetoes.
template<class Optimizer=detail::K1999Optimizer>
inline bool Build(const course::Path& centreline,const Params& requested,Result& out,Optimizer optimizer={}) {
    using clock=std::chrono::steady_clock;
    const auto started=clock::now();
    out={};out.optimizer=optimizer.Name();
    auto params=requested;
    const std::size_t n=centreline.points.size();
    if(!params.Valid() || n<2 || centreline.metres.size()!=n || !(centreline.Length()>0))return false;
    if(params.fixed_offsets&&params.fixed_offsets->size()!=n)return false;
    if(params.fallback_offsets&&params.fallback_offsets->size()!=n)return false;
    const auto fixed=[&](std::size_t i){return params.fixed_offsets&&std::isfinite((*params.fixed_offsets)[i]);};
    int pinned=0;for(std::size_t i=0;i<n;++i)if(fixed(i))++pinned;
    for(const auto& p:centreline.points)if(!destination::Finite(p))return false;
    out.reference=centreline.metres;
    out.offsets.assign(n,0.0F);
    detail::Measure(centreline,params.dynamics,out.peak_curvature_before,out.mean_curvature_before,out.nominal_seconds_before);
    out.length_before=centreline.Length();
    auto identity=[&](bool fallback=false){
        for(std::size_t i=0;i<n;++i)if(fixed(i)&&(*params.fixed_offsets)[i]!=0)return false;
        if(!detail::RecordedNotBlocked(centreline,params)){out.verified=false;return false;}
        std::fill(out.offsets.begin(),out.offsets.end(),0.0F);out.max_offset=0;out.recorded_fallback=fallback;
        out.obstacle_verified=false;out.recorded_segments=params.obstacles?static_cast<int>(n-1):0;
        out.recorded_chords.assign(n-1,params.obstacles?1:0);
        out.path=centreline;out.pinned_nodes=pinned;out.peak_curvature_after=out.peak_curvature_before;out.mean_curvature_after=out.mean_curvature_before;
        out.nominal_seconds_after=out.nominal_seconds_before;out.length_after=out.length_before;out.verified=true;
        out.milliseconds=std::chrono::duration<double,std::milli>(clock::now()-started).count();return true;
    };
    const float usable=params.Usable(),aggression=params.Aggression();
    if(n<3 || !(usable>0) || !(aggression>0))return identity();
    // This copy exists only for this solve. The caller retains the actual
    // survey, so estimates cannot be saved, learned from, or counted as scans.
    obstacle_gap_view::View estimated;
    if(params.obstacles && params.obstacle_path_starts) {
        estimated=obstacle_gap_view::Build(*params.obstacles,centreline,*params.obstacle_path_starts);
        params.obstacles=&estimated.map;
        out.estimated_rows=estimated.row_count;out.estimated_links=estimated.link_count;
    }
    // The judge rejects a distance strictly beyond the width, and a node put
    // exactly on the wall can read a few micrometres past it once cast to
    // float; the box stops 2 cm short and verification allows 1 cm of that.
    const float box=std::max(0.0F,usable-.02F),limit=usable-.01F;

    // 1. Frame per node (detail::Frame): left normal from a +-4 m chord so a
    //    0.35 m recording hiccup does not swing the normal, and the signed
    //    bend direction.
    std::vector<detail::Vec2> normal;
    detail::Frame(centreline,normal);
    std::vector<float> signed_k(n,0.0F);
    for(std::size_t i=0;i<n;++i)signed_k[i]=detail::SignedChordCurvature(centreline,i);
    // 2. Bounds: the usable corridor both ways, and on the inside of a bend
    //    no further than fold_fraction of the local radius, so the along-rate
    //    (1 - offset*curvature) stays positive and progress never reverses.
    //    Two more, both about the polyline rather than the road: the offset
    //    tapers to the pinned endpoints (half the distance to them), and a
    //    node whose adjacent segment is short and skewed to its normal - a
    //    0.35 m recording hiccup, a corner two nodes before the finish - may
    //    only move so far that it still projects inside that segment, which
    //    is what keeps the nodes' centreline order strictly increasing.
    std::vector<double> lo(n,0),hi(n,0),repair_lo(n,0),repair_hi(n,0);
    for(std::size_t i=1;i+1<n;++i) {
        double outer=box;
        for(std::size_t j:{i-1,i}) {
            const auto& a=centreline.points[j];const auto& b=centreline.points[j+1];
            const double h=std::max(.05F,centreline.metres[j+1]-centreline.metres[j]);
            const double skew=std::abs(normal[i].x*(b.x-a.x)+normal[i].y*(b.y-a.y))/h;
            if(skew>.05)outer=std::min(outer,.9*h/skew);
        }
        const double geometric=outer;
        outer=std::min(outer,.5*std::min(centreline.metres[i]-centreline.metres.front(),centreline.metres.back()-centreline.metres[i]));
        double inner=outer;
        const float k=std::abs(signed_k[i]);
        if(k>1e-4F)inner=std::min(inner,static_cast<double>(params.fold_fraction)/k);
        if(signed_k[i]>0){hi[i]=inner;lo[i]=-outer;} else if(signed_k[i]<0){hi[i]=outer;lo[i]=-inner;}
        else {hi[i]=outer;lo[i]=-outer;}
        // A cosmetic endpoint taper cannot force a repair into an obstacle.
        // Keep its true width/fold/skew bounds, with both endpoints still 0.
        const double repair_inner=k>1e-4F?std::min(geometric,static_cast<double>(params.fold_fraction)/k):geometric;
        repair_lo[i]=signed_k[i]<0?-repair_inner:-geometric;repair_hi[i]=signed_k[i]>0?repair_inner:geometric;
    }
    for(std::size_t i=0;i<n;++i)if(fixed(i)) {
        const double value=(*params.fixed_offsets)[i];
        if(value<lo[i]||value>hi[i])return false;
        lo[i]=hi[i]=repair_lo[i]=repair_hi[i]=value;
    }
    const float keep=params.ProofClearance();
    const float window=std::max(60.0F,3*usable+30);
    std::vector<detail::SectionBound> obstacle_sections;
    std::vector<detail::SectionBound> measured_rows;
    std::vector<unsigned char> recorded(n-1,0);
    bool unconstrained_map=false;
    out.preparation_ms=std::chrono::duration<double,std::milli>(clock::now()-started).count();
    if(params.obstacles) {
        detail::ScopedMs timing(out.branch_ms);
        // A survey offset is not nearest-road distance. Clip branch islands
        // by the world-space legal corridor before selecting their chain.
        detail::ObstacleSections(centreline,*params.obstacles,keep,limit,
            obstacle_sections,measured_rows,recorded,unconstrained_map,params.obstacle_path_starts?&estimated:nullptr);
    }
    // Only actual observations may certify a changed repair; the preparation
    // view may contain short legacy estimates used solely to seed the solve.
    detail::FallbackRepair fallback(centreline,requested,normal,repair_lo,repair_hi);
    out.recorded_branch_segments=static_cast<int>(std::count(recorded.begin(),recorded.end(),1));
    auto pin_recorded=[&] {
        for(std::size_t i=0;i+1<n;++i)if(recorded[i])for(auto node:{i,i+1}) {
            if(fixed(node)&&(*params.fixed_offsets)[node]!=fallback.retained[node])return false;
            lo[node]=hi[node]=fallback.retained[node];
        }
        return true;
    };
    if(!pin_recorded())return false;
    // 3. The actual circumcircle curvature, 2 cross(u,v)/(|u||v||u+v|).
    // Chord endpoints keep fixed recorded references; candidate coordinates
    // and their analytic derivatives are evaluated anew on every pass. The
    // same formula at adjacent waypoints prevents hiding a kink between
    // the longer chord samples. No tangential-spacing or radius surrogate.
    std::vector<detail::Row> chord_rows,local_rows;chord_rows.reserve(n);local_rows.reserve(n);
    std::size_t bandwidth=1;
    auto blend=[&](float along,detail::Row& row,double du,double dv) {
        along=std::clamp(along,0.0F,centreline.Length());
        const std::size_t j=centreline.Segment(along);
        const float span=centreline.metres[j+1]-centreline.metres[j];
        const double t=span>0?std::clamp((along-centreline.metres[j])/span,0.0F,1.0F):0.0;
        row.Add(j,du*(1-t),dv*(1-t));row.Add(j+1,du*t,dv*t);
        const auto& p=centreline.points[j];const auto& q=centreline.points[j+1];
        const double x=p.x+(q.x-p.x)*t,y=p.y+(q.y-p.y)*t;
        row.u.x+=du*x;row.u.y+=du*y;row.v.x+=dv*x;row.v.y+=dv*y;
    };
    for(std::size_t r=1;r+1<n;++r) {
        const double h1=std::max(.05F,centreline.metres[r]-centreline.metres[r-1]);
        const double h2=std::max(.05F,centreline.metres[r+1]-centreline.metres[r]);
        const double mass=(h1+h2)/2;
        {
            detail::Row row;
            blend(centreline.metres[r]-kChordMetres,row,-1,0);
            row.Add(r,1,-1);row.u.x+=centreline.points[r].x;row.u.y+=centreline.points[r].y;
            row.v.x-=centreline.points[r].x;row.v.y-=centreline.points[r].y;
            blend(centreline.metres[r]+kChordMetres,row,0,1);
            row.mass=mass;
            for(int p=0;p<row.count;++p)for(int q=0;q<row.count;++q)
                bandwidth=std::max(bandwidth,row.node[p]>row.node[q]?row.node[p]-row.node[q]:row.node[q]-row.node[p]);
            chord_rows.push_back(row);
        }
        if(params.local_weight>0) {
            detail::Row row;
            row.Add(r-1,-1,0);row.Add(r,1,-1);row.Add(r+1,0,1);
            const auto& a=centreline.points[r-1];const auto& b=centreline.points[r];const auto& c=centreline.points[r+1];
            row.u={b.x-a.x,b.y-a.y};row.v={c.x-b.x,c.y-b.y};
            row.mass=mass*params.local_weight;
            local_rows.push_back(row);
        }
    }
    out.bandwidth=static_cast<int>(bandwidth);

    // 4. Bounded Gauss-Newton on this single true-curvature L4 objective.
    // The positive Hessian approximation and a small ridge stay banded;
    // line search accepts only a decrease of the actual nonlinear score.
    auto d=unconstrained_map?std::vector<double>(n,0):detail::BranchSeed(centreline,normal,measured_rows);
    for(std::size_t i=0;i+1<n;++i)if(recorded[i])for(auto node:{i,i+1})d[node]=fallback.retained[node];
    for(std::size_t i=0;i<n;++i)d[i]=std::clamp(d[i],lo[i],hi[i]);
    std::vector<Vector3> q(n);
    detail::QuarticSystem quartic(normal,chord_rows,local_rows,lo,hi,d,bandwidth,out);
    auto& h=quartic.h;
    auto& b=quartic.b;
    auto& clamped=quartic.clamped;
    std::vector<detail::Constraint> constraints;
    constraints.reserve(n+obstacle_sections.size());
    // A changed node can affect only the nearby candidate chords used by
    // Cross. Cache the reverse incidence once for local fallback closure.
    std::vector<std::vector<std::size_t>> sections_at_node(n);
    for(std::size_t k=0;k<obstacle_sections.size();++k) {
        const auto source=obstacle_sections[k].source;
        const auto first=source>2?source-2:0,last=std::min(n-1,source+3);
        for(std::size_t i=first;i<=last;++i)sections_at_node[i].push_back(k);
    }
    auto make_constraints=[&] {
        detail::ScopedMs timing(out.constraints_ms);
        constraints.clear();
        for(std::size_t i=0;i<n;++i)constraints.push_back({i,i,1,0,lo[i],hi[i]});
        for(const auto& section:obstacle_sections) {
            if(section.recorded)continue;
            const auto cut=detail::Cross(centreline,normal,d,section,window);
            if(!cut.valid||recorded[cut.segment])continue;
            const double origin=cut.a*d[cut.segment]+cut.b*d[cut.segment+1]-cut.lateral;
            const double inset=std::min(.003,std::max(0.0,(section.hi-section.lo)*.1));
            constraints.push_back({cut.segment,cut.segment+1,cut.a,cut.b,
                section.lo+inset+origin,section.hi-inset+origin});
        }
        out.obstacle_constraints=static_cast<int>(constraints.size()-n);
    };
    // Feasibility projection is bounded separately from the objective solve.
    // Only the two endpoints of the chord that actually crosses a section
    // are affected. A projection cannot certify an empty or unknown span.
    auto project_constraints=[&](int sweeps) {
        detail::ScopedMs timing(out.projection_ms);
        for(int sweep=0;sweep<sweeps;++sweep) {
            ++out.constraint_sweeps;double worst=0;
            for(const auto& c:constraints) {
                const double value=c.Value(d),error=std::clamp(value,c.lo,c.hi)-value;
                worst=std::max(worst,std::abs(error));if(error==0)continue;
                const double norm=c.a*c.a+c.b*c.b;
                if(norm>1e-14){d[c.i]+=c.a*error/norm;d[c.j]+=c.b*error/norm;}
            }
            for(std::size_t i=0;i<n;++i)d[i]=std::clamp(d[i],lo[i],hi[i]);
            if(worst<1e-6)break;
        }
    };
    auto obstacle_feasible=[&](const std::vector<double>& offsets,std::vector<unsigned char>* bad=nullptr) {
        detail::ScopedMs timing(out.constraints_ms);
        bool valid=true;
        for(const auto& section:obstacle_sections) {
            if(section.recorded)continue;
            const auto cut=detail::Cross(centreline,normal,offsets,section,window,false);
            const auto segment=cut.valid?cut.segment:centreline.Segment(section.along);
            if(recorded[segment])continue;
            if(!cut.valid||!std::isfinite(cut.lateral)||cut.lateral<section.lo-.002||cut.lateral>section.hi+.002) {
                valid=false;if(bad)(*bad)[segment]=1;
            }
        }
        return valid;
    };
    auto restore_recorded=[&](const std::vector<unsigned char>& bad) {
        detail::ScopedMs timing(out.fallback_ms);
        std::vector<std::size_t> pending;
        for(std::size_t i=0;i+1<n;++i)if(bad[i])pending.push_back(i);
        for(std::size_t next=0;next<pending.size();++next) {
            const auto segment=pending[next];if(recorded[segment])continue;
            recorded[segment]=1;
            for(std::size_t node:{segment,segment+1}) {
                const double target=fallback.retained[node];
                const bool changed=d[node]!=target;lo[node]=hi[node]=d[node]=target;
                if(!changed)continue;
                for(auto k:sections_at_node[node]) {
                    const auto& section=obstacle_sections[k];if(section.recorded)continue;
                    const auto cut=detail::Cross(centreline,normal,d,section,window,false);
                    const auto affected=cut.valid?cut.segment:centreline.Segment(section.along);
                    if(recorded[affected])continue;
                    if(!cut.valid||cut.lateral<section.lo-.002||cut.lateral>section.hi+.002)pending.push_back(affected);
                }
            }
        }
        return pin_recorded();
    };
    auto project_actual=[&](int rounds) {
        for(int round=0;round<rounds;++round) {
            make_constraints();project_constraints(10);
            if(obstacle_feasible(d))return true;
        }
        return false;
    };
    // Keep the box solution when physical constraints are inactive. For an
    // active section, bounded symmetric coordinate sweeps solve the same
    // quadratic within its linearized feasible intervals. Each constraint
    // touches at most two adjacent nodes; no dense matrix or growing search.
    auto solve_sections=[&]()->bool {
        if(unconstrained_map) {
            std::fill(clamped.begin(),clamped.end(),0);clamped[0]=clamped[n-1]=2;
            return quartic.SolveActive(params.iterations);
        }
        const int first_iteration=out.iterations;
        std::fill(clamped.begin(),clamped.end(),0);clamped[0]=clamped[n-1]=2;
        // Section constraints frequently exist without being active. Solve
        // the corridor box first and check that candidate against every
        // actual section. An inactive constraint must not change the line
        // merely because the road was supplied as measured geometry.
        if(!quartic.SolveActive(std::max(1,params.iterations/2)))return false;
        for(std::size_t i=0;i<n;++i)d[i]=std::clamp(d[i],lo[i],hi[i]);
        if(obstacle_feasible(d))return true;
        const int remaining=std::max(0,params.iterations-(out.iterations-first_iteration));
        make_constraints();project_constraints(10);
        struct Limit {std::size_t other;double ratio,low,high;};
        std::vector<std::vector<Limit>> incidence(n);
        for(const auto& c:constraints) {
            auto add=[&](std::size_t i,std::size_t j,double coefficient,double other) {
                if(std::abs(coefficient)<1e-14)return;
                double low=c.lo/coefficient,high=c.hi/coefficient;
                if(low>high)std::swap(low,high);
                incidence[i].push_back({j,other/coefficient,low,high});
            };
            if(c.i==c.j)add(c.i,c.i,c.a+c.b,0);
            else {add(c.i,c.j,c.a,c.b);add(c.j,c.i,c.b,c.a);}
        }
        const auto linear_started=clock::now();
        auto update=[&](std::size_t i) {
            if(lo[i]==hi[i]){d[i]=lo[i];return;}
            double low=lo[i],high=hi[i];
            for(const auto& c:incidence[i]) {
                const double shift=c.ratio*d[c.other];
                low=std::max(low,c.low-shift);high=std::min(high,c.high-shift);
            }
            // An inconsistent local linearization cannot be widened into
            // a certificate. Leave this coordinate for the actual-section
            // projection and nonlinear feasibility check after the solve.
            if(low>high)return;
            double off=0;
            for(std::size_t k=1;k<=bandwidth;++k) {
                if(i+k<n)off+=h.at(i,k)*d[i+k];
                if(i>=k)off+=h.at(i-k,k)*d[i-k];
            }
            d[i]=std::clamp((b[i]-off)/h.at(i,0),low,high);
        };
        for(int it=0;it<remaining;++it) {
            ++out.iterations;
            for(std::size_t i=0;i<n;++i)update(i);
            for(std::size_t i=n;i-->0;)update(i);
        }
        out.linear_ms+=std::chrono::duration<double,std::milli>(clock::now()-linear_started).count();
        return std::all_of(d.begin(),d.end(),[](double value){return std::isfinite(value);});
    };
    // 6. Guards and corridor verification, as hard post-conditions rather
    //    than estimates: build the polyline, project every node back onto
    //    the centreline within a local window and require the distance
    //    inside the usable width and the order strictly increasing; check
    //    every 1 m chord sample against its own centreline segment, whose
    //    distance is an upper bound of the judge's (a nearer segment
    //    anywhere only lowers it). Offenders and their neighbours, whose
    //    normals shaped that chord, are pulled half way back before
    //    checking again. Leaves the verified points in q.
    detail::ChordVerifier chords(centreline,params,measured_rows,recorded,usable,limit);
    auto verify=[&](std::vector<float>& offsets)->bool{
        detail::ScopedMs timing(out.verify_ms,&out.constraints_ms);
        for(int round=0;round<12;++round) {
            ++out.verification_rounds;
            for(std::size_t i=0;i<n;++i) {
                if(lo[i]==hi[i])offsets[i]=static_cast<float>(lo[i]);
                const auto& p=centreline.points[i];
                q[i]={static_cast<float>(p.x+offsets[i]*normal[i].x),static_cast<float>(p.y+offsets[i]*normal[i].y),p.z};
            }
            std::vector<char> bad(n,0),missing(n-1,0);bool ok=true;
            float previous=-1;
            for(std::size_t i=0;i<n;++i) {
                const auto node=detail::ProjectNear(centreline,q[i],centreline.metres[i],window);
                if(!node.valid || node.distance>limit || !(node.along>previous)){bad[i]=1;ok=false;}
                if(node.valid)previous=node.along;
                if(i) {
                    const float len=destination::Distance(q[i-1],q[i]);
                    if(len<.05F || len>250.0F){bad[i-1]=bad[i]=1;ok=false;}
                    const auto& a=centreline.points[i-1];const auto& c=centreline.points[i];
                    if((q[i].x-q[i-1].x)*(c.x-a.x)+(q[i].y-q[i-1].y)*(c.y-a.y)<=.01F){bad[i-1]=bad[i]=1;ok=false;}
                    if(!chords.Measured(q[i-1],q[i],i-1)) {
                        if((offsets[i-1]==0&&offsets[i]==0)||!fallback.PositiveAllows(q[i-1],q[i],i-1))missing[i-1]=1;
                    } else if(estimated.Estimated()&&!recorded[i-1]&&!fallback.PositiveAllows(q[i-1],q[i],i-1))missing[i-1]=1;
                    const int samples=static_cast<int>(std::ceil(len/std::min(1.0F,params.verify_step)));
                    for(int s2=1;s2<samples;++s2) {
                        const float t=static_cast<float>(s2)/samples;
                        const Vector3 m{q[i-1].x+(q[i].x-q[i-1].x)*t,q[i-1].y+(q[i].y-q[i-1].y)*t,q[i-1].z+(q[i].z-q[i-1].z)*t};
                        const bool corridor_bad=chords.Beyond(m,i-1);
                        if(corridor_bad) {
                            bad[i-1]=bad[i]=1;ok=false;break;
                        }
                    }
                }
            }
            // An absent certificate restores this chord's exact recorded
            // endpoints. Keeping a merely unblocked displaced candidate would
            // authorize shortcuts over unknown ground (including a bridge).
            for(std::size_t i=0;i+1<n;++i)if(missing[i]) {
                recorded[i]=1;
                bad[i]=bad[i+1]=1;ok=false;
            }
            if(!pin_recorded())return false;
            if(params.obstacles) {
                std::vector<double> actual(offsets.begin(),offsets.end());
                std::vector<unsigned char> failed(n-1,0);
                if(!obstacle_feasible(actual,&failed)) {
                    ok=false;
                    for(std::size_t i=0;i+1<n;++i)if(failed[i]) {
                        // Lose positive certification locally. A conflicting
                        // negative still requires the bounded chord repair;
                        // this mask never certifies an unresolved connection.
                        recorded[i]=1;bad[i]=bad[i+1]=1;
                    }
                    if(!pin_recorded())return false;
                }
            }
            if(ok)return true;
            // Offenders pulled half way back, and their neighbours within the
            // window with them, by a raised cosine of the distance: a repair
            // is a gentle ease, not a dent at the node spacing.
            std::vector<float> pull(n,0.0F);
            for(std::size_t i=0;i<n;++i)if(bad[i]) {
                for(std::size_t j2=i;j2<n && centreline.metres[j2]-centreline.metres[i]<=kBoxWindowMetres;++j2)pull[j2]=std::max(pull[j2],.5F*(1+std::cos(3.14159265F*(centreline.metres[j2]-centreline.metres[i])/kBoxWindowMetres)));
                for(std::size_t j2=i;j2-->0 && centreline.metres[i]-centreline.metres[j2]<=kBoxWindowMetres;)pull[j2]=std::max(pull[j2],.5F*(1+std::cos(3.14159265F*(centreline.metres[i]-centreline.metres[j2])/kBoxWindowMetres)));
            }
            for(std::size_t i=1;i+1<n;++i)offsets[i]=lo[i]==hi[i]?static_cast<float>(lo[i]):
                static_cast<float>(offsets[i]+.5F*pull[i]*(fallback.retained[i]-offsets[i]));
            d.assign(offsets.begin(),offsets.end());
            if(!fallback.Repair(d,recorded,lo,hi))return false;
            for(std::size_t i=0;i<n;++i)offsets[i]=static_cast<float>(d[i]);
        }
        return false;
    };
    // 7. Bounded Newton steps. Accept only a finite, decreasing value of
    // the unchanged L4 objective. Both ends of the backtracked segment
    // are in the same box, so interpolation keeps its one-sided constraints.
    if(params.obstacles) {
        if(!fallback.Repair(d,recorded,lo,hi))return identity(true);
        for(int attempt=0;attempt<6;++attempt) {
            if(project_actual(2))break;
            std::vector<unsigned char> failed(n-1,0);obstacle_feasible(d,&failed);if(!restore_recorded(failed))return identity(true);
            if(!fallback.Repair(d,recorded,lo,hi))return identity(true);
        }
    }
    out.recorded_projection_segments=static_cast<int>(std::count(recorded.begin(),recorded.end(),1));
    const auto feasible_baseline=d;
    out.objective_before=out.objective_after=quartic.Objective(d);
    if(!std::isfinite(out.objective_before))return identity(true);
    bool quartic_steps=true;
    if constexpr(!std::is_same_v<Optimizer,detail::QuarticOptimizer>) {
        quartic_steps=optimizer(centreline,params,normal,lo,hi,d,constraints,make_constraints,project_actual,out.curvature_sweeps);
        out.objective_after=quartic.Objective(d);
        if(!std::isfinite(out.objective_after))return identity(true);
    }
    for(int pass=0;quartic_steps&&pass<params.passes;++pass) {
        const auto previous=d;
        const double previous_score=out.objective_after;
        quartic.Assemble();
        std::fill(clamped.begin(),clamped.end(),0);clamped[0]=clamped[n-1]=2;
        if(!(params.obstacles?solve_sections():quartic.SolveActive(params.iterations))){d=previous;break;}
        for(std::size_t i=0;i<n;++i)d[i]=std::clamp(d[i],lo[i],hi[i]);
        // An open physical map must leave the original Newton direction
        // intact. Projecting its large trial step before backtracking can
        // alter even a smaller, already feasible step. Every accepted step
        // and final chord still passes the same physical checks below.
        if(params.obstacles&&!unconstrained_map)project_actual(1);
        const auto solved=d;
        bool accepted=false;double alpha=1;
        for(int step=0;step<12;++step,alpha*=.5) {
            for(std::size_t i=0;i<n;++i)d[i]=previous[i]+alpha*(solved[i]-previous[i]);
            const double score=quartic.Objective(d);
            if(std::isfinite(score) && score<=previous_score && (!params.obstacles||obstacle_feasible(d))) {
                out.objective_after=score;++out.accepted_passes;accepted=true;break;
            }
        }
        if(!accepted){d=previous;break;}
    }
    std::vector<float> best(n);
    for(std::size_t i=0;i<n;++i)best[i]=static_cast<float>(d[i]);
    if(!verify(best))return identity(true);
    // A nonzero skill blend stays on the chosen measured branch. Blending an
    // outer bypass straight towards zero could pass through the obstacle.
    // Exactly zero still requests the recorded identity above.
    for(std::size_t i=0;i<n;++i)out.offsets[i]=static_cast<float>(
        aggression*best[i]+(1-aggression)*feasible_baseline[i]);
    out.verified=aggression==1 || verify(out.offsets);
    if(!out.verified)return identity(true);
    // Comparison optimizers retain their own solve; only K1999 gets this phase.
    if constexpr(std::is_same_v<Optimizer,detail::K1999Optimizer>) {
      if(params.obstacles&&params.obstacles->native_mode) {
        detail::RecordedSeamChoice seam(centreline,params,normal,measured_rows,recorded,q,out.offsets);
        const bool selected=seam.Select();
        if(selected) {
            const auto free=seam.FineFree(),certified=recorded;
            const auto saved_lo=lo,saved_hi=hi;
            d.assign(out.offsets.begin(),out.offsets.end());
            for(std::size_t i=0;i<n;++i)if(!free[i])lo[i]=hi[i]=d[i];
            detail::K1999Optimizer{}(centreline,params,normal,lo,hi,d,constraints,
                make_constraints,project_actual,out.seam_curvature_sweeps,true);
            recorded=certified;
            lo=saved_lo;hi=saved_hi;
        }
        seam.Refine(selected?&d:nullptr);
        out.refinement_candidates=static_cast<int>(seam.RefinementAttempts());
        out.refinement_nodes=static_cast<int>(seam.RefinementWork());
        out.reference_refinements=static_cast<int>(seam.LocalAccepted());
      }
    }
    out.objective_final=quartic.Objective(std::vector<double>(out.offsets.begin(),out.offsets.end()));
    if(!out.path.Build(q))return identity(true);
    out.recorded_chords=recorded;
    for(std::size_t i=0;i+1<n;++i)if(recorded[i]&&(out.offsets[i]!=0||out.offsets[i+1]!=0)) {
        if(!fallback.PositiveAllows(q[i],q[i+1],i))return identity(true);
        out.recorded_chords[i]=0; // Independently measured repair, never an unknown fallback.
    }
    out.recorded_segments=static_cast<int>(std::count(out.recorded_chords.begin(),out.recorded_chords.end(),1));out.pinned_nodes=pinned;
    out.obstacle_verified=params.obstacles && !out.recorded_segments && !out.estimated_rows && !out.estimated_links;
    for(float o:out.offsets)out.max_offset=std::max(out.max_offset,std::abs(o));
    detail::Measure(out.path,params.dynamics,out.peak_curvature_after,out.mean_curvature_after,out.nominal_seconds_after);
    out.length_after=out.path.Length();
    out.milliseconds=std::chrono::duration<double,std::milli>(clock::now()-started).count();
    return true;
}
} // namespace horse_races::racing_line
