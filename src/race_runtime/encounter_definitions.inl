// Private encounter definitions implementation; included by race_system.cpp.
enum class State {
    Disabled,
    Scanning,
    LoadingRoute,
    Ready,
    LoadingModels,
    PreparingFixedOutfit,
    Approaching,
    Offering,
    Accepted,
    Declined,
    Racing,
    Result,
    Departing,
    PreparingCircuit,
};

enum class ResultPhase {
    Interrupted,
    Arrival,
    Preparing,
    Settling,
    Starting,
    Playing,
    ScriptedRivalLine,
    Interaction,
    PlayerReply,
    OpponentReply,
};

constexpr Hash kCoordinateBlipStyle = 408396114;
// Native activity: always on the world map, radar range follows its local zoom level.
// Do not reuse the objective style here: it pins remote sites to the radar edge.
constexpr Hash kRaceActivityBlipStyle = 0xD792CF71; // BLIP_STYLE_MINIGAME
// Duels retain the verified SP horseshoe. Breed challenges may
// use the MP horse only after dictionary AND texture checks (UpdateFixedRaceIcon).
constexpr Hash kHorseRaceBlipSprite = 0xD99F0C2B; // BLIP_AMBIENT_HORSE (-643888085)
constexpr const char* kRaceBlipTextureDict = "blips";
constexpr Hash kFixedRaceBlipSprite = 564457427; // BLIP_MP_GAME_RACE_HORSE
constexpr Hash kFixedRaceTextureDictHash = 672378911; // blips_mp
constexpr const char* kFixedRaceTextureDict = "blips_mp";
// Native companion neutral small dot; encounter-owned coordinate launchers only.
constexpr Hash kEncounterCompanionBlipStyle = 0x19365607; // BLIP_STYLE_COMPANION
constexpr Hash kEncounterCompanionBlipSprite = 0x000A9056; // BLIP_AMBIENT_PED_SMALL
constexpr Hash kEncounterAlwaysRangeModifier = 0x32850803; // BLIP_MODIFIER_RADAR_EDGE_ALWAYS: Range Always
constexpr Hash kEncounterMapOnlyModifier = 0x1E8D95CF; // AUTO_MODIFIER_MAP_ONLY: Hidden on Radar
// beat_horse_race.c, func_70 -> func_209: original rival marker, not a race-launcher icon.
constexpr Hash kOriginalRivalBlipStyle = 4205537449U; // -89429847 / BLIP_STYLE_FRIENDLY
constexpr Hash kOriginalRivalBlipAlert = 580546400; // BLIP_MODIFIER_URGENT_ALERT
constexpr int kWaypointTaskFlags = 67139080;
constexpr int kRelationshipNeutral = 3;
constexpr float kRouteDeviationAllowance = 100.0F;
constexpr float kFinishHeadingTolerance = 80.0F;
constexpr Hash kConversationalSpeechParams = 291934926;
constexpr float kOriginalActivationMinRadius = 110.0F;
constexpr float kOriginalActivationMaxRadius = 215.0F;
constexpr float kOriginalEncounterCleanupRadius = 235.0F;
constexpr float kOriginalAwarenessRadius = 60.0F;
constexpr int kSpeedAttribute = 5;
constexpr int kAccelerationAttribute = 6;

struct DifficultyTier {
    int rank_bonus;
    int reward_cents;
    const char* name;
};

const std::array<DifficultyTier, 3> kDifficultyTiers{{
    {0, 1000, "standard"},
    {1, 1500, "competitive"},
    {2, 2000, "elite"},
}};

enum class RivalArchetype {
    Weird,
    Novice,
    Confident,
};

struct OriginalRaceVariant {
    int id;
    Vector3 encounter_anchor;
    Vector3 finish;
    const char* full_route;
    const char* shortcut_route;
    RivalArchetype archetype;
    const char* goal_speech;
};

// Direct transcription of beat 27's eleven fixed encounter points and route bindings in init_all_sp.c and
// beat_horse_race.c. Keeping these fields together prevents a route from being paired with another rival's dialogue.
const std::array<OriginalRaceVariant, 11> kOriginalRaceVariants{{
    {0, {1908.322F, -1583.275F, 42.6331F}, {1408.11F, -2286.16F, 40.87F}, "rehor_scarlett_a_1",
        "rehor_scarlett_shortcut_a_1", RivalArchetype::Confident, "RE_HR_UNI_V2_CONFIDENT_GOAL_A_ALT"},
    {1, {-1738.734F, 391.4279F, 108.8374F}, {-2315.48F, -455.95F, 143.09F}, "rehor_big_valley_a",
        "rehor_big_valley_a", RivalArchetype::Novice, "RE_HR_UNI_V1_NOVICE_GOAL_C"},
    {2, {-1130.605F, 460.2371F, 56.677F}, {-1030.82F, -303.08F, 82.94F}, "rehor_heartlands_a",
        "rehor_heartlands_shortcut_a", RivalArchetype::Novice, "RE_HR_UNI_V1_NOVICE_GOAL_A"},
    {3, {-1731.106F, -832.389F, 97.45F}, {-2315.48F, -455.95F, 143.09F}, "rehor_big_valley_b",
        "rehor_big_valley_shortcut_b", RivalArchetype::Novice, "RE_HR_UNI_V1_NOVICE_GOAL_C"},
    {4, {-1555.696F, 153.446F, 104.902F}, {-1030.82F, -303.08F, 82.94F}, "rehor_big_valley_c",
        "rehor_big_valley_shortcut_c", RivalArchetype::Novice, "RE_HR_UNI_V1_NOVICE_GOAL_A"},
    {5, {703.0021F, 1421.519F, 177.4066F}, {585.92F, 2339.94F, 231.22F}, "rehor_cumberland_a",
        "rehor_cumberland_shortcut_a", RivalArchetype::Weird, "RE_HR_UNI_V3_WEIRD_GOAL_E"},
    {6, {1481.706F, 1013.085F, 167.6041F}, {1557.9F, 1543.5F, 145.05F}, "rehor_heartlands_b",
        "rehor_heartlands_shortcut_b", RivalArchetype::Weird, "RE_HR_UNI_V3_WEIRD_GOAL_A"},
    {7, {2159.43F, 444.6013F, 83.4389F}, {2975.25F, 409.11F, 49.44F}, "rehor_roanoke_a",
        "rehor_roanoke_shortcut_a", RivalArchetype::Weird, "RE_HR_UNI_V3_WEIRD_GOAL_C"},
    {8, {1688.494F, -1617.637F, 52.7785F}, {1408.11F, -2286.16F, 40.87F}, "rehor_scarlett_a",
        "rehor_scarlett_shortcut_a", RivalArchetype::Confident, "RE_HR_UNI_V2_CONFIDENT_GOAL_A_ALT"},
    {9, {1548.366F, -1415.864F, 61.3286F}, {1408.11F, -2286.16F, 40.87F}, "rehor_scarlett_a_2",
        "rehor_scarlett_shortcut_a_2", RivalArchetype::Confident, "RE_HR_UNI_V2_CONFIDENT_GOAL_A_ALT"},
    {10, {860.2926F, -890.3083F, 51.4971F}, {1563.09F, -1433.79F, 60.32F}, "rehor_scarlett_b",
        "rehor_scarlett_shortcut_b", RivalArchetype::Confident, "RE_HR_UNI_V2_CONFIDENT_GOAL_B"},
}};

// A station is a PLACE: one route file, one marker, one direction cycle and
// its own memory. Who stands there is that file's Host= (empty means the
// station's original host), so a station whose Host= changes is still the
// same station, and two events by the same host are two stations. Nothing
// here names an NPC as the identity of a station.
//
// Station ids (11..17) are a separate destination course. Never index the
// eleven original-site arrays with them or request a Rockstar recording
// under these display names.
struct Station {
    const char* key;               // Place-based name, for logs and lookups.
    const char* routes_file;       // The route file this station loads, under the mod directory.
    OriginalRaceVariant variant;   // variant.id is the station's external site id.
    std::optional<route_config::Route> route,pending;
    std::string config_path,issue;
    std::optional<std::string> observed,parsed;
    Clock::time_point poll{};
    bool waiting_for_leave=false;
    Blip start_blip=0;
    Hash icon_sprite=0;
    // Where the camp says this station's marker belongs - on Arthur, or on
    // his horses - instead of on the start line 644 m away. Set by the camp
    // while its layout is valid for this player; a camp station with none is
    // not on the map at all.
    std::optional<Vector3> camp_marker;
    // What the route file (and an overlay's base) looked like when it was last read.
    file_stamp::Stamp stamp,base_stamp;
    std::string base_path;
};
// Slot 0 is the default station; an original encounter has no station of its
// own and reads that slot's route, memory and pickup flag, as it always has.
constexpr std::size_t kStationCount=17;
// Refreshed only when the Valentine route revision is accepted while idle.
// Selected races hold that revision and their frozen lap plan through settlement.
double g_price_reference_metres=0;
std::array<Station,kStationCount> g_stations{{
    {"emerald-valentine","HorseRaceRoutes.ini",
        {11,{},{},"hsl_emerald_valentine","hsl_emerald_valentine",RivalArchetype::Novice,""}},
    {"adler-strawberry","SadieRaceRoutes.ini",
        {12,{},{},"hsl_adler_strawberry","hsl_adler_strawberry",RivalArchetype::Novice,""}},
    {"valentine-circuit","CharlesRaceRoutes.ini",
        {13,{},{},"hsl_valentine_circuit","hsl_valentine_circuit",RivalArchetype::Novice,""}},
    {"rhodes-vanhorn","PearsonRaceRoutes.ini",
        {14,{},{},"hsl_rhodes_vanhorn","hsl_rhodes_vanhorn",RivalArchetype::Novice,""}},
    {"veteran-trail","VeteranCabinRaceRoutes.ini",
        {15,{},{},"hsl_veteran_trail","hsl_veteran_trail",RivalArchetype::Novice,""}},
    {"blackwater","BlackwaterRaceRoutes.ini",
        {16,{},{},"hsl_blackwater","hsl_blackwater",RivalArchetype::Novice,""}},
    {"new-austin","NewAustinRaceRoutes.ini",
        {17,{},{},"hsl_new_austin","hsl_new_austin",RivalArchetype::Novice,""}},
    // Arthur's own race, offered where he camps in the epilogue: on the map
    // once ArthurCampRaceRoutes.ini holds two surveyed points (its template
    // says how). Its host point stands beside the camp, and the camp stands
    // its own Arthur down while that one is out.
    {"arthur-camp","ArthurCampRaceRoutes.ini",
        {19,{},{},"hsl_arthur_camp","hsl_arthur_camp",RivalArchetype::Novice,""}},
    // The one-on-one his own horses offer: the player locks on to the horse
    // and Arthur rides it. Same shape as any other corridor race.
    {"arthur-camp-duel","ArthurCampDuelRoutes.ini",
        {20,{},{},"hsl_arthur_camp_duel","hsl_arthur_camp_duel",RivalArchetype::Novice,""}},
    // The same stakes race in Arthur's own chapters, where John hosts it on
    // Rachel: JohnStakesRaceRoutes.ini is an overlay on Arthur's file (other
    // host, Era=Arthur, its own host point), and the course data is the camp
    // race's (kCourseKeys).
    {"john-stakes","JohnStakesRaceRoutes.ini",
        {21,{},{},"hsl_john_stakes","hsl_john_stakes",RivalArchetype::Novice,""}},
    // The breed stakes races, one station each, recorded one by one. The
    // Arabians' is in the far south-west of New Austin.
    {"arabian-stakes","ArabianRaceRoutes.ini",
        {22,{},{},"hsl_arabian_stakes","hsl_arabian_stakes",RivalArchetype::Novice,""}},
    // The Appaloosas', north-west of Valentine.
    {"appaloosa-stakes","AppaloosaRaceRoutes.ini",
        {23,{},{},"hsl_appaloosa_stakes","hsl_appaloosa_stakes",RivalArchetype::Novice,""}},
    // The Standardbreds': a 221 m ring in Big Valley, five to ten laps.
    {"standardbred-stakes","StandardbredRaceRoutes.ini",
        {24,{},{},"hsl_standardbred_stakes","hsl_standardbred_stakes",RivalArchetype::Novice,""}},
    // The Paints': 5.2 km across Big Valley from Hawks Eye Creek, ridden once
    // end to end. Rains Fall hosts.
    {"paint-stakes","PaintRaceRoutes.ini",
        {25,{},{},"hsl_paint_stakes","hsl_paint_stakes",RivalArchetype::Novice,""}},
    // Fifteen to twenty recorded laps, all seven Tennessee Walker coats.
    {"tennessee-stakes","TennesseeRaceRoutes.ini",
        {26,{},{},"hsl_tennessee_stakes","hsl_tennessee_stakes",RivalArchetype::Novice,""}},
    {"new-austin-javier-duel","JavierDuelRaceRoutes.ini",
        {27,{},{},"hsl_new_austin_javier_duel","hsl_new_austin_javier_duel",RivalArchetype::Novice,""}},
    {"scarlett-meadows-bill-duel","BillDuelRaceRoutes.ini",
        {28,{},{},"hsl_scarlett_meadows_bill_duel","hsl_scarlett_meadows_bill_duel",RivalArchetype::Novice,""}},
}};
// A station that races another station's course reads that station's course
// data - survey map, pins, corridor width - instead of surveying it again.
constexpr std::array<std::pair<std::string_view,const char*>,1> kCourseKeys{{{"john-stakes","arthur-camp"}}};

std::size_t StationSlot(const Station& station) { return static_cast<std::size_t>(&station-g_stations.data()); }
// Canonical route definitions above never change when rolling a direction.
// A published selection survives map opens, streaming retries, F9 and F11.
std::array<std::optional<route_config::Route>,kStationCount> g_fixed_directed_routes;
std::array<bool,kStationCount> g_fixed_direction_renew{};

Vector3 OriginalFinishRotation(int variant_id)
{
    if (variant_id == 7) {
        return {0.0F, 0.0F, -35.0F};
    }
    if (variant_id == 6) {
        return {0.0F, 0.0F, 20.0F};
    }
    if (variant_id == 2 || variant_id == 4) {
        return {0.0F, 0.0F, 47.0F};
    }
    return {0.0F, 0.0F, 0.0F};
}

Vector3 OriginalFinishScale(int variant_id)
{
    if (variant_id == 7) {
        return {20.75F, 10.25F, 13.5F};
    }
    if (variant_id == 10) {
        return {18.0F, 18.0F, 10.0F};
    }
    if (variant_id == 0 || variant_id == 8 || variant_id == 9) {
        return {18.0F, 18.0F, 10.0F};
    }
    if (variant_id == 2 || variant_id == 4) {
        return {26.0F, 12.0F, 13.0F};
    }
    if (variant_id == 6) {
        return {36.0F, 13.0F, 6.0F};
    }
    return {18.0F, 18.0F, 5.0F};
}

struct OriginalRivalDefinition {
    const char* rider_model;
    Hash rider_outfit;
    const char* horse_model;
    const char* voice;
    const char* approach_a;
    const char* approach_b;
    const char* call_over;
    const char* waiting_a;
    const char* waiting_b;
    const char* player_accept;
    const char* player_decline;
    const char* lets_go;
    const char* react_start;
    const char* player_won;
    const char* player_lost;
    const char* player_win_reaction;
    const char* player_lose_reaction;
    const char* outro;
    const char* result_positive_win;
    const char* result_antagonize_win;
    const char* result_positive_loss;
    const char* result_antagonize_loss;
    std::array<const char*, 3> ahead_taunts;
    std::array<const char*, 4> opponent_passes;
    std::array<const char*, 4> player_passes;
    characters::Id character=characters::Id::OtherRival;
};

const OriginalRivalDefinition& RivalDefinition(RivalArchetype archetype)
{
    static const OriginalRivalDefinition novice{
        "RE_HORSERACE_MALES_01", static_cast<Hash>(2001844695), "A_C_HORSE_NOKOTA_WHITEROAN",
        "0702_A_M_M_RhdUpperClass_01_WHITE_02", "RE_HR_UNI_V1_NOVICE_APPROACH_A",
        "RE_HR_UNI_V1_NOVICE_APPROACH_B", "RE_HR_UNI_V1_NOVICE_CALLOVER",
        "RE_HR_UNI_V1_NOVICE_WAITING_A", "RE_HR_UNI_V1_NOVICE_WAITING_B",
        "RE_HR_UNI_V1_NOVICE_PLYR_ACCEPT", "RE_HR_UNI_V1_NOVICE_PLYR_DECLINE",
        "RE_HR_UNI_V1_NOVICE_LETS_GO", "RE_HR_UNI_V1_NOVICE_REACT_START",
        "RE_HR_UNI_V1_NOVICE_GOAL_PLYR_WIN",
        "RE_HR_UNI_V1_NOVICE_GOAL_PLYR_LOSE", "RE_HR_UNI_V1_NOVICE_PLYR_WIN",
        "RE_HR_UNI_V1_NOVICE_PLYR_LOSE", "RE_HR_UNI_V1_NOVICE_OUTRO",
        "RE_HR_UNI_V1_NOVICE_ILO_END_WON_A", "RE_HR_UNI_V1_NOVICE_ILO_END_WON_B",
        "RE_HR_UNI_V1_NOVICE_ILO_END_LOST_A", "RE_HR_UNI_V1_NOVICE_ILO_END_LOST_B",
        {"RE_HR_UNI_V1_NOVICE_PASS_A", "RE_HR_UNI_V1_NOVICE_PASS_B", "RE_HR_UNI_V1_NOVICE_PASS_C"},
        {"RE_HR_UNI_V1_NOVICE_PASS_D", "RE_HR_UNI_V1_NOVICE_PASS_E", "RE_HR_UNI_V1_NOVICE_PASS_F",
            "RE_HR_UNI_V1_NOVICE_PASS_G"},
        {"RE_HR_UNI_V1_NOVICE_PLAYER_PASS_A", "RE_HR_UNI_V1_NOVICE_PLAYER_PASS_B",
            "RE_HR_UNI_V1_NOVICE_PLAYER_PASS_C", "RE_HR_UNI_V1_NOVICE_PLAYER_PASS_D"}};
    static const OriginalRivalDefinition confident{
        "RE_HORSERACE_FEMALES_01", static_cast<Hash>(-1918933390), "A_C_HORSE_THOROUGHBRED_BRINDLE",
        "0847_A_F_M_Civ_Black_AVOID_02", "RE_HR_UNI_V2_CONFIDENT_APPROACH_A",
        "RE_HR_UNI_V2_CONFIDENT_APPROACH_B", "RE_HR_UNI_V2_CONFIDENT_CALLOVER",
        "RE_HR_UNI_V2_CONFIDENT_WAITING_A", "RE_HR_UNI_V2_CONFIDENT_WAITING_B",
        "RE_HR_UNI_V2_CONFIDENT_PLYR_ACCEPT", "RE_HR_UNI_V2_CONFIDENT_PLYR_DECLINE",
        "RE_HR_UNI_V2_CONFIDENT_LETS_GO", "RE_HR_UNI_V2_CONFIDENT_REACT_START",
        "RE_HR_UNI_V2_CONFIDENT_GOAL_PLYR_WIN",
        "RE_HR_UNI_V2_CONFIDENT_GOAL_PLYR_LOSE", "RE_HR_UNI_V2_CONFIDENT_PLYR_WIN",
        "RE_HR_UNI_V2_CONFIDENT_PLYR_LOSE", "RE_HR_UNI_V2_CONFIDENT_OUTRO",
        "RE_HR_UNI_V2_CONFIDENT_ILO_END_WON_A", "RE_HR_UNI_V2_CONFIDENT_ILO_END_WON_B",
        "RE_HR_UNI_V2_CONFIDENT_ILO_END_LOST_A", "RE_HR_UNI_V2_CONFIDENT_ILO_END_LOST_B",
        {"RE_HR_UNI_V2_CONFIDENT_PASS_A", "RE_HR_UNI_V2_CONFIDENT_PASS_B",
            "RE_HR_UNI_V2_CONFIDENT_PASS_C"},
        {"RE_HR_UNI_V2_CONFIDENT_PASS_D", "RE_HR_UNI_V2_CONFIDENT_PASS_E",
            "RE_HR_UNI_V2_CONFIDENT_PASS_F", "RE_HR_UNI_V2_CONFIDENT_PASS_G"},
        {"RE_HR_UNI_V2_CONFIDENT_PLAYER_PASS_A", "RE_HR_UNI_V2_CONFIDENT_PLAYER_PASS_B",
            "RE_HR_UNI_V2_CONFIDENT_PLAYER_PASS_C", "RE_HR_UNI_V2_CONFIDENT_PLAYER_PASS_D"}};
    static const OriginalRivalDefinition weird{
        "RE_HORSERACE_MALES_01", static_cast<Hash>(-2146022560), "A_C_HORSE_AMERICANSTANDARDBRED_BLACK",
        "1034_S_M_M_CoachTaxiDriver_01_WHITE_06", "RE_HR_UNI_V3_WEIRD_APPROACH_A",
        "RE_HR_UNI_V3_WEIRD_APPROACH_B", "RE_HR_UNI_V3_WEIRD_CALLOVER",
        "RE_HR_UNI_V3_WEIRD_WAITING_A", "RE_HR_UNI_V3_WEIRD_WAITING_B",
        "RE_HR_UNI_V3_WEIRD_PLYR_ACCEPT", "RE_HR_UNI_V3_WEIRD_PLYR_DECLINE",
        "RE_HR_UNI_V3_WEIRD_LETS_GO", "RE_HR_UNI_V3_WEIRD_REACT_START",
        "RE_HR_UNI_V3_WEIRD_GOAL_PLYR_WIN",
        "RE_HR_UNI_V3_WEIRD_GOAL_PLYR_LOSE", "RE_HR_UNI_V3_WEIRD_PLYR_WIN",
        "RE_HR_UNI_V3_WEIRD_PLYR_LOSE", "RE_HR_UNI_V3_WEIRD_OUTRO",
        "RE_HR_UNI_V3_WEIRD_ILO_END_WON_A", "RE_HR_UNI_V3_WEIRD_ILO_END_WON_B",
        "RE_HR_UNI_V3_WEIRD_ILO_END_LOST_A", "RE_HR_UNI_V3_WEIRD_ILO_END_LOST_B",
        {"RE_HR_UNI_V3_WEIRD_PASS_A", "RE_HR_UNI_V3_WEIRD_PASS_B", "RE_HR_UNI_V3_WEIRD_PASS_C"},
        {"RE_HR_UNI_V3_WEIRD_PASS_D", "RE_HR_UNI_V3_WEIRD_PASS_E", "RE_HR_UNI_V3_WEIRD_PASS_F",
            "RE_HR_UNI_V3_WEIRD_PASS_G"},
        {"RE_HR_UNI_V3_WEIRD_PLAYER_PASS_A", "RE_HR_UNI_V3_WEIRD_PLAYER_PASS_B",
            "RE_HR_UNI_V3_WEIRD_PLAYER_PASS_C", "RE_HR_UNI_V3_WEIRD_PLAYER_PASS_D"}};
    switch (archetype) {
    case RivalArchetype::Novice:
        return novice;
    case RivalArchetype::Confident:
        return confident;
    case RivalArchetype::Weird:
        return weird;
    }
    return novice;
}

struct ScriptedSpeechParams {
    const char* speech_name{};
    const char* voice_name{};
    alignas(8) int variation{};
    alignas(8) Hash speech_param_hash{};
    alignas(8) Ped listener{};
    alignas(8) BOOL sync_over_network{};
    alignas(8) int v7{};
    alignas(8) int v8{};
};

static_assert(sizeof(ScriptedSpeechParams) == 0x40);
