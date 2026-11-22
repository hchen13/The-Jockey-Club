#pragma once
#include "../race_authored_obstacles.h"

namespace horse_races::authored_obstacles {
// Confirmed in-game hitching posts: model p_hitchpostbla01x, origin XY.
// Z is the observed ray hit height. .30 m is a conservative estimate, not
// measured model geometry; the existing rider clearance is added separately.
inline const std::vector<Post> kBlackwaterPosts{
    {{-761.8439F,-1332.5637F,43.548F},.30F},
    {{-758.0580F,-1332.5637F,43.631F},.30F}
};
}
