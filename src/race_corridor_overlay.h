#pragma once
#include "race_circuit.h"
#include "game_natives.h"
#include <algorithm>
#include <cstdint>
#include <vector>

// Debug view of the legal corridor of a judged course, drawn IN THE WORLD with
// GRAPHICS::_DRAW_MARKER: one ground-projected translucent marker per recorded
// node inside a rolling window around the viewer's qualified metres. Pure
// geometry plus marker calls - the caller decides WHEN (F11 toggle, corridor_dq
// rules, HUD/fade/pause state) and WHERE (which frozen plan, whose metres).
//
// Marker type. The SDK has no line/polygon primitive (grep natives.h for
// DRAW_LINE|DRAW_POLY|DRAW_BOX: none), only _DRAW_MARKER(Hash type,...) whose
// type is a marker MODEL hash, not joaat("MARKER_*"). Vanilla draws exactly one
// ground-projected translucent shape: 0x94FDAE17 (-1795314153, prop_mk_cylinder
// in femga's marker_types.lua). Evidence in analysis/RDR2-Decompiled-Scripts/
// script_mp_rel:
//  - fme_king_of_the_castle.c func_830: a capture VOLUME is drawn as that
//    cylinder with scale.x/y = volume scale*2 (DIAMETER), scale.z=1 and
//    pos.z = volume centre - height/2, i.e. the marker origin is its BASE and it
//    grows upward; colour+alpha from _GET_COLOR_FROM_NAME, dir 0, rot = volume
//    rotation, bob=false, faceCamera=false, p19=2, rotate=false, dict/name 0,
//    drawOnEnts=false.
//  - fm_deathmatch_controller.c ~45782: same type at an entity, scale
//    (0.4,0.4,1.25), yellow, alpha ROUND(80*f)/ROUND(60*f) fading.
//  - fm_mission_controller.c ~218401: same type, COLOR_YELLOW, scale
//    (0.6,0.6,1.0), alpha optionally 0..255.
// Every argument here that is not geometry or colour copies that template.
namespace horse_races::corridor_overlay {
inline constexpr Hash kCylinderMarker=0x94FDAE17; // prop_mk_cylinder, base at pos, scale.x/y = diameter.
// Ground probes start this far above the riding position and accept a
// surface this far from it: a line moved sideways onto a canyon wall or a
// cut bank has its ground metres above or below the node, and a probe from
// 2 m up started inside the hill and found nothing.
inline constexpr float kProbeAbove=6,kPlausibleMetres=8;
// The ground is never above the rider. An answer over the riding height is
// an overpass, a lamp arm, a trestle the ask from kProbeAbove up fell on
// (2026-09-19: the host's line climbed onto the railway trestle at Saint
// Denis' western gate and came down after it); the ask is repeated from
// just over the rider, under anything a horse passes beneath.
inline constexpr float kProbeRetryAbove=1,kAboveRiderMetres=.5F;
struct Style {
    int r=255,g=220,b=60,a=170; // COLOR_YELLOW-ish; alpha must stay < 255 so the road shows through.
    float lift_m=.05F;           // Added to the resolved ground z: the marker base sits just above the terrain.
    float behind_m=30,ahead_m=250; // Window around the viewer's metres; clamped to the path (wrapped on a loop).
    float edge_diameter_m=.8F,edge_height_m=1.6F; // Fence posts.
    // Rails join each post to the next on the same side, so the two borders
    // read as lines rather than as dots. A cylinder laid on its side.
    bool rails=true;float rail_diameter_m=.3F,rail_height_m=.8F; // Rails run between the posts at this height.
    int ground_probes_per_frame=8;                // Lazy GET_GROUND_Z_FOR_3D_COORD budget when a GroundCache is bound.
    // GroundLine only: pieces of at most this length, so the line follows
    // the undulations between recorded nodes; the tube's axis sits
    // rail_diameter/2 - sink above the probed terrain (top just showing);
    // pitch_sign flips the slope tilt should the marker's convention differ.
    float ground_step_m=3,ground_sink_m=.05F,pitch_sign=1;
};
// Recorded nodes are riding positions: up to a few metres above the surface
// (Layout tolerates 6 m in height, course::Project forgives 6 m). Probing the
// terrain under each node once and caching it keeps the per-frame cost flat.
// Bind to a layout's node count; Reset when the route is released.
struct GroundCache {
    static constexpr std::uint8_t kAnswered=255;
    std::vector<float> z;
    std::vector<std::uint8_t> probed; // kAnswered once z stands (terrain or fallback); else asks so far.
    int probes=0;                     // Total natives issued; tests and logs.
    // Asks before the riding height stands in. The collision far ahead is
    // not loaded yet, and a node asked once there would be frozen at riding
    // height for the whole race; a line that keeps asking settles on the
    // terrain as the rider approaches. kAnswered means ask until answered.
    std::uint8_t attempts=1;
    void Reset(){z.clear();probed.clear();probes=0;}
    // Allocates only when the node count changes; free per frame afterwards.
    void Bind(std::size_t nodes) {
        if(z.size()==nodes)return;
        z.assign(nodes,0);probed.assign(nodes,0);probes=0;
    }
    std::size_t Pending(std::size_t begin,std::size_t end) const {
        std::size_t count=0;
        for(std::size_t i=begin;i<end && i<probed.size();++i)count+=probed[i]!=kAnswered;
        return count;
    }
    bool Answered(std::size_t i) const {return i<probed.size() && probed[i]==kAnswered;}
    // One ask for node i at riding position p, from just above it so a
    // bridge deck is found before the river under it; only a plausible drop
    // is taken. Returns true when the node is settled by this call.
    bool Probe(std::size_t i,Vector3 p) {
        if(i>=z.size() || probed[i]==kAnswered)return false;
        for(const float above:{kProbeAbove,kProbeRetryAbove}) {
            ++probes;float probe=p.z;
            if(!(MISC::GET_GROUND_Z_FOR_3D_COORD(p.x,p.y,p.z+above,&probe,false) && std::isfinite(probe) &&
                probe>=p.z-kPlausibleMetres))break;   // Nothing plausible below: the fallback, as before.
            if(probe<=p.z+kAboveRiderMetres){z[i]=probe;probed[i]=kAnswered;return true;}
            // Above the rider: an overpass. Asked again from just over the rider.
        }
        if(probed[i]<kAnswered-1)++probed[i];
        if(probed[i]>=attempts){z[i]=p.z;probed[i]=kAnswered;return true;}
        return false;
    }
};
inline bool IsLoop(const circuit::Layout& layout) {
    // Layout::Build only accepts an open line when distinct_finish was
    // requested, and closes every other course to within 0.1 m.
    return !layout.destination_finish && layout.path.points.size()>=2 &&
        destination::Distance(layout.path.points.front(),layout.path.points.back())<=.1F;
}
// Metres of node i from `along` in the drawing sense: negative behind, positive
// ahead. On a loop the lap line is transparent: the last 30 m before it are
// "behind" a rider who just crossed it, the first 250 m after it are "ahead" of
// a rider about to.
inline float Offset(const circuit::Layout& layout,std::size_t i,float along,bool loop) {
    const float d=layout.path.metres[i]-along;
    if(!loop)return d;
    const float length=layout.path.Length();
    float forward=std::fmod(d,length);if(forward<0)forward+=length;
    return forward; // [0,length): callers accept forward<=ahead || forward>=length-behind.
}
inline bool InWindow(float offset,float length,bool loop,const Style& style) {
    if(!loop)return offset>=-style.behind_m && offset<=style.ahead_m;
    return offset<=style.ahead_m || offset>=length-style.behind_m;
}
// Unit XY tangent at node i: central difference inside, one-sided at the open
// ends, wrapped across the seam of a loop.
inline Vector3 Tangent(const circuit::Layout& layout,std::size_t i,bool loop) {
    const auto& p=layout.path.points;const std::size_t n=p.size();
    std::size_t before=i?i-1:(loop?n-2:0),after=i+1<n?i+1:(loop?1:n-1);
    if(before==after)return {1,0,0};
    float tx=p[after].x-p[before].x,ty=p[after].y-p[before].y;
    const float length=std::hypot(tx,ty);
    if(!(length>.001F))return {1,0,0};
    return {tx/length,ty/length,0};
}
inline void Marker(Vector3 pos,Vector3 scale,const Style& style) {
    GRAPHICS::_DRAW_MARKER(kCylinderMarker,pos,{0,0,0},{0,0,0},scale,style.r,style.g,style.b,style.a,
        false,false,2,false,nullptr,nullptr,false);
}
// A rail from a to b: the cylinder's base sits at a and its height runs to b.
// Tipping it by 90 degrees about X points its axis along -Y; the yaw then turns
// that onto the segment, so heading = atan2(dx, -dy). Height differences are
// ignored: a rail follows the ground closely enough at recorded node spacing.
inline void Rail(Vector3 a,Vector3 b,const Style& style) {
    const float dx=b.x-a.x,dy=b.y-a.y,len=std::hypot(dx,dy);
    if(len<.2F)return;
    constexpr float kDegrees=57.2957795F;
    const float heading=std::atan2(dx,-dy)*kDegrees;
    GRAPHICS::_DRAW_MARKER(kCylinderMarker,a,{0,0,0},{90,0,heading},{style.rail_diameter_m,style.rail_diameter_m,len},
        style.r,style.g,style.b,style.a,false,false,2,false,nullptr,nullptr,false);
}
// Draw the corridor of `layout` at `half_width` around `along` (the viewer's
// qualified metres on the expanded path). Returns the number of markers drawn.
// Nothing is drawn for an empty layout, a non-positive/non-finite width or a
// non-finite `along`. Allocation-free: the only container is the optional
// GroundCache, sized once per layout.
// Both sides show the configured outer boundary around the recorded line.
// Surveyed usable passages and planning margins do not move this fence.
inline int Draw(const circuit::Layout& layout,float half_width,float along,const Style& style,GroundCache* ground=nullptr) {
    const auto& path=layout.path;const std::size_t n=path.points.size();
    if(n<2 || path.metres.size()!=n || !std::isfinite(half_width) || half_width<=0 || !std::isfinite(along) ||
        !std::isfinite(style.behind_m) || !std::isfinite(style.ahead_m) || style.behind_m<0 || style.ahead_m<0 ||
        style.a<=0)return 0;
    const bool loop=IsLoop(layout);
    const float length=path.Length();
    if(ground)ground->Bind(n);
    int budget=ground?std::max(0,style.ground_probes_per_frame):0;
    // The closing node of a loop repeats its first node: draw it once.
    const std::size_t last=loop?n-1:n;
    int drawn=0;
    // Rails need the previous node's posts; only a node directly before this one
    // (or the loop seam, closed after the sweep) is joined.
    std::size_t prev_index=static_cast<std::size_t>(-1);
    Vector3 prev_left{},prev_right{},first_left{},first_right{};bool have_first=false;
    std::size_t last_drawn=static_cast<std::size_t>(-1);
    for(std::size_t i=0;i<last;++i) {
        if(!InWindow(Offset(layout,i,along,loop),length,loop,style))continue;
        const Vector3 p=path.points[i];
        float z=p.z;
        if(ground) {
            if(!ground->Answered(i) && budget>0){--budget;ground->Probe(i,p);}
            if(ground->Answered(i))z=ground->z[i];
        }
        z+=style.lift_m;
        const Vector3 t=Tangent(layout,i,loop);
        const Vector3 scale{style.edge_diameter_m,style.edge_diameter_m,style.edge_height_m};
        const Vector3 left{p.x-t.y*half_width,p.y+t.x*half_width,z},right{p.x+t.y*half_width,p.y-t.x*half_width,z};
        Marker(left,scale,style);Marker(right,scale,style);drawn+=2;
        if(style.rails) {
            if(prev_index+1==i) {
                const auto raised=[&](Vector3 v){v.z+=style.rail_height_m;return v;};
                Rail(raised(prev_left),raised(left),style);Rail(raised(prev_right),raised(right),style);drawn+=2;
            }
            if(i==0){first_left=left;first_right=right;have_first=true;}
            prev_index=i;prev_left=left;prev_right=right;last_drawn=i;
        }
    }
    // A loop drawn across its seam joins the last node back to the first.
    if(style.rails && loop && have_first && last_drawn==last-1 && last>1) {
        const auto raised=[&](Vector3 v){v.z+=style.rail_height_m;return v;};
        Rail(raised(prev_left),raised(first_left),style);Rail(raised(prev_right),raised(first_right),style);drawn+=2;
    }
    return drawn;
}
// A rail from a to b in three dimensions: the flat rail's heading, tilted
// by the slope (positive pitch climbs) and as long as the 3D piece.
inline void GroundRail(Vector3 a,Vector3 b,const Style& style) {
    const float dx=b.x-a.x,dy=b.y-a.y,dz=b.z-a.z,flat=std::hypot(dx,dy),len=std::hypot(flat,dz);
    if(flat<.2F)return;
    constexpr float kDegrees=57.2957795F;
    const float heading=std::atan2(dx,-dy)*kDegrees;
    const float pitch=std::atan2(dz,flat)*kDegrees*style.pitch_sign;
    GRAPHICS::_DRAW_MARKER(kCylinderMarker,a,{0,0,0},{90-pitch,0,heading},{style.rail_diameter_m,style.rail_diameter_m,len},
        style.r,style.g,style.b,style.a,false,false,2,false,nullptr,nullptr,false);
}
// A line ON the ground: `path` cut into pieces of at most ground_step_m,
// each end probed for the terrain (lazily, within a per-frame budget,
// nearest first, asked again until the collision ahead has loaded), drawn
// as a tube laid along each piece, pitched with the slope and sunk so only
// its top shows. A piece is drawn once both its ends are on the terrain;
// until then nothing floats. The host's racing line is drawn this way in
// magenta. Build once per line (Matches tells a stale one), Reset with it.
struct GroundLine {
    std::vector<Vector3> nodes;   // Piece ends: XY on the path, z the riding height until probed.
    std::vector<float> metres;    // Along the source path.
    GroundCache ground;
    bool loop=false;
    std::size_t source_nodes=0;float source_length=0;Vector3 source_mid{};
    bool Matches(const course::Path& path) const {
        const std::size_t n=path.points.size();
        return n>=2 && n==source_nodes && path.Length()==source_length &&
            destination::Distance(path.points[n/2],source_mid)==0;
    }
    void Reset(){nodes.clear();metres.clear();ground.Reset();loop=false;source_nodes=0;source_length=0;source_mid={};}
    bool Build(const course::Path& path,float step_m,bool closed) {
        Reset();
        const std::size_t n=path.points.size();
        if(n<2 || path.metres.size()!=n || !std::isfinite(step_m) || !(step_m>0))return false;
        loop=closed;source_nodes=n;source_length=path.Length();source_mid=path.points[n/2];
        for(std::size_t i=0;i+1<n;++i) {
            const auto& a=path.points[i];const auto& b=path.points[i+1];
            const float len=destination::Distance(a,b);
            const int pieces=std::max(1,static_cast<int>(std::ceil(len/step_m)));
            for(int k=0;k<pieces;++k) {
                const float t=static_cast<float>(k)/static_cast<float>(pieces);
                nodes.push_back({a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t});
                metres.push_back(path.metres[i]+(path.metres[i+1]-path.metres[i])*t);
            }
        }
        // An open line ends at its last node; a loop's closing node is its
        // first, joined by the seam piece.
        if(!closed){nodes.push_back(path.points.back());metres.push_back(path.metres.back());}
        ground.Bind(nodes.size());ground.attempts=GroundCache::kAnswered;
        return nodes.size()>=2;
    }
    int Draw(float along,const Style& style) {
        const std::size_t m=nodes.size();
        if(m<2 || !std::isfinite(along) || style.a<=0 || !(source_length>0))return 0;
        along=std::clamp(along,0.0F,source_length);
        const auto offset=[&](std::size_t i) {
            float d=metres[i]-along;
            if(loop){d=std::fmod(d+source_length,source_length);if(d>source_length/2)d-=source_length;}
            return d;
        };
        const auto in_window=[&](std::size_t i){const float d=offset(i);return d>=-style.behind_m && d<=style.ahead_m;};
        // Probe the window nearest first, so what the rider is about to see
        // settles before the far end does.
        int budget=std::max(0,style.ground_probes_per_frame);
        std::vector<std::size_t> pending;
        for(std::size_t i=0;i<m;++i)if(in_window(i) && !ground.Answered(i))pending.push_back(i);
        std::sort(pending.begin(),pending.end(),[&](std::size_t a,std::size_t b){return std::abs(offset(a))<std::abs(offset(b));});
        for(std::size_t i:pending){if(budget<=0)break;--budget;ground.Probe(i,nodes[i]);}
        const auto at=[&](std::size_t i){Vector3 v=nodes[i];v.z=ground.z[i]+style.rail_diameter_m/2-style.ground_sink_m;return v;};
        int drawn=0;
        const auto piece=[&](std::size_t i,std::size_t j) {
            if(!in_window(i) || !in_window(j) || !ground.Answered(i) || !ground.Answered(j))return;
            GroundRail(at(i),at(j),style);++drawn;
        };
        for(std::size_t i=0;i+1<m;++i)piece(i,i+1);
        if(loop)piece(m-1,0);
        return drawn;
    }
};
} // namespace horse_races::corridor_overlay
