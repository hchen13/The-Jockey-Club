#pragma once

#include <types.h>
#include <algorithm>
#include <cmath>
#include <optional>

namespace horse_races::destination {
inline constexpr float kFinishRadius = 12.0F;
inline constexpr float kFinishHeight = 6.0F;

inline bool Finite(Vector3 p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }
inline float Distance(Vector3 a, Vector3 b)
{
    return std::hypot(a.x - b.x, a.y - b.y);
}

// First crossing of the finish cylinder during this sample, not the closest
// point on a made-up recording. Both competitors use the same sample interval.
inline std::optional<float> ArrivalFraction(Vector3 from, Vector3 to, Vector3 finish)
{
    if (!Finite(from) || !Finite(to) || !Finite(finish)) return {};
    const float dx = to.x - from.x, dy = to.y - from.y;
    const float x = from.x - finish.x, y = from.y - finish.y;
    const float a = dx*dx + dy*dy;
    const float c = x*x + y*y - kFinishRadius*kFinishRadius;
    float t = 0.0F;
    if (c > 0.0F) {
        if (a < 0.00001F || a > 150.0F*150.0F) return {}; // No swept win across a teleport.
        const float b = 2.0F*(x*dx + y*dy);
        const float discriminant = b*b - 4.0F*a*c;
        if (discriminant < 0.0F) return {};
        t = (-b - std::sqrt(discriminant)) / (2.0F*a);
        if (t < 0.0F || t > 1.0F) return {};
    }
    if (std::abs(from.z + (to.z - from.z)*t - finish.z) > kFinishHeight) return {};
    return t;
}

// This first prototype is destination racing, not an enforced course. This
// distance is used only for a generous abandonment envelope, NEVER road claims.
inline float DistanceFromSegment(Vector3 p, Vector3 start, Vector3 finish)
{
    const float dx = finish.x - start.x, dy = finish.y - start.y;
    const float length2 = dx*dx + dy*dy;
    if (!Finite(p) || !Finite(start) || !Finite(finish) || length2 < 1.0F) return INFINITY;
    const float t = std::clamp(((p.x-start.x)*dx + (p.y-start.y)*dy) / length2, 0.0F, 1.0F);
    return std::hypot(p.x - (start.x + dx*t), p.y - (start.y + dy*t));
}
} // namespace horse_races::destination
