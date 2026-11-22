#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>

// A sixth core beside the game's five, for the player's burst
// (player_stimulant.h). The game's cores are a black disc with the core's
// icon on it and a thin ring on its edge that empties as the bar drains;
// they sit on an orbit round the radar, twenty degrees or so apart. This
// one is drawn the same way, in pixels - the disc and the ring from one-
// pixel rows, so it is round on any screen - with the mod's horseshoe on
// it, and its ring is the burst left: the whole ring the longest burst the
// settings allow (the special stimulant at the top bonding level), a lesser
// dose lighting that fraction. The arc is anchored at twelve and drawn
// clockwise, so three quarters of the longest burst reaches nine o'clock,
// and as the burst drains its end recedes counter-clockwise, back through
// six and three to twelve. It appears with the burst and goes with it;
// Calibrate shows it full at all times so its place can be tuned. Pure
// geometry here; the natives that draw it live in plugin.cpp.
namespace horse_races::stimulant_hud {
struct Config {
    bool enabled=true;
    float radar_x=.080F,radar_y=.824F;   // The radar's centre, fractions of the screen.
    float radar_radius=.135F;            // The radar's radius, fraction of the screen's height.
    float orbit=1.33F;                   // The cores' orbit, in radar radii.
    float angle_degrees=-33;             // On the orbit: 0 is right of the radar, negative up, positive down.
    float core_radius=.138F;             // The black disc's radius, in radar radii.
    float ring_thickness=.026F;          // The ring's thickness, in radar radii; it sits on the disc's edge.
    float ring_gap=0;                    // Between disc and ring, in radar radii; the game's cores have none.
    float icon_scale=1.3F;               // The icon's box, in disc radii.
    int r=255,g=196,b=64;                // The lit ring and the icon.
    int screen_width=0,screen_height=0;  // The screen in pixels; 0 = measured from the game's window.
    bool calibrate=false;                // Drawn full, always, to be placed.
    std::string icon="blip_ambient_horse";   // In the game's blips texture dictionary.
    bool Valid() const {
        return std::isfinite(radar_x) && std::isfinite(radar_y) && radar_x>=0 && radar_x<=1 && radar_y>=0 && radar_y<=1 &&
            std::isfinite(radar_radius) && radar_radius>0 && radar_radius<=.5F &&
            std::isfinite(orbit) && orbit>=0 && orbit<=4 && std::isfinite(angle_degrees) && angle_degrees>=-360 && angle_degrees<=360 &&
            std::isfinite(core_radius) && core_radius>0 && core_radius<=1 &&
            std::isfinite(ring_thickness) && ring_thickness>0 && ring_thickness<=.5F &&
            std::isfinite(ring_gap) && ring_gap>=0 && ring_gap<=.5F &&
            std::isfinite(icon_scale) && icon_scale>0 && icon_scale<=4 &&
            screen_width>=0 && screen_height>=0 && !icon.empty();
    }
};
struct Point {float x=0,y=0;};
// The disc's centre in pixels, on the orbit round the radar - round on any
// screen, since the orbit is drawn in pixels of the height both ways.
inline Point Centre(const Config& c,int width_px,int height_px) {
    const float a=c.angle_degrees*3.14159265F/180;
    const float d=c.orbit*c.radar_radius*static_cast<float>(height_px);
    return {c.radar_x*static_cast<float>(width_px)+std::cos(a)*d,c.radar_y*static_cast<float>(height_px)+std::sin(a)*d};
}
// The disc's half-width on the row `y` pixels below its centre (negative
// above), or false when the row misses it.
inline bool DiscRow(float y,float r,float& half_width) {
    if(!(std::fabs(y)<r))return false;
    half_width=std::sqrt(r*r-y*y);
    return true;
}
// A piece of the ring on one row: from x0 to x1 (pixels right of the
// centre, negative left), lit or not.
struct Span {float x0=0,x1=0;bool lit=false;};
// The ring's pieces on the row `y` pixels below the centre: the annulus
// between r_in and r_out cut by that row, at most two pieces a side, lit
// where the point's clockwise angle from twelve is under `fraction` of the
// full turn. Returns how many of `out` (room for four) were filled.
inline int RingRow(float y,float r_in,float r_out,float fraction,Span* out) {
    if(!(std::fabs(y)<r_out) || !(r_out>r_in) || !(r_in>=0))return 0;
    constexpr float kTurn=6.2831853F;
    const float end=std::clamp(fraction,0.0F,1.0F)*kTurn;
    const float xo=std::sqrt(r_out*r_out-y*y);
    const float xi=std::fabs(y)<r_in?std::sqrt(r_in*r_in-y*y):0.0F;
    if(!(xo>xi))return 0;
    // The clockwise angle from twelve of the point (x,y), 0..turn.
    const auto angle=[y](float x){float a=std::atan2(x,-y);if(a<0 || (a==0 && x<0))a+=kTurn;return a;};
    int n=0;
    const auto piece=[&](float a,float b) {   // a<b, one side of the row; the angle is monotonic along it
        const float inset=(b-a)*1e-3F;
        const float pa=angle(a+inset),pb=angle(b-inset);
        const float lo=std::min(pa,pb),hi=std::max(pa,pb);
        if(hi<=end){out[n++]={a,b,true};return;}
        if(lo>=end || end<=0){out[n++]={a,b,false};return;}
        // The arc's end, a ray from the centre, crosses this row at x=-y*tan(end).
        const float xb=std::clamp(-y*std::tan(end),a,b);
        if(pa<end){out[n++]={a,xb,true};out[n++]={xb,b,false};}
        else {out[n++]={a,xb,false};out[n++]={xb,b,true};}
    };
    piece(xi,xo);      // the right side, angles 0..half a turn
    piece(-xo,-xi);    // the left side, angles half a turn..a turn
    return n;
}
// The longest burst: the ring's whole.
inline double Whole(double special_seconds,const float* factors,std::size_t count) {
    float best=0;
    for(std::size_t i=0;i<count;++i)best=std::max(best,factors[i]);
    return special_seconds*best;
}
} // namespace horse_races::stimulant_hud
