// Private implementation of arthur_camp.cpp; included inside its original namespace.
// Grounded scene poses, seats and camp activity occupancy.
// The ground under a spot, when the game has it: a capture is a foot on the
// ground, but a prop offset a few metres from it may be up or down a slope.
bool GroundZ(float x,float y,float z,float& out) {
    STREAMING::REQUEST_COLLISION_AT_COORD(x,y,z);
    float g=0;Vector3 normal{};
    if(!MISC::GET_GROUND_Z_AND_NORMAL_FOR_3D_COORD(x,y,z+6.0F,&g,&normal) || !std::isfinite(g) || std::abs(g-z)>8.0F)return false;
    out=g;return true;
}
Pose Grounded(const Pose& pose,bool ground,float up) {
    Pose out=pose;
    float g=0;
    if(ground && GroundZ(pose.position.x,pose.position.y,pose.position.z-up,g))out.position.z=g+up;
    return out;
}
std::optional<float> FireFacingYaw(const Pose& seat);
Pose ActivityPose(const Scene& s) {
    // From where the anchor actually landed, not where the layout meant it.
    Pose pose=!s.anchor.empty() && g_placed.count(s.anchor)?Offset(g_placed.at(s.anchor),s.forward,s.right,s.up,s.yaw)
                                                           :Offset(g_layout.tent,s.forward,s.right,s.up,s.yaw);
    // Someone standing at the fire faces the fire, from wherever they stand. Mary's place beside it was
    // a hand-written half-turn and pointed about 35 degrees wide of it (2026-09-22). A sitter is aligned
    // by their seat's own point instead, so this is only for the ones on their feet.
    if(!s.sitting)if(const auto facing=FireFacingYaw({pose.position,0}))pose.heading=*facing;
    return pose;
}
std::string Describe(const Pose& p) {
    return std::to_string(p.position.x)+","+std::to_string(p.position.y)+","+std::to_string(p.position.z)+" h="+std::to_string(p.heading);
}
std::string Describe(const Point& p){return std::to_string(p.x)+","+std::to_string(p.y)+","+std::to_string(p.z);}
// A seat carries a scenario point of its own - the one the player may use -
// and the game aligns a sitting ped to the point. Starting a seat scenario at
// a bare position sat him on the ground facing backwards through the tent
// wall (2026-09-16), so an activity anchored to a seat uses that seat's point.
// Which way a sitter has to face to have the fire in front of them, as a yaw relative to the seat.
// A fixed half-turn faces wherever the prop happened to be dropped: on 2026-09-22 the far crate sat
// Mary and a passing stranger 71 degrees off the fire, warming their hands at nothing, while the other
// four seats happened to fall within 10 degrees. Empty for a seat that is not at this fire (the cot, a
// chair inside the tent), which keeps the yaw its layout states.
// How high the top of a prop stands above its origin, asked of the model rather than typed out.
float SeatTop(Hash model) {
    Vector3 minimum{},maximum{};
    MISC::GET_MODEL_DIMENSIONS(model,&minimum,&maximum);
    return std::isfinite(maximum.z)?maximum.z:0;
}
// How far from the tent's middle a prop can stand and still be thought of as inside it. Beyond this
// the tent's own geometry says nothing useful about which side of the prop is open.
constexpr float kInsideTent=8;
// The layout's record of a prop - where it was placed relative to the tent, which is the frame its
// offsets are written in.
const Prop* PropByKey(std::string_view key) {
    for(const auto& p:g_layout.props)if(p.key==key)return &p;
    return nullptr;
}
// Which model a prop was built from, by the name the layout gives it.
std::string_view PropModel(std::string_view key) {
    for(const auto& p:g_layout.props)if(p.key==key)return p.model;
    return {};
}
// A chair, a crate or a bench at the fire turns to face it. A cot, a table, a bedroll keeps the yaw
// its layout states: Arthur lay across his own cot with half of him through it (the user, 2026-09-22)
// because everything within six metres of the fire was being turned towards it, beds included.
std::optional<float> SeatFacesFire(const Pose& seat,std::string_view prop_key);
std::optional<float> FireFacingYaw(const Pose& seat) {
    const auto fire=g_placed.find("Fire");
    if(fire==g_placed.end())return {};
    const float dx=fire->second.position.x-seat.position.x,dy=fire->second.position.y-seat.position.y;
    const float away=std::hypot(dx,dy);
    if(!std::isfinite(away) || away<0.8F || away>6.0F)return {};
    const float bearing=std::fmod(std::atan2(-dx,dy)*57.29577951F+360.0F,360.0F);
    return std::fmod(bearing-seat.heading+360.0F,360.0F);
}
std::optional<float> SeatFacesFire(const Pose& seat,std::string_view prop_key) {
    if(!SeatScenario(PropModel(prop_key)))return {};
    return FireFacingYaw(seat);
}
// The sixth argument of TASK_USE_SCENARIO_POINT: true warps the ped onto the point, which is what the
// game's own scripts pass because they have just created the ped standing on it. False makes them walk.
constexpr BOOL kWalkToPoint=false;
constexpr float kSeatApproach=1.2F;   // Metres in front of a seat that someone walks to first.
// Where a sitter ends up, and where they should stand before taking the seat.
std::optional<std::pair<Pose,Pose>> SeatApproach(const Scene& s) {
    if(!s.sitting || s.anchor.empty())return {};
    const auto anchor=g_placed.find(s.anchor);
    if(anchor==g_placed.end())return {};
    const bool a_seat=SeatScenario(PropModel(s.anchor))!=nullptr;
    const float yaw=a_seat?SeatFacesFire(anchor->second,s.anchor).value_or(s.yaw):s.yaw;
    const Pose seat=Offset(anchor->second,s.forward,s.right,s.up,yaw);
    const auto stand=[&](const Point& d,float heading) {
        return Pose{{seat.position.x+d.x*kSeatApproach,seat.position.y+d.y*kSeatApproach,
            anchor->second.position.z},heading};
    };
    // A chair or a crate is taken from in front of it.
    if(a_seat)return std::pair{seat,stand(Forward(seat.heading),Wrap(seat.heading+180))};
    // A bed is not. A step "in front of" it is a step past its head and through the tent wall, and
    // left to the game's own pathing she walked round the OUTSIDE of the tent and stood against its
    // back (the user, 2026-09-22 and again 2026-09-23). It is entered from a long side - the side
    // with floor on it.
    //
    // Which side that is comes from where the bed stands in the tent, not from which way the tent
    // faces: the cot lies ALONG the tent's axis, so both of its long sides are equally square to the
    // tent's heading and comparing them against it decided the question on 0.0007 of a dot product -
    // four hundredths of a degree of recorded yaw, and the wrong sign would have walked her through
    // the other wall. The cot is 0.82 m to the tent's left of centre, so the floor is on the tent's
    // right: a metre of margin instead of a thousandth.
    const Prop* placed=PropByKey(s.anchor);
    if(!placed)return {};
    // A prop that is not really in the tent has no "open side" to speak of; the game's own pathing
    // does better than a guess from an unrelated object.
    if(std::hypot(placed->forward,placed->right)>kInsideTent)return {};
    // Stand on the open floor beside it and let the point take you in from there. The floor is on
    // the side of the tent the bed is NOT on: the cot sits 0.82 m to the tent's left of centre, so
    // the floor is to its right. Choosing between the bed's own two long sides instead needed them
    // to be square to that direction, which they are not when the bed lies along the tent - the two
    // numbers came out to a thousandth of each other and the answer was noise.
    const float away=placed->right<=0?Wrap(g_layout.tent.heading-90):Wrap(g_layout.tent.heading+90);
    const Point open=Forward(away);
    return std::pair{seat,stand(open,Wrap(away+180))};
}
// The bed's final stand point is inside the tent. A direct walk from the fire
// to that point cuts through the canvas side wall even though the destination
// itself is on the open floor. Approach the tent's front opening first.
std::optional<Pose> TentDoorFor(const Scene& s) {
    if(!s.sitting || s.scenario.find("SLEEP_BED")==std::string::npos)return {};
    const Prop* anchor=PropByKey(s.anchor);
    if(!anchor || std::hypot(anchor->forward,anchor->right)>kInsideTent)return {};
    return Offset(g_layout.tent,2.6F,0,0,0);
}
int SeatPoint(const Scene& s) {
    if(!s.sitting)return 0;
    const auto it=g_activity_point.find(s.name);
    return it==g_activity_point.end()?0:it->second;
}
// Ask for an activity: he plays his way out of the one he is in first, and
// the new one starts when he is out of it. Nothing snaps.
void AskActivityAt(std::size_t a,const Clock::time_point& now,const char* why);
// The other way round from ArthurIsAt below: the cot and the crates carry one point each, so whoever
// gets there first has it and the other takes a different activity (the user, 2026-09-22: "if someone
// is on the bed, skip it and pick another activity" - they do not want a second bed).
// Ours are known from what they were asked to do, even while they are still walking there; a
// passer-by is only known from the point itself. The user watched a stranger sit down on the crate
// Mary was already on (2026-09-22) - nothing of ours had asked him to.
bool SeatTaken(int point,Ped mine) {
    if(!point)return false;
    const Ped who=TASK::_GET_PED_USING_SCENARIO_POINT(point);
    return who && who!=mine && ENTITY::DOES_ENTITY_EXIST(who);
}
bool MaryIsAt(std::string_view anchor) {
    const auto& scenes=g_layout.companion_scenes;
    if(anchor.empty() || scenes.empty() || !Exists(g_mary.ped))return false;
    const Scene& hers=scenes[std::min(g_mary.activity,scenes.size()-1)];
    return hers.sitting && hers.anchor==anchor;
}
