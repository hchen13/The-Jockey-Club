// Private labels and config implementation; included by race_system.cpp.
Hash& PreviousFixedHorse() { return g_fixed_previous_horses[std::max(FixedStationIndex(g_variant),0)]; }
marybeth_dialogue::Meeting& FixedMeeting() { return g_fixed_meetings[std::max(FixedStationIndex(g_variant),0)]; }
std::uint64_t g_mary_instance = 0;
bool g_mary_talk_pressed = false;
int g_mary_horse_health = 0, g_mary_player_horse_health = 0;
Clock::time_point g_mary_fall_at{};

const OriginalRivalDefinition& FixedRivalDefinition()
{
    // Private clones only; no real companion/persistent-character writes. Empty
    // encounter contexts never borrow Novice dialogue. Fixed clips use reviewed allowlists.
    static const auto rivals=[] {
        std::array<OriginalRivalDefinition,hosts::kProfiles.size()> values{};
        for(std::size_t i=0;i<values.size();++i) {
            const auto& profile=hosts::kProfiles[i];auto& result=values[i];
            result.rider_model=profile.model;result.character=profile.id;
            result.horse_model="A_C_HORSE_AMERICANSTANDARDBRED_BLACK";
            result.voice=profile.voice?profile.voice:"";
        }
        return values;
    }();
    for(const auto& rival:rivals)if(rival.character==FixedProfile().id)return rival;
    return rivals.front();
}

std::string FixedRouteTitle(const OriginalRaceVariant* site)
{
    // Activity/legend label. Route and destination names remain in data and the
    // in-race finish marker, not separate world-map legend entries. A route
    // that fields a whole pack against one breed is a different kind of event
    // from a one-against-one duel, so it says so on the map.
    if(FixedRouteDefinition(site?site:g_variant).field)
        return i18n::Text("Breed Challenge", "品种限定赛");
    return i18n::Text("Rider Duel", "对决赛");
}
std::string FixedFinishName()
{
    const auto& route=SelectedFixedRoute();
    return i18n::Label(route.finish.name, route.finish.name_zh);
}
std::string FixedRacePromptLabel()
{
    // Same compact entry-stake presentation for every fixed host. Route prize
    // multipliers still affect settlement, not the interaction's label format.
    return std::string(i18n::Text("Race", "赛马"))+" ($"+std::to_string(g_fixed_stake_cents/100)+")"+
        (g_laps.plan.Total()>1?" - "+std::to_string(g_laps.plan.Total())+i18n::Text(" laps"," 圈"):"");
}
std::string FixedPracticePromptLabel()
{
    return std::string(i18n::Text("Practice","练习"))+
        (g_laps.plan.Total()>1?" ("+std::to_string(g_laps.plan.Total())+i18n::Text(" laps)"," 圈)"):"");
}

const char* ResultPlaylist(bool player_won)
{
    // Vanilla func_78/func_79: these names describe the rival's result, not the player's.
    if (g_variant->archetype == RivalArchetype::Novice) {
        return player_won ? "PBL_LOSS_01" : "PBL_WIN_01";
    }
    if (g_variant->archetype == RivalArchetype::Confident) {
        return player_won ? "confident_loss_01" : "confident_win_01";
    }
    return player_won ? "weird_loss_01" : "weird_win_01";
}

bool HasScriptedRivalResultLine()
{
    // Vanilla func_78 explicitly calls func_82(5) for iLocal_402 == 0 (Weird).
    // func_79 calls func_82(6); func_253 gives this archetype zero delay in both outcomes.
    // Do not extend this to Novice: its scene-owned audio was separately validated in game.
    return g_variant->archetype == RivalArchetype::Weird;
}

const char* StateName(State state)
{
    switch (state) {
    case State::Disabled:
        return "Disabled";
    case State::Scanning:
        return "Scanning";
    case State::LoadingRoute:
        return "LoadingRoute";
    case State::Ready:
        return "Ready";
    case State::LoadingModels:
        return "LoadingModels";
    case State::PreparingFixedOutfit:
        return "PreparingAppearance";
    case State::Approaching:
        return "Approaching";
    case State::Offering:
        return "Offering";
    case State::Accepted:
        return "Accepted";
    case State::Declined:
        return "Declined";
    case State::Racing:
        return "Racing";
    case State::Result:
        return "Result";
    case State::Departing:
        return "Departing";
    case State::PreparingCircuit:
        return "PreparingCircuit";
    }
    return "Unknown";
}

void Log(const std::string& message)
{
    horse_races::log_sink::Shared().Write("[Race] " + message);
}

std::string LoadString(const char* key, const char* fallback)
{
    std::array<char, 256> value{};
    GetPrivateProfileStringA(
        "RacePrototype", key, fallback, value.data(), static_cast<DWORD>(value.size()), g_ini_path.c_str());
    return value.data();
}

float LoadFloat(const char* key, const char* fallback, float minimum, float maximum, float default_value)
{
    try {
        const float parsed = std::stof(LoadString(key, fallback));
        if (std::isfinite(parsed) && parsed >= minimum && parsed <= maximum) {
            return parsed;
        }
    } catch (...) {
    }
    Log(std::string("invalid ") + key + "; using " + fallback);
    return default_value;
}

int DisplayedAttributeLevel(Ped horse, int attribute)
{
    return std::clamp(ATTRIBUTE::GET_ATTRIBUTE_RANK(horse, attribute) + 1, 1, 10);
}
