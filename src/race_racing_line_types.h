#pragma once
#include "race_obstacle_map.h"
#include "race_obstacle_gap_view.h"
#include "race_pace_plan.h"
#include "race_tracking_margin.h"
#include <chrono>
#include <cstddef>
#include <vector>
#include <type_traits>

// The racing line INSIDE a legal corridor, computed at runtime from the
// recorded centreline and the corridor half-width in force at that moment.
//
// Pure geometry: no natives, no globals, no clock other
// than a diagnostic duration. The output keeps the centreline's node count
// and endpoints, so node i of the line IS node i of the centreline moved
// sideways: the metres mapping the native follower needs is `reference`
// (== centreline.metres) and the maintainer can register `path` as a
// waypoint recording unchanged.
//
// K1999-style multiscale curvature interpolation spreads turning over the
// available approach at strides 8/4/2/1, at most 100 sweeps each. Physical
// constraints refresh every ten fine sweeps. Horse dynamics and predicted
// traversal time never select the geometry. The former quartic objective is
// retained for diagnostics and explicit offline comparisons only.
// The configured corridor bounds every node. Obstacles constrain
// the chord where it crosses their own measured section; opposite edges at
// different metres never become one fictitious narrow cross section. A
// continuous branch through measured interval unions is chosen once, before
// the bounded banded solve. Unmeasured pieces may retain the recording;
// known negative conflicts require a positively measured bypass. Missing
// coverage permits the exact recording only, with collision vetoes intact.
//
// Nonzero aggression blends towards a section-checked feasible baseline.
// Chords and order are checked again after blending. Exactly 0 explicitly
// requests the recorded line.
namespace horse_races::racing_line {
inline constexpr float kTrackingMarginMetres=tracking_margin::kDefaultMetres; // Default only; runtime snapshots the configured value.
inline constexpr float kChordMetres=8; // race_pace::Plan's curvature stencil.
// The corridor-only repair eases its correction over this physical window.
// This is not an obstacle bound and does not move a measured edge upstream.
inline constexpr float kBoxWindowMetres=12;
struct Params {
    float half_width=14;        // circuit::Layout::strict_half_width in force (metres).
    float margin=kTrackingMarginMetres; // Course/known-obstacle reserve; historical replay may pass its recorded value.
    float aggression=1.0F;      // 0 = centreline exactly, 1 = full usable width. Clamped to [0,1].
    bool closed=false;          // Documented only: a closed loop is solved with its seam pinned (the grid stands there).
    race_pace::Dynamics dynamics; // Diagnostics only. Geometry is independent of the pace model.
    int iterations=20;          // Linear-solve cap per Gauss-Newton pass.
    int passes=8;               // Same 160-solve total budget; relinearise actual curvature more often.
    float fold_fraction=.7F;    // Inside offset <= fold_fraction / curvature: progress never reverses.
    float local_weight=3.0F;    // Give native waypoint smoothness priority alongside the 8 m chord residual.
    float verify_step=1.0F;     // Metres between corridor samples along each output chord.
    // Actual measured section unions and their swept connections. Legacy
    // side widths remain a conservative compatibility input. Absent: empty.
    const obstacle_map::Map* obstacles=nullptr;
    // Explicit recorded-path starts enable short, local gap estimates. No
    // wrapping or inference between independently recorded laps is allowed.
    const std::vector<float>* obstacle_path_starts=nullptr;
    // Optional authored offsets on this exact node grid. NaN leaves a node
    // free; finite entries are hard constraints, including during fallback.
    const std::vector<float>* fixed_offsets=nullptr;
    // Legacy authored approach hints, validated for source compatibility.
    // Missing coverage now always uses zero, so these never grant clearance.
    const std::vector<float>* fallback_offsets=nullptr;
    float obstacle_clearance=.5F; // Physical half-horse width, applied once to raw obstacle space.
    bool Valid() const {
        if(!(std::isfinite(half_width) && half_width>=0 && half_width<=100 && std::isfinite(margin) && margin>=0 &&
            std::isfinite(aggression) && dynamics.Valid() && iterations>=1 && iterations<=400 && passes>=1 && passes<=8 &&
            std::isfinite(fold_fraction) && fold_fraction>0 && fold_fraction<1 && std::isfinite(obstacle_clearance) && obstacle_clearance>=0 &&
            std::isfinite(local_weight) && local_weight>=0 &&
            std::isfinite(verify_step) && verify_step>=.25F))return false;
        if(fixed_offsets)for(float value:*fixed_offsets)if(std::isinf(value))return false;
        if(fallback_offsets)for(float value:*fallback_offsets)if(std::isinf(value))return false;
        return true;
    }
    // Positive measurements prove body clearance. Their boundary may border
    // unknown space, so it does not acquire the tracking reserve of an actual
    // obstacle. Independent negative checks retain that complete reserve.
    float ProofClearance() const {return obstacle_clearance;}
    float ObstacleReserve() const {return margin+obstacle_clearance;}
    float Usable() const {return std::max(0.0F,half_width-margin);}
    float Aggression() const {return std::clamp(aggression,0.0F,1.0F);}
};
struct Result {
    std::string optimizer="k1999";
    int seam_curvature_sweeps=0; // Optional post-seam fine phase, at most 100.
    int curvature_sweeps=0; // Actual K1999 sweeps, at most 100 at each of four scales.
    int refinement_candidates=0,refinement_nodes=0,reference_refinements=0; // Additional local-reference work only; legacy fine/split keeps its own allowance.
    course::Path path;               // Same node count and endpoints as the centreline. Register this.
    std::vector<float> offsets;      // Signed metres per node, + = left of travel. |offset| <= half_width - margin.
    std::vector<float> reference;    // Centreline metres of node i (== centreline.metres): the follower's order metres.
    // Present only after a local captured reference was published in canonical
    // progress. Solver offsets still belong to its work normals; diagnostics
    // and geometry-free legacy consumers use these canonical lateral values.
    std::vector<float> canonical_offsets;
    bool recorded_reference_applied=false;
    float peak_curvature_before=0, peak_curvature_after=0;   // race_pace::Plan's +-8 m chord estimator.
    float mean_curvature_before=0, mean_curvature_after=0;
    double nominal_seconds_before=0, nominal_seconds_after=0; // Model estimate at params.dynamics, never a lap time.
    double objective_before=0, objective_after=0; // Actual curvature L4 score, before verification repairs.
    double objective_final=0; // Submitted polyline after repairs and utilization blending.
    int accepted_passes=0;
    float length_before=0, length_after=0, max_offset=0;
    int iterations=0, verification_rounds=0, bandwidth=0;
    int pinned_nodes=0;              // Nodes whose offset is a recorded one (race_line_pins.h), not the solver's.
    bool authored_constraints=false; // line_pins::Build accepted all authored offsets; independent of optional terrain overlays.
    bool verified=false;             // Corridor/order and independent known-negative checks passed; not proof of positive coverage.
    bool recorded_fallback=false;    // Solver/verification failed; recorded identity retained only if known-negative checks pass.
    bool obstacle_verified=false;    // Every chord passed measured section/link clearance; false for any recorded-only piece.
    int recorded_segments=0;         // Exact recorded chords retained without positive clearance certificates.
    std::vector<unsigned char> recorded_chords; // Every marked chord has the exact recorded endpoints.
    int recorded_branch_segments=0,recorded_projection_segments=0;
    std::size_t estimated_rows=0,estimated_links=0;
    int obstacle_constraints=0, constraint_sweeps=0;
    double preparation_ms=0,branch_ms=0,constraints_ms=0,projection_ms=0,linear_ms=0,verify_ms=0,fallback_ms=0;
    double milliseconds=0;           // Diagnostic build duration for the driver log; never a deadline.
    // Centreline metres <-> line metres, piecewise linear over the shared nodes.
    float ToLine(float centre_along) const {return Map(reference,path.metres,centre_along);}
    float ToCentre(float line_along) const {return Map(path.metres,reference,line_along);}
    std::size_t Index(float centre_along) const {
        if(reference.size()<2)return 0;
        const auto it=std::upper_bound(reference.begin(),reference.end(),centre_along);
        return static_cast<std::size_t>(std::max<std::ptrdiff_t>(0,it-reference.begin()-1));
    }
private:
    static float Map(const std::vector<float>& from,const std::vector<float>& to,float along) {
        if(from.size()<2 || to.size()!=from.size() || !std::isfinite(along))return 0;
        if(along<=from.front())return to.front();
        if(along>=from.back())return to.back();
        const auto it=std::upper_bound(from.begin(),from.end(),along);
        const std::size_t i=static_cast<std::size_t>(it-from.begin()-1);
        const float span=from[i+1]-from[i];
        const float t=span>0?(along-from[i])/span:0;
        return to[i]+(to[i+1]-to[i])*t;
    }
};

// Keep every surveyed point, adding intermediate nodes only where the
// recording is sparse. Both hosted races and the bookmaker field solve on
// this same geometry; their navigation references remain centreline metres.
inline bool Densify(const course::Path& centre,float step,course::Path& out) {
    out={};
    if(centre.points.size()<2 || !std::isfinite(step) || step<=0)return false;
    std::vector<Vector3> points;
    points.reserve(centre.points.size());
    for(std::size_t i=0;i+1<centre.points.size();++i) {
        const auto& a=centre.points[i];const auto& b=centre.points[i+1];
        points.push_back(a);
        const float length=std::hypot(b.x-a.x,b.y-a.y);
        const int pieces=length>step?static_cast<int>(std::ceil(length/step)):1;
        for(int k=1;k<pieces;++k) {
            const float t=static_cast<float>(k)/static_cast<float>(pieces);
            points.push_back({a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t});
        }
    }
    points.push_back(centre.points.back());
    return out.Build(points);
}

} // namespace horse_races::racing_line
