#pragma once
#include "race_racing_line_build.h"

namespace horse_races::racing_line {
// Live width change: the corridor half-width in force changed (F11 +/-), so
// the bound changed. Rebuild in place with everything else as before.
inline bool Rebuild(const course::Path& centreline,Params& params,float half_width,Result& out) {
    params.half_width=half_width;
    return Build(centreline,params,out);
}
} // namespace horse_races::racing_line
