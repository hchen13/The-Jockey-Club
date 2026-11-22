#pragma once
#include <cmath>

namespace horse_motion::speed_learning {
// Learning exclusion only: never change the horse's native motivation.
// A small tolerance avoids treating floating point noise as agitation. The
// recovery delay deliberately favours clean evidence over learning while an
// elevated gait is settling; neither value is a proven engine threshold.
struct AgitationGuard {
    static constexpr float kCalmTolerance=.01F;
    static constexpr float kRecoverySeconds=5.F;
    float calm_seconds=0;
    bool blocked=true;
    void Reset(){*this={};}
    bool Observe(float agitation,float elapsed) {
        if(!std::isfinite(agitation) || agitation<0 || agitation>1 ||
           !std::isfinite(elapsed) || elapsed<=0 || elapsed>.25F || agitation>kCalmTolerance) {
            calm_seconds=0;blocked=true;return blocked;
        }
        calm_seconds+=elapsed;
        blocked=calm_seconds<kRecoverySeconds;
        return blocked;
    }
};
} // namespace horse_motion::speed_learning
