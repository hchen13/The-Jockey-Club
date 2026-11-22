#pragma once
#include "race_course.h"
#include <span>
#include <string_view>

namespace horse_races::recorded_reference {
// A local piece of an actual recording. The containing race keeps its
// canonical judge/profile identity; only this declared core is mandatory.
struct Fragment {
    std::string_view station,canonical_geometry,source,source_sha256;
    std::size_t source_first_sample=0;
    float from=0,to=0;
    std::span<const Vector3> points;
};
}
