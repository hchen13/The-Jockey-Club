#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include "game_natives.h"

// Evidence limits use game seconds and measured world positions. Neither a
// post-impact heading nor an artificial relocation supplies an impact sweep.
namespace horse_races::collision_learner {
inline constexpr double kContactWindowSeconds=.5;
inline constexpr float kMaximumSweepMetres=4;
inline constexpr int kMaximumSelfContinuations=8;
inline constexpr float kSelfAdvanceMetres=.05F;
enum class Outcome {
    None,Learned,AlreadyExcluded,NoHit,DynamicContact,SelfContact,NoContact,
    NoRiderFall,OutsideSweep,InvalidNormal,SupportingSurface,GroundUnknown,
    QueryUnavailable,QueryTimeout,QueryBudget,InvalidFootprint,OwnerChanged,Cancelled
};
inline const char* Name(Outcome value) {
    switch(value) {
    case Outcome::Learned:return "learned";
    case Outcome::AlreadyExcluded:return "outside-path-or-already-excluded";
    case Outcome::NoHit:return "no-hit-in-observed-sweep";
    case Outcome::DynamicContact:return "dynamic-first-contact";
    case Outcome::SelfContact:return "rider-or-mount-first-contact";
    case Outcome::NoContact:return "physical-contact-not-observed";
    case Outcome::NoRiderFall:return "rider-fall-not-observed";
    case Outcome::OutsideSweep:return "hit-outside-observed-sweep";
    case Outcome::InvalidNormal:return "invalid-contact-normal";
    case Outcome::SupportingSurface:return "supporting-terrain";
    case Outcome::GroundUnknown:return "support-classification-unknown";
    case Outcome::QueryUnavailable:return "query-unavailable";
    case Outcome::QueryTimeout:return "query-timeout";
    case Outcome::QueryBudget:return "query-budget-unavailable";
    case Outcome::InvalidFootprint:return "invalid-horse-footprint";
    case Outcome::OwnerChanged:return "impact-owner-changed";
    case Outcome::Cancelled:return "cancelled";
    default:return "none";
    }
}
inline bool Finite(Vector3 p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
inline float SegmentDistance(Vector3 p,Vector3 a,Vector3 b) {
    if(!Finite(p)||!Finite(a)||!Finite(b))return INFINITY;
    const float dx=b.x-a.x,dy=b.y-a.y,dz=b.z-a.z,length=dx*dx+dy*dy+dz*dz;
    const float t=length>1e-8F?std::clamp(((p.x-a.x)*dx+(p.y-a.y)*dy+(p.z-a.z)*dz)/length,0.0F,1.0F):0;
    return std::hypot(std::hypot(p.x-a.x-t*dx,p.y-a.y-t*dy),p.z-a.z-t*dz);
}
inline bool ShortObservedSweep(Vector3 a,Vector3 b,double elapsed) {
    return std::isfinite(elapsed)&&elapsed>0&&elapsed<=kContactWindowSeconds&&
        SegmentDistance(a,b,b)<=kMaximumSweepMetres;
}
// Keep searching the UNOBSERVED remainder when the first collider is our own
// rider or mount. The endpoint and radius stay fixed: every continuation is a
// subset of the original capsule, never a look-ahead beyond physical motion.
// A native capsule can ignore only one entity, so the next query ignores the
// own actor it just met. The caller still vetoes any other dynamic first hit.
inline bool ContinuePastSelf(Vector3 original_from,Vector3 original_to,Vector3 hit,float radius,
    float previous,float& progress,Vector3& from) {
    if(!Finite(original_from)||!Finite(original_to)||!Finite(hit)||!std::isfinite(radius)||radius<=0||
        !std::isfinite(previous)||previous<0)return false;
    const float dx=original_to.x-original_from.x,dy=original_to.y-original_from.y,dz=original_to.z-original_from.z;
    const float length=std::hypot(std::hypot(dx,dy),dz);
    if(length<=.001F||previous>=length-.001F)return false;
    const float projection=((hit.x-original_from.x)*dx+(hit.y-original_from.y)*dy+(hit.z-original_from.z)*dz)/length;
    progress=std::min(length,std::max(previous+kSelfAdvanceMetres,projection+radius+.001F));
    const float t=progress/length;
    from={original_from.x+t*dx,original_from.y+t*dy,original_from.z+t*dz};
    return progress>previous&&Finite(from);
}
// Native pointer outputs may be packed floats; SDK return vectors have 8-byte
// component slots. A NaN-filled buffer reveals which layout was written.
inline int ModelBound(const std::array<float,8>& raw,Vector3& bound) {
    const bool padded=std::isfinite(raw[4]);
    bound={raw[0],raw[padded?2:1],raw[padded?4:2]};
    return Finite(bound)?(padded?2:1):0;
}
inline bool FrontSweep(Vector3 a,Vector3 b,Vector3 facing_a,Vector3 facing_b,Vector3 low,Vector3 high,float radius,
    Vector3& from,Vector3& to) {
    if(!Finite(a)||!Finite(b)||!Finite(facing_a)||!Finite(facing_b)||!Finite(low)||!Finite(high)||
        low.y>0||high.y<=0||high.y<=low.y||high.y-low.y>2*kMaximumSweepMetres)return false;
    for(const auto facing:{facing_a,facing_b}) {
        const float length=std::hypot(std::hypot(facing.x,facing.y),facing.z);
        if(length<.9F||length>1.1F)return false;
    }
    // Sample the model's FRONT at both observed poses. Sweeping from tail to
    // nose traverses the saddle and first-hits our own rider after a fall.
    // Keep the full dynamic mask: another actor in front still hides a wall.
    // These bounded samples are not a reconstruction of the entire horse mesh.
    const float front=std::max(0.0F,high.y-radius);
    from={a.x+facing_a.x*front,a.y+facing_a.y*front,a.z+facing_a.z*front};
    to={b.x+facing_b.x*front,b.y+facing_b.y*front,b.z+facing_b.z*front};
    return true;
}
} // namespace horse_races::collision_learner
