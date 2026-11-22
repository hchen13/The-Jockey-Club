#pragma once
#include "game_natives.h"
#include <algorithm>
#include <chrono>

// One clock for every deadline in this mod.
//
// std::chrono::steady_clock keeps running while the ASI thread is descheduled.
// The pause menu deschedules it completely - one recorded pause left a 4356 s
// gap in the log with not a single line in between - so on the first tick back
// a race timeout read 73 minutes and cancelled a race 80 m from the finish, and
// a 15 s appearance budget expired one second after it began because a
// loading-screen freeze had already been counted against it.
//
// The engine's own timer does not have that problem: across 60 recordings and
// 74,797 sample intervals it tracks steady_clock at a median ratio of 0.99984
// and stops only at the freezes - the worst caught in the act advanced it by
// 39 ms and the frame counter by 3 across 463 s of wall time. A pause is not
// time, which is what every timeout in a race always meant.
//
// Making this the type behind `Clock` everywhere - rather than substituting
// call sites - is the point: a default argument in a header, an anchor stamped
// in one file and compared in another, all inherit the same clock, and mixing
// it with real time becomes a compile error instead of a one-second abort.
// Real time survives only as WallClock, for profiling and disk polling.
namespace horse_time {
using WallClock=std::chrono::steady_clock;

struct GameClock {
    using rep=WallClock::rep;
    using period=WallClock::period;
    using duration=WallClock::duration;
    using time_point=std::chrono::time_point<GameClock,duration>;
    static constexpr bool is_steady=true;

    struct State {
        time_point now{};
        int engine_ms=0;
        bool ready=false;
        bool scaled=false;   // Fell back to the time-scaled timer.
    };
    static State& state(){static State s;return s;}

    // The non-scaled timer also ignores Dead Eye, which otherwise stretches a
    // sub-second speech gate tenfold. Fall back if this build does not have it.
    static int EngineMilliseconds() {
        const int unscaled=MISC::_GET_GAME_TIMER_NON_SCALED_CLIPPED();
        if(unscaled>0)return unscaled;
        state().scaled=true;
        return MISC::GET_GAME_TIMER();
    }
    // Accumulated as clamped deltas rather than read as an absolute: the 0
    // floor absorbs a stale or rebased read, and the 250 ms ceiling is double
    // the worst legitimate frame measured anywhere in the corpus. Sampled on
    // every read: the engine timer does not change within a frame, so repeated
    // reads add nothing, and the race code is entered from more than one tick.
    static time_point now() {
        auto& s=state();
        const int ms=EngineMilliseconds();
        if(!s.ready) {
            // Start from the real clock so the values look like an ordinary
            // steady_clock's; only the RATE of advance differs from here on.
            s.now=time_point(WallClock::now().time_since_epoch());
            s.engine_ms=ms;s.ready=true;
        }
        s.now+=std::chrono::milliseconds(std::clamp(ms-s.engine_ms,0,250));
        s.engine_ms=ms;
        return s.now;
    }
    static bool Scaled(){return state().scaled;}
};
} // namespace horse_time
