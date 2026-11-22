#include "log_sink.h"
#include "runtime_paths.h"
#include "race_showcase.h"
#include "race_localization.h"
#include "rider_outfits.h"
#include "fixed_rider_models.h"
#include "race_host_profiles.h"

#include "game_natives.h"
#include "game_clock.h"
#include "race_firearm_guard.h"
#include "race_appearance.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>

namespace horse_races::showcase {
namespace {
using Clock=horse_time::GameClock;
enum class Phase { Off, Planning, Loading, Creating, Settling, Dressing, Visible };
constexpr std::size_t kCount = 6;
constexpr float kSiteRadius = 250.0F;
constexpr float kCleanupDistance = 100.0F;
constexpr float kSpacing = 4.0F;
struct Entry {
    const char* horse_name{};
    Hash horse_model{};
    Vector3 position{};
    Ped horse{};
    Ped rider{};
    const char* donor_name{};
    Hash donor_model{};
    int donor_preset{};
    Blip blip{};
    int clothing_changes{};
    std::string tack;
    Clock::time_point grounded_since{};
    bool grounded{};
    bool shown{};
    bool skipped{};
    bool clothing_verified{};
    std::string clothing_status = "original";
    int create_attempts{};
    appearance::Session appearance;
    bool position_valid=false;
    bool appearance_failed=false;
    bool excluded=false;
    int actual_preset=-1;
    std::string failure_reason;
};

std::string g_directory;
runtime_paths::Paths g_paths;
Phase g_phase = Phase::Off;
Site g_site{};
const char* HostOutfitLabel() {
    static std::string label;
    const auto* profile=hosts::Find(g_site.character);
    label=std::string(profile?profile->name:"Rider")+i18n::Text(" outfit #"," 服装 #");
    return label.c_str();
}
Ped g_player = 0;
Vector3 g_origin{};
Vector3 g_forward{};
float g_heading = 0.0F;
Hash g_rider_model = 0;
std::vector<Entry> g_entries(kCount);
bool g_preset_count_known = false;
std::vector<Hash> g_requested_models;
int g_outfit_request = 0;
bool g_outfit_requested = false;
std::size_t g_cursor = 0;
std::size_t g_page=0;
Clock::time_point g_started{};
Clock::time_point g_phase_started{};
Clock::time_point g_next_plan{};
Clock::time_point g_next_label{};
int g_last_label = -1;
float g_min_normal_z = 0.82F;
float g_max_ground_delta = 15.0F;
float g_planning_timeout = 6.0F;
float g_spawn_lift = 0.35F;
Clock::time_point g_next_placement_log{};
std::string g_placement_failure;
std::size_t g_candidate = 0;

#include "race_showcase_runtime/session_assets.inl"

#include "race_showcase_runtime/presentation.inl"

#include "race_showcase_runtime/placement.inl"

#include "race_showcase_runtime/appearance.inl"
} // namespace

void Initialize(const std::string& directory)
{
    if (Active()) Clear("reinitialize");
    g_directory = directory;
    g_paths=runtime_paths::Resolve(directory);
    horse_races::log_sink::Shared().Open(g_paths.Log("TheJockeyClub.log").string());
}

bool Active() { return g_phase != Phase::Off; }
int SiteId() { return g_site.id; }

bool Start(const Site& site)
{
    if (Active()) return false;
    g_player = PLAYER::PLAYER_PED_ID();
    if (!SafeSession()) { Notify(i18n::Text("Race display unavailable during a mission or player transition.", "任务中或角色切换时无法展示骑手。")); return false; }
    g_origin = ENTITY::GET_ENTITY_COORDS(g_player, true, false);
    const Ped movement_entity = PED::IS_PED_ON_MOUNT(g_player) ? PED::GET_MOUNT(g_player) : g_player;
    const float speed = Exists(movement_entity) ? ENTITY::GET_ENTITY_SPEED(movement_entity) : 100.0F;
    if (!std::isfinite(speed) || speed > 1.0F || DistanceSquared(g_origin, site.anchor) > kSiteRadius*kSiteRadius) {
        Notify(i18n::Text("Stop near a race start marker, face open ground, then choose Host outfit display in F11.", "请在赛马起点附近停下，面向空地，再选择 F11 的点位 Host 服装展示。"));
        return false;
    }
    g_site = site;
    g_entries.assign(kCount, Entry{});
    g_preset_count_known = false;
    g_cursor = 0;
    g_page=0;
    g_last_label = -1;
    g_next_plan = g_next_label = {};
    g_next_placement_log = {};
    g_placement_failure.clear();
    g_candidate = 0;
    // Read on every display start so placement tuning does not need another restart.
    g_min_normal_z = PlacementSetting("ShowcaseMinGroundNormalZ", "0.82", 0.70F, 1.0F);
    g_max_ground_delta = PlacementSetting("ShowcaseMaxGroundDelta", "15.0", 2.0F, 25.0F);
    g_planning_timeout = PlacementSetting("ShowcasePlanningTimeoutSeconds", "6.0", 2.0F, 12.0F);
    g_spawn_lift = PlacementSetting("ShowcaseSpawnLift", "0.35", 0.10F, 0.60F);
    g_forward = ENTITY::GET_ENTITY_FORWARD_VECTOR(g_player);
    const float length = std::sqrt(g_forward.x*g_forward.x + g_forward.y*g_forward.y);
    if (!std::isfinite(length) || length < 0.01F) return false;
    g_forward.x /= length;
    g_forward.y /= length;
    g_heading = MISC::GET_HEADING_FROM_VECTOR_2D(-g_forward.x, -g_forward.y);
    g_rider_model = MISC::GET_HASH_KEY(site.rider_model);
    // One original mount, then five distinct stock single-player horse models.
    const std::array<const char*, 6> candidates{{"A_C_HORSE_NOKOTA_WHITEROAN",
        "A_C_HORSE_THOROUGHBRED_BRINDLE", "A_C_HORSE_AMERICANSTANDARDBRED_BLACK",
        "A_C_HORSE_MORGAN_BAY", "A_C_HORSE_MUSTANG_GRULLODUN", "A_C_HORSE_SHIRE_DARKBAY"}};
    g_entries[0].horse_name = site.horse_model;
    // Original female identity/outfit is locked in every slot. Male previews use
    // adult male models with their OWN stock presets. Old Novice #4 / Weird #2
    // garment combinations are retired, not silently reused on the race mesh.
    const std::array<const char*, 5> male_models{{"A_M_M_RHDTOWNFOLK_01",
        "A_M_M_STRTOWNFOLK_01", "A_M_M_BIVFANCYTRAVELLERS_01",
        "A_M_M_BLWTOWNFOLK_01", "A_M_M_VALTOWNFOLK_01"}};
    if (!site.female) for (std::size_t i = 1; i < kCount; ++i) {
        g_entries[i].donor_name = male_models[i - 1];
        g_entries[i].donor_model = MISC::GET_HASH_KEY(male_models[i - 1]);
        g_entries[i].donor_preset = 0;
        if (!ValidModel(g_entries[i].donor_model)) {
            // Do not display the original racer under a failed candidate's ID.
            // Keep this requested identity so its slot can report NOT REVIEWED.
            Log("complete male candidate unavailable; its slot will fail explicitly");
        }
    }
    std::size_t next = 1;
    for (const char* name : candidates) {
        if (std::string(name) != site.horse_model && next < kCount) g_entries[next++].horse_name = name;
    }
    if (site.all_rider_presets) for (std::size_t i=0;i<g_entries.size();++i) {
        g_entries[i].horse_name=site.horse_model;
        g_entries[i].donor_name=site.rider_model;
        g_entries[i].donor_model=g_rider_model;
        g_entries[i].donor_preset=static_cast<int>(i);
    }
    if(site.all_rider_presets && appearance::FixedOutfit(site.character)) {
        g_entries.resize(1);g_preset_count_known=true;
        Log("protagonist display: same required full outfit as production; no outfit pages/randomization");
    }
    if (!ValidModel(g_rider_model)) { Notify(i18n::Text("Race display rider model unavailable; nothing spawned.", "骑手模型不可用，未生成展示。")); return false; }
    for (Entry& entry : g_entries) {
        entry.horse_model = MISC::GET_HASH_KEY(entry.horse_name);
        if (!ValidModel(entry.horse_model)) { Notify(i18n::Text("Race display model unavailable; nothing spawned.", "展示模型不可用，未生成展示。")); return false; }
    }
    g_started = Clock::now();
    SetPhase(Phase::Planning);
    Log("start site=" + std::to_string(site.id) + " (default-off, display-only)");
    Log("placement config: p4=false min_normal_z=" + std::to_string(g_min_normal_z) +
        " max_ground_delta=" + std::to_string(g_max_ground_delta) +
        " adaptive_per_pair=true timeout_per_slot=" + std::to_string(g_planning_timeout) +
        " spawn_lift=" + std::to_string(g_spawn_lift));
    Notify(i18n::Text("Preparing race outfits. Six pairs/page; Left/Right; F11 display or F10 clears.",
        "正在准备服装展示，每页最多 6 组；左右键翻页，F11 展示项或 F10 清理。"));
    return true;
}

void Clear(const char* reason)
{
    if (!Active()) return;
    // Exact private handles only. Never scan/delete nearby actors, player horses or story entities.
    for (Entry& entry : g_entries) {
        DeleteEntry(entry);
        entry = {};
    }
    ReleaseAssets();
    SetPhase(Phase::Off);
    Log(std::string("cleared: ") + reason);
    Notify(i18n::Text("Race display OFF. Details in log.", "骑手展示已关闭，详情见日志。"));
}

void ChangePage(int direction)
{
    if(!Active() || !direction)return;
    if(!SafeSession()){Clear("session changed during page request");return;}
    if(!g_site.all_rider_presets || (g_preset_count_known && PageCount()==1)) {Notify(PageLabel());return;}
    if(!g_preset_count_known) {Notify(i18n::Text("Waiting for the native outfit list; then Left/Right.","正在等原生服装列表，加载后可用左右方向键翻页。"));return;}
    // Explicit debug navigation owns deletion of the old page, including failed
    // markers and in-flight requests. No automatic cycling in front of the user.
    for(Entry& entry:g_entries)DeleteEntry(entry);
    ReleaseAssets();
    const auto count=PageCount();
    g_page=direction>0?(g_page+1)%count:(g_page+count-1)%count;
    g_cursor=PageBegin();g_candidate=0;g_last_label=-1;
    for(std::size_t i=PageBegin();i<PageEnd();++i) {
        Entry& entry=g_entries[i];entry.skipped=false;entry.failure_reason.clear();entry.appearance_failed=false;
        entry.actual_preset=-1;entry.create_attempts=0;entry.position_valid=false;
    }
    g_started=Clock::now();g_next_plan=g_next_placement_log=g_next_label={};g_placement_failure.clear();
    SetPhase(Phase::Planning);Log("explicit page change: "+PageLabel());Notify(PageLabel());
}

void Tick()
{
    if (!Active()) return;
    if (!SafeSession() || DistanceSquared(ENTITY::GET_ENTITY_COORDS(g_player, true, false), g_origin) >
            kCleanupDistance*kCleanupDistance) { Clear("left area / mission / player changed"); return; }
    for (Entry& shown : g_entries) {
        if (shown.shown && (!Exists(shown.rider) || !Exists(shown.horse) ||
                ENTITY::IS_ENTITY_DEAD(shown.rider) || ENTITY::IS_ENTITY_DEAD(shown.horse))) {
            LogEntry(shown, "shown pair lost; removing only this pair");
            shown.failure_reason="displayed actor lost; review incomplete";
            DeleteEntry(shown);
            shown.skipped = true;
            LabelEntry(shown);
            Notify(EntryLabel(shown));
        }
    }
    // A page with failed slots remains inspectable; absence is NOT a pass.
    if (g_phase != Phase::Visible && Clock::now() - g_started >
            std::chrono::seconds(120)) {
        Log("display preparation reached total time budget; preserving completed pairs");
        for (std::size_t i = g_cursor; i < PageEnd(); ++i) {
            DeleteEntry(g_entries[i]); g_entries[i].skipped = true;
            g_entries[i].failure_reason="page time budget exceeded; NOT REVIEWED";LabelEntry(g_entries[i]);
        }
        FinishDisplay(); return;
    }
    const double elapsed = std::chrono::duration<double>(Clock::now() - g_phase_started).count();
    if (g_phase == Phase::Planning) {
        if (Clock::now() < g_next_plan) return;
        g_next_plan = Clock::now() + std::chrono::milliseconds(50);
        if (!PlanEntry()) {
            if (elapsed > g_planning_timeout) SkipEntry("no local position: " + g_placement_failure);
            return;
        }
        const Entry& entry = g_entries[g_cursor];
        if(entry.donor_model && !ValidModel(entry.donor_model)) {
            SkipEntry("requested complete rider model unavailable; no identity substitution");return;
        }
        RequestModel(g_rider_model);
        RequestModel(entry.horse_model);
        if (entry.donor_model != 0) RequestModel(entry.donor_model);
        SetPhase(Phase::Loading);
        return;
    }
    if (g_phase == Phase::Loading) {
        Entry& entry = g_entries[g_cursor];
        if (!STREAMING::HAS_MODEL_LOADED(g_rider_model) || !STREAMING::HAS_MODEL_LOADED(entry.horse_model)) {
            if (elapsed >= 15.0) SkipEntry("rider/horse model streaming timed out");
            return;
        }
        if (entry.donor_model != 0 && !STREAMING::HAS_MODEL_LOADED(entry.donor_model)) {
            if (elapsed < 15.0) return;
            SkipEntry("requested complete rider model timed out; no identity substitution");return;
        }
        if (entry.donor_model != 0) { SetPhase(Phase::Creating); return; }
        if (!g_outfit_requested) {
            g_outfit_request = PED::_REQUEST_META_PED_OUTFIT(g_rider_model, g_site.rider_outfit);
            g_outfit_requested = true;
            LogEntry(entry, "requested original rider outfit=" + std::to_string(g_site.rider_outfit) +
                " request=" + std::to_string(g_outfit_request));
            return;
        }
        if (!PED::_IS_META_PED_OUTFIT_REQUEST_VALID(g_outfit_request)) { SkipEntry("original outfit request invalid"); return; }
        if (!PED::_HAS_META_PED_OUTFIT_LOADED(g_outfit_request)) {
            if (elapsed >= 15.0) SkipEntry("original outfit streaming timed out");
            return;
        }
        SetPhase(Phase::Creating);
        return;
    }
    if (g_phase == Phase::Creating) {
        Entry& entry = g_entries[g_cursor];
        ++entry.create_attempts;
        if (!CreateEntry(entry)) {
            LogEntry(entry, "creation attempt failed=" + std::to_string(entry.create_attempts));
            DeleteEntry(entry);
            ReleaseOutfit();
            if (entry.create_attempts >= 3) SkipEntry("three creation/location attempts failed");
            else { g_next_plan = {}; SetPhase(Phase::Planning); }
            return;
        }
        SetPhase(Phase::Settling);
        return; // At most one pair per tick; no blocking native loops.
    }
    if (g_phase != Phase::Visible) {
        Entry& entry = g_entries[g_cursor];
        if (!Exists(entry.rider) || !Exists(entry.horse) || ENTITY::IS_ENTITY_DEAD(entry.rider) ||
            ENTITY::IS_ENTITY_DEAD(entry.horse)) {
            SkipEntry("rider/horse lost while preparing pair"); return;
        }
    }
    if (g_phase == Phase::Settling) {
        Entry& entry = g_entries[g_cursor];
        const Vector3 position = ENTITY::GET_ENTITY_COORDS(entry.horse, true, false);
        const Vector3 velocity = ENTITY::GET_ENTITY_VELOCITY(entry.horse, 0);
        const bool stable = !ENTITY::IS_ENTITY_IN_AIR(entry.horse, 0) && !PED::IS_PED_FALLING(entry.horse) &&
            std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z) &&
            std::isfinite(velocity.x) && std::isfinite(velocity.y) && std::isfinite(velocity.z) &&
            std::abs(velocity.z) < 0.15F && velocity.x*velocity.x + velocity.y*velocity.y < 0.25F &&
            DistanceSquared(position, entry.position) < 9.0F;
        if (!stable) entry.grounded_since = {};
        else if (entry.grounded_since == Clock::time_point{}) entry.grounded_since = Clock::now();
        else if (Clock::now() - entry.grounded_since >= std::chrono::milliseconds(250)) {
            entry.position = position;
            ENTITY::FREEZE_ENTITY_POSITION(entry.horse, true);
            entry.grounded = true;
            LogEntry(entry, "horse grounded and stable");
            appearance::HidePair(entry.rider,entry.horse);
            SetPhase(Phase::Dressing);
            return;
        }
        if (elapsed > 8.0) {
            std::ostringstream detail;
            detail << "settling timeout in_air=" << ENTITY::IS_ENTITY_IN_AIR(entry.horse, 0) <<
                " falling=" << PED::IS_PED_FALLING(entry.horse) <<
                " position=" << position.x << ',' << position.y << ',' << position.z <<
                " velocity=" << velocity.x << ',' << velocity.y << ',' << velocity.z <<
                " displacement_squared=" << DistanceSquared(position, entry.position);
            LogEntry(entry, detail.str());
            SkipEntry("horse could not settle on ground");
        }
        return;
    }
    if (g_phase == Phase::Dressing) {
        Entry& entry = g_entries[g_cursor];
        if (!entry.appearance.active && (!PED::IS_PED_READY_TO_RENDER(entry.rider) || !PED::IS_PED_READY_TO_RENDER(entry.horse))) {
            if (elapsed >= 15.0) SkipEntry("rider/horse render readiness timed out");
            return;
        }
        if (g_site.all_rider_presets && !g_preset_count_known) {
            const int count=PED::GET_NUM_META_PED_OUTFITS(entry.rider);
            if(count <= 0) { if(elapsed >= 15.0) Clear("fixed host outfit count unavailable"); return; }
            if(count > 64 || static_cast<std::size_t>(count) <= g_cursor) {
                Clear("unexpected outfit count; refusing truncated or unbounded showcase"); return;
            }
            g_entries.resize(static_cast<std::size_t>(count)); // entry reference may be invalidated: return below.
            for(std::size_t i=0;i<g_entries.size();++i) {
                g_entries[i].horse_name=g_site.horse_model;
                g_entries[i].horse_model=MISC::GET_HASH_KEY(g_site.horse_model);
                g_entries[i].donor_name=g_site.rider_model;
                g_entries[i].donor_model=g_rider_model;
                g_entries[i].donor_preset=static_cast<int>(i);
            }
            g_preset_count_known=true;
            Log(std::string(g_site.rider_model)+" runtime outfit count="+std::to_string(count)+"; exact preset IDs 0.."+std::to_string(count-1));
            return;
        }
        if(!entry.appearance.active) {
            // Models with no indexed presets can still have a complete stock
            // appearance. This is an existing-outfit audit, not a fictitious #0.
            const bool named=appearance::FixedOutfit(g_site.character)!=0;
            const int preset=named?0:(entry.donor_model && PED::GET_NUM_META_PED_OUTFITS(entry.rider)>0?entry.donor_preset:-1);
            std::vector<int> pool;
            if(g_site.all_rider_presets) {
                const int count=named?0:PED::GET_NUM_META_PED_OUTFITS(entry.rider);
                pool=appearance::AllowedPool(g_paths.MainConfig().string(),g_site.character,count);
                if(pool.empty()){SkipEntry("same production outfit pool is invalid; NOT REVIEWED");return;}
                entry.excluded=std::find(pool.begin(),pool.end(),preset)==pool.end();
            } else if(preset>=0)pool={preset}; // Candidate male model, not a silent identity swap.
            entry.appearance.Begin(entry.rider,entry.horse,entry.donor_model?entry.donor_model:g_rider_model,
                entry.horse_model,preset,std::move(pool),appearance::Preferred(g_site.character),
                appearance::FixedOutfit(g_site.character)?-1:(g_site.female?1:0),Log,
                Clock::now(),appearance::FixedOutfit(g_site.character));
        }
        const auto status=entry.appearance.Tick();
        entry.actual_preset=entry.appearance.current;
        if(status==appearance::Status::Failed){SkipEntry(entry.appearance.issue);return;}
        if(status!=appearance::Status::Ready)return;
        entry.clothing_verified=true; // Shared native audit only, NEVER automatic visual approval.
        entry.clothing_changes=entry.appearance.rider_assets.count;
        entry.clothing_status=entry.appearance.failures.empty()?"exact; shared audit; visual review required":
            "requested preset FAILED; fallback shown; requested preset NOT approved";
        ShowEntry();return;
    }
    if (Clock::now() < g_next_label) return;
    g_next_label = Clock::now() + std::chrono::seconds(1);
    const Vector3 player_position = ENTITY::GET_ENTITY_COORDS(g_player, true, false);
    int nearest = -1;
    float best = 36.0F;
    for (std::size_t i = 0; i < g_entries.size(); ++i) {
        if (!g_entries[i].shown && !g_entries[i].blip) continue;
        const float distance = DistanceSquared(player_position, g_entries[i].position);
        if (distance < best) { best = distance; nearest = static_cast<int>(i); }
    }
    if (nearest >= 0 && nearest != g_last_label) {
        const Entry& entry = g_entries[static_cast<std::size_t>(nearest)];
        if(!entry.failure_reason.empty() || !entry.appearance.failures.empty()) {
            Notify(EntryLabel(entry)+" | "+PageLabel());g_last_label=nearest;return;
        }
        if(g_site.all_rider_presets) {
            Notify(EntryLabel(entry)+" | "+PageLabel());
            g_last_label=nearest; return;
        }
        Notify(std::string(i18n::Text("Test #", "展示 #")) + std::to_string(nearest + 1) + ": " + i18n::HorseName(entry.horse_name) +
            (entry.donor_model ? i18n::Text(" / complete male NPC, stock outfit", " / 完整男性 NPC 及原装服饰") :
                i18n::Text(" / original racer and outfit", " / 原生骑手及原装服饰")) +
            i18n::Text(" / stock NPC saddle", " / 原生 NPC 马鞍"));
    }
    g_last_label = nearest;
}
} // namespace horse_races::showcase
