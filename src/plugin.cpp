#include <windows.h>
#include <mmsystem.h>

#include <main.h>
#include "file_stamp.h"
#include "game_natives.h"
#include "game_clock.h"
#include "log_sink.h"
#include "runtime_paths.h"
#include "script_fault.h"
#include <enums.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <map>
#include <string>
#include <vector>

#include "race_system.h"
#include "race_showcase.h"
#include "horse_ledger_runtime.h"
#include "horse_market_runtime.h"
#include "horse_save_observer.h"
#include "horse_save_startup_probe.h"
#include "bonus_horse_shop_runtime.h"
#include "suhm_runtime.h"
#include "race_map_textures.h"
#include "horse_native_vector_windows.h"
#include "horse_snow_grid_probe_windows.h"
#include "race_progression_runtime.h"
#include "race_progression_messages.h"
#include "race_progression_toast.h"
#include "race_bookmaker_runtime.h"
#include "race_bookmaking.h"
#include "race_breeds.h"
#include "race_host_boost.h"
#include "settings_ui.h"
#include "horse_attributes.h"
#include "horse_wheel_labels.h"
#include "bg_literal_text.h"
#include "horse_motion_native.h"
#include "player_mount_recovery.h"
#include "player_physics_handoff_windows.h"
#include "race_simulation_lease.h"
#include "player_stimulant.h"
#include "player_stimulant_native.h"
#include "player_speed_learning_native.h"
#include "stimulant_hud.h"
#include "arthur_camp_runtime.h"
#include "scene_survey_runtime.h"
#include "horse_motion_recording.h"
#include "route_recorder.h"
#include "race_localization.h"
#include "race_cues.h"
#include "kernel/files.h"

namespace {
using Clock=horse_time::GameClock;

HMODULE g_module{};
std::string g_directory;
horse_races::runtime_paths::Paths g_paths;
bool g_enabled = false;

float g_gait_target_desired_blend = 2.9F;
std::array<float, 10> g_auto_speed_targets = {44.00F, 46.04F, 48.31F, 50.79F, 53.50F,
                                              56.43F, 59.58F, 62.95F, 66.54F, 70.35F};
std::array<float, 4> g_auto_low_speed_blends = {2.41F, 2.55F, 2.68F, 2.84F};
std::array<float, 5> g_auto_high_speed_forces = {1.70F, 3.18F, 4.77F, 6.44F, 8.22F};
std::array<float, 4> g_auto_low_accel_ramp_seconds = {12.0F, 8.0F, 5.0F, 2.5F};
std::array<float, 5> g_auto_high_accel_responses = {0.01F, 0.02F, 0.04F, 0.08F, 0.15F};
std::array<float, 10> g_auto_force_ramp_seconds = {18.0F, 15.0F, 12.5F, 10.5F, 8.5F,
                                                   7.0F, 5.5F, 4.0F, 2.3F, 0.5F};
float g_auto_ramp_start_blend = 1.50F;
float g_auto_open_blend = 3.20F;
float g_auto_force_start_kmh = 40.0F;
float g_rider_gait_cap = 2.9F; // [AutoCurve] RiderGaitCap: the gait an AI racer may reach; the player's own is untouched.
// [AutoCurve] PlayerGaitByBonding: the gait the player's mount may reach follows
// the bond (the user's design, 2026-09-20). A rider's spurs ask 2.9 of a ridden
// horse (gait probe: median 2.81, p90 2.90, 3.0 seen only in passing); the
// gallop animation's full blend is 3.0, where a task-driven horse sits, worth
// 2 km/h at the same force. Bonding rank 0 and 1 (the game's raw rank) leave
// the horse native; rank 2 may reach 2.933, rank 3 2.967, rank 4 the full 3.0.
// The isolated 0.27.2 trial established that desired-blend and simulated-input
// writes are overwritten by the game (0% of frames at 3.0), while the native
// minimum-blend request reached 3.0 with a natural-looking gait in the user's
// ride (2026-09-24). Apply that request only during a real sprint and release
// it as soon as eligibility ends. Bonding still sets the target; this is not
// a stimulant effect.
void Log(const std::string& message);
bool g_player_gait_by_bonding = true;
struct PlayerGaitRelease {Ped mount=0;int rank=-1;float target=0;double reported=0;int frames=0,at_target=0;double blend_sum=0;bool minimum_owned=false;};
PlayerGaitRelease g_player_gait;
float PlayerGaitTarget(int rank) {
    return rank<=1?0.0F:rank==2?2.933F:rank==3?2.967F:3.0F;
}
void ReleasePlayerGaitMinimum() {
    auto& g=g_player_gait;
    if(g.minimum_owned && g.mount && ENTITY::DOES_ENTITY_EXIST(g.mount))
        PED::SET_PED_MIN_MOVE_BLEND_RATIO(g.mount,0.0F);
    g.minimum_owned=false;
}
void TickPlayerGaitRelease(Ped mount,const horse_motion::NativeFrame& f,double now,bool active)
{
    auto& g=g_player_gait;
    if(!g_player_gait_by_bonding || !active || !mount || !ENTITY::DOES_ENTITY_EXIST(mount)){
        ReleasePlayerGaitMinimum();g={};return;
    }
    if(g.mount!=mount){ReleasePlayerGaitMinimum();g={};g.mount=mount;}
    const int rank=std::clamp(ATTRIBUTE::GET_ATTRIBUTE_RANK(mount,7),0,4);
    if(rank!=g.rank) {
        g.rank=rank;g.target=PlayerGaitTarget(rank);g.reported=now;g.frames=g.at_target=0;g.blend_sum=0;
        char b[160];
        if(g.target>0)std::snprintf(b,sizeof b,"player gait by bonding: mount bonding rank %d, the sprint may reach gait %.3f",rank,static_cast<double>(g.target));
        else std::snprintf(b,sizeof b,"player gait by bonding: mount bonding rank %d, native gait (2.9)",rank);
        Log(b);
    }
    // Keep F11's isolated probes separate from production control.
    if(!(g.target>0) || !std::isfinite(now) || horse_settings::PlayerGaitExperimentMode()!=0 ||
        !PAD::IS_CONTROL_PRESSED(0,INPUT_HORSE_SPRINT) || f.input.speed_kmh<40.0F ||
        f.input.braking_requested || f.input.desired_blend<2.5F){
        ReleasePlayerGaitMinimum();return;
    }
    const float desired=TASK::GET_PED_DESIRED_MOVE_BLEND_RATIO(mount);
    if(std::isfinite(desired) && desired>=2.5F) {
        PED::SET_PED_MIN_MOVE_BLEND_RATIO(mount,g.target);
        g.minimum_owned=true;
    } else {ReleasePlayerGaitMinimum();return;}
    if(f.current_blend_valid){++g.frames;g.blend_sum+=f.current_blend_y;if(f.current_blend_y>=g.target-.01F)++g.at_target;}
    if(now-g.reported>=10 && g.frames>0) {
        char b[200];
        std::snprintf(b,sizeof b,"player gait by bonding: target %.3f, sprint blend mean %.3f, at target %d%% of %d frames",
            static_cast<double>(g.target),g.blend_sum/g.frames,100*g.at_target/g.frames,g.frames);
        Log(b);g.reported=now;g.frames=g.at_target=0;g.blend_sum=0;
    }
}
struct PlayerGaitExperiment {
    Ped mount=0;
    int mode=0,frames=0,at_target=0;
    double reported=0,blend_sum=0,desired_before_sum=0,desired_after_sum=0,speed_sum=0;
};
PlayerGaitExperiment g_gait_experiment;
const char* GaitExperimentName(int mode){return mode==1?"input":mode==2?"minimum-blend":"off";}
void ReleasePlayerGaitExperiment(const char* reason) {
    auto& g=g_gait_experiment;
    if(!g.mode)return;
    if(g.mode==1)PLAYER::RESET_PLAYER_INPUT_GAIT(PLAYER::PLAYER_ID());
    if(g.mode==2 && g.mount && ENTITY::DOES_ENTITY_EXIST(g.mount))
        PED::SET_PED_MIN_MOVE_BLEND_RATIO(g.mount,0.0F);
    Log(std::string("player gait experiment: released ")+GaitExperimentName(g.mode)+" ("+reason+")");
    g={};
}
void TickPlayerGaitExperiment(Ped player,Ped mount,const horse_motion::NativeFrame* frame,double now,bool eligible) {
    auto& g=g_gait_experiment;
    const int wanted=horse_settings::PlayerGaitExperimentMode();
    const float target=mount && frame?PlayerGaitTarget(std::clamp(ATTRIBUTE::GET_ATTRIBUTE_RANK(mount,7),0,4)):0.0F;
    const bool running=wanted>0 && eligible && frame && frame->valid && frame->current_blend_valid &&
        !frame->input.environment.unsafe && !frame->input.environment.in_water &&
        !frame->input.braking_requested && frame->input.speed_kmh>=40.0F &&
        frame->input.desired_blend>=2.5F && target>0 &&
        PAD::IS_CONTROL_PRESSED(0,INPUT_HORSE_SPRINT);
    if(g.mode && (!running || g.mode!=wanted || g.mount!=mount))
        ReleasePlayerGaitExperiment(g.mode!=wanted?"mode changed":g.mount!=mount?"mount changed":"sprint/control ended");
    if(!running)return;
    if(!g.mode){
        g.mode=wanted;g.mount=mount;g.reported=now;
        Log(std::string("player gait experiment: started ")+GaitExperimentName(wanted)+
            " target="+std::to_string(target)+" (free roam only)");
    }
    if(wanted==1)
        PLAYER::SIMULATE_PLAYER_INPUT_GAIT(PLAYER::PLAYER_ID(),target,100,
            ENTITY::GET_ENTITY_HEADING(player),false,false);
    else
        PED::SET_PED_MIN_MOVE_BLEND_RATIO(mount,target);
    // Observe happened before this write. These samples therefore include the
    // engine's next update, not merely the value this script just requested.
    if(frame->input.speed_kmh>=40.0F){
        ++g.frames;g.blend_sum+=frame->current_blend_y;
        g.desired_before_sum+=frame->input.desired_blend;
        g.desired_after_sum+=TASK::GET_PED_DESIRED_MOVE_BLEND_RATIO(mount);
        g.speed_sum+=frame->input.speed_kmh;
        if(frame->current_blend_y>=target-0.01F)++g.at_target;
    }
    if(std::isfinite(now) && now-g.reported>=8.0 && g.frames>0){
        char b[256];
        std::snprintf(b,sizeof b,"player gait experiment: mode=%s target=%.3f current=%.3f at-target=%d%% samples=%d desired-before=%.3f desired-after=%.3f speed=%.1f km/h",
            GaitExperimentName(g.mode),static_cast<double>(target),g.blend_sum/g.frames,
            100*g.at_target/g.frames,g.frames,g.desired_before_sum/g.frames,
            g.desired_after_sum/g.frames,g.speed_sum/g.frames);
        Log(b);g.reported=now;g.frames=g.at_target=0;
        g.blend_sum=g.desired_before_sum=g.desired_after_sum=g.speed_sum=0;
    }
}
float g_rider_force_offset = 0; // [AutoCurve] RiderForceOffset: force taken from an AI racer ridden at the sprint gait instead.
// [Stimulant]: a horse stimulant used during a race is the player's burst (player_stimulant.h).
horse_races::stimulant::Config g_stimulant_config;
horse_races::stimulant::Tracker g_stimulant;
horse_motion::player_speed_learning::NativeSampler g_player_speed_learning;
horse_races::stimulant::DoseDetector g_stimulant_detection;
horse_races::stimulant::NativeSample g_stimulant_observed;
bool g_stimulant_observed_reported=false;
horse_races::stimulant::Counts g_stimulant_counts{-1,-1,-1};
bool g_stimulant_counts_reported=false;
bool g_stimulant_reported=false;
// [StimulantHud]: the sixth core, the burst's gauge (stimulant_hud.h).
horse_races::stimulant_hud::Config g_stimulant_hud;
// Hysteresis around the force gate. reset <= hold <= start is enforced on load;
// setting hold to the start speed and the delay to zero restores the old edge.
float g_auto_force_hold_kmh = 33.0F, g_auto_force_reset_kmh = 30.0F;
float g_auto_force_reset_seconds = 1.0F;
int g_auto_speed_level = 5;
int g_auto_accel_level = 5;
// The speed the curve is aiming the player's mount at, read off its displayed
// speed level; the two audit lines below quote it. Level 5 is vanilla, where
// the levels above start out.
float g_metrics_target_kmh = g_auto_speed_targets[4];
Ped g_auto_mount = 0;
horse_motion::Config g_motion_config;
horse_motion::NativeController g_player_motion, g_opponent_motion;
horse_motion::RacePhysicsKeeper g_player_physics, g_opponent_physics;
horse_motion::RaceSimulationLease g_race_simulation;
// Sized for the largest field a route may declare, not for one race.
horse_races::host_boost::ScopedGate<horse_motion::Identity> g_host_boost;
bool g_host_boost_reported=false;
horse_motion::ExemptionRefusal g_exemption_reported=horse_motion::ExemptionRefusal::None;
bool g_reins_held_reported=false;
std::array<horse_motion::NativeController,9> g_extra_motion;
std::array<horse_races::host_boost::ScopedGate<horse_motion::Identity>,9> g_bookmaker_bursts;
std::array<bool,9> g_bookmaker_burst_reported{};
std::array<horse_motion::RacePhysicsKeeper,9> g_extra_physics;
horse_motion::Recorder g_motion_recorder;
std::string g_last_recording_path;
Ped g_race_curve_mount = 0;
int g_race_curve_speed_level = 5;
int g_race_curve_accel_level = 5;

Clock::time_point g_next_attribute_refresh{};
std::vector<std::uint8_t> g_sound_on;
std::vector<std::uint8_t> g_sound_off;

std::string ModuleDirectory()
{
    std::array<char, MAX_PATH> path{};
    const DWORD length = GetModuleFileNameA(g_module, path.data(), static_cast<DWORD>(path.size()));
    std::string result(path.data(), length);
    const auto slash = result.find_last_of("\\/");
    return slash == std::string::npos ? "." : result.substr(0, slash);
}

void Log(const std::string& message)
{
    horse_races::log_sink::Shared().Write(message);
}

std::string LoadString(const char* section, const char* key, const char* fallback)
{
    const std::string ini = g_paths.MainConfig().string();
    std::array<char, 2048> value{};
    GetPrivateProfileStringA(section, key, fallback, value.data(), static_cast<DWORD>(value.size()), ini.c_str());
    return value.data();
}

float LoadFloat(
    const char* section, const char* key, const char* fallback, float minimum, float maximum, float default_value)
{
    const std::string text = LoadString(section, key, fallback);
    try {
        const float parsed = std::stof(text);
        if (std::isfinite(parsed) && parsed >= minimum && parsed <= maximum) {
            return parsed;
        }
    } catch (...) {
    }
    Log(std::string("invalid ") + key + "; using " + fallback);
    return default_value;
}

void WriteU16(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint16_t value)
{
    bytes[offset] = static_cast<std::uint8_t>(value & 0xFF);
    bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
}

void WriteU32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value)
{
    for (int byte = 0; byte < 4; ++byte) {
        bytes[offset + byte] = static_cast<std::uint8_t>((value >> (byte * 8)) & 0xFF);
    }
}

std::vector<std::uint8_t> MakeCue(bool enabled)
{
    constexpr std::uint32_t sample_rate = 22'050;
    constexpr double duration = 0.34;
    constexpr double pi = 3.14159265358979323846;
    const std::uint32_t sample_count = static_cast<std::uint32_t>(sample_rate * duration);
    const std::uint32_t data_size = sample_count * sizeof(std::int16_t);
    std::vector<std::uint8_t> wav(44 + data_size, 0);
    std::copy_n(reinterpret_cast<const std::uint8_t*>("RIFF"), 4, wav.begin());
    WriteU32(wav, 4, 36 + data_size);
    std::copy_n(reinterpret_cast<const std::uint8_t*>("WAVEfmt "), 8, wav.begin() + 8);
    WriteU32(wav, 16, 16);
    WriteU16(wav, 20, 1);
    WriteU16(wav, 22, 1);
    WriteU32(wav, 24, sample_rate);
    WriteU32(wav, 28, sample_rate * 2);
    WriteU16(wav, 32, 2);
    WriteU16(wav, 34, 16);
    std::copy_n(reinterpret_cast<const std::uint8_t*>("data"), 4, wav.begin() + 36);
    WriteU32(wav, 40, data_size);

    double phase = 0.0;
    for (std::uint32_t index = 0; index < sample_count; ++index) {
        const double seconds = static_cast<double>(index) / sample_rate;
        const bool second_tone = seconds >= 0.17;
        const double frequency = enabled ? (second_tone ? 1'320.0 : 880.0) : (second_tone ? 392.0 : 660.0);
        phase += 2.0 * pi * frequency / sample_rate;
        const double local = std::fmod(seconds, 0.17);
        const double fade = std::min({1.0, local / 0.012, (0.17 - local) / 0.012});
        const auto sample = static_cast<std::int16_t>(std::sin(phase) * 9'000.0 * std::max(0.0, fade));
        WriteU16(wav, 44 + index * 2, static_cast<std::uint16_t>(sample));
    }
    return wav;
}

void SoundOn()
{
    PlaySoundA(reinterpret_cast<LPCSTR>(g_sound_on.data()), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}

void SoundOff()
{
    PlaySoundA(reinterpret_cast<LPCSTR>(g_sound_off.data()), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}

void LogMountAttributes()
{
    const Ped player = PLAYER::PLAYER_PED_ID();
    if (!PED::IS_PED_ON_MOUNT(player)) {
        Log("mount attributes: player is not mounted");
        return;
    }
    const Ped mount = PED::GET_MOUNT(player);
    constexpr int speed_attribute = 5;
    constexpr int acceleration_attribute = 6;
    Log("mount attributes: speed rank=" + std::to_string(ATTRIBUTE::GET_ATTRIBUTE_RANK(mount, speed_attribute)) +
        " base=" + std::to_string(ATTRIBUTE::GET_ATTRIBUTE_BASE_RANK(mount, speed_attribute)) +
        " bonus=" + std::to_string(ATTRIBUTE::GET_ATTRIBUTE_BONUS_RANK(mount, speed_attribute)) +
        " max=" + std::to_string(ATTRIBUTE::GET_MAX_ATTRIBUTE_RANK(mount, speed_attribute)) +
        "; acceleration rank=" +
        std::to_string(ATTRIBUTE::GET_ATTRIBUTE_RANK(mount, acceleration_attribute)) +
        " base=" + std::to_string(ATTRIBUTE::GET_ATTRIBUTE_BASE_RANK(mount, acceleration_attribute)) +
        " bonus=" + std::to_string(ATTRIBUTE::GET_ATTRIBUTE_BONUS_RANK(mount, acceleration_attribute)) +
        " max=" + std::to_string(ATTRIBUTE::GET_MAX_ATTRIBUTE_RANK(mount, acceleration_attribute)));
}

int DisplayedAttributeLevel(Ped mount, int attribute)
{
    const int raw=ATTRIBUTE::GET_ATTRIBUTE_RANK(mount,attribute);
    return raw>=0 && raw<=9?raw+1:0;
}

void SelectAutoMount(Ped mount)
{
    constexpr int speed_attribute = 5;
    constexpr int acceleration_attribute = 6;
    const int displayed_speed_level = DisplayedAttributeLevel(mount, speed_attribute);
    const int displayed_accel_level = DisplayedAttributeLevel(mount, acceleration_attribute);
    g_next_attribute_refresh = Clock::now() + std::chrono::milliseconds(500);
    if (mount == g_auto_mount && displayed_speed_level == g_auto_speed_level &&
        displayed_accel_level == g_auto_accel_level) {
        return;
    }
    g_auto_mount = mount;
    g_auto_speed_level = displayed_speed_level;
    g_auto_accel_level = displayed_accel_level;
    g_metrics_target_kmh = g_auto_speed_level>=1 && g_auto_speed_level<=10 ?
        g_auto_speed_targets[static_cast<std::size_t>(g_auto_speed_level - 1)] : 0;
    // This is a throttled audit/UI cache only. NativeController resets on the
    // actual per-frame identity/rank change, identically for both racers.
    Log("AutoCurve selected mount: displayed speed=" + std::to_string(displayed_speed_level) +
        " acceleration=" + std::to_string(displayed_accel_level) +
        " target=" + std::to_string(g_metrics_target_kmh) + " km/h");
    const auto attributes = horse_attributes::Read(mount);
    Log("player attribute audit horse=" + std::to_string(mount) + " model=" + std::to_string(attributes.model) +
        " raw modelDefault=" + horse_attributes::Pair(attributes.model_default) +
        " current=" + horse_attributes::Pair(attributes.current) + " entityBase=" +
        horse_attributes::Pair(attributes.entity_base) + " bonus=" + horse_attributes::Pair(attributes.bonus) +
        "; player controller still uses final current rank, not model default");
}

void ResetRaceOpponentCurve()
{
    g_opponent_motion.Reset();
    g_race_curve_mount=0;
    g_race_curve_speed_level=g_race_curve_accel_level=5;
}

void RefreshMotionConfig()
{
    g_motion_config.low_speed_blends=g_auto_low_speed_blends;
    g_motion_config.high_speed_forces=g_auto_high_speed_forces;
    g_motion_config.low_accel_seconds=g_auto_low_accel_ramp_seconds;
    g_motion_config.high_accel_responses=g_auto_high_accel_responses;
    g_motion_config.force_ramp_seconds=g_auto_force_ramp_seconds;
    g_motion_config.ramp_start_blend=g_auto_ramp_start_blend;
    g_motion_config.open_blend=g_auto_open_blend;
    g_motion_config.desired_blend=g_gait_target_desired_blend;
    g_motion_config.force_start_kmh=g_auto_force_start_kmh;
    // Clamp rather than reject: a nonsensical pair must not silently disable
    // the gate, and the ordering is what makes the hysteresis a hysteresis.
    g_motion_config.force_hold_kmh=std::min(g_auto_force_hold_kmh,g_auto_force_start_kmh);
    g_motion_config.force_reset_kmh=std::min(g_auto_force_reset_kmh,g_motion_config.force_hold_kmh);
    g_motion_config.force_reset_seconds=g_auto_force_reset_seconds;
}

horse_races::route_recorder::Recorder g_route_recorder;
#include "plugin_recording_stimulant_hud.inl"
bool RepairMountCollision(Ped player,Ped mount,bool control_ready,double now,bool racing,const Vector3* position)
{
    const auto r=g_mount_recovery.Tick(player,mount,now,control_ready,position,horse_motion::player_physics::RecoverPlayer);
    if(r.started || r.recovered || (r.requested && (r.attempts<=3 || r.attempts%10==0)))
        Log(std::string("player mount physics recovery: ")+(r.recovered?"verified across engine frames":
            r.started?(r.teleported?"coordinate jump; extra force held during placement":"collision lost; extra force held"):
                "native simulation hand-back checked; NOT yet verified")+
            " horse="+std::to_string(mount)+" racing="+std::to_string(racing)+
            " collision_disabled="+std::to_string(r.collision_disabled)+" static="+std::to_string(r.is_static)+
            " world_ready="+std::to_string(r.world_ready)+" attempts="+std::to_string(r.attempts)+
            " handoff_status="+std::to_string(static_cast<int>(r.handoff.status))+
            " mode_before="+std::to_string(r.handoff.before.mode)+" mode_after="+std::to_string(r.handoff.after.mode)+
            " forced="+std::to_string(r.handoff.before.forced)+
            " physics_flags="+std::to_string(r.handoff.before.flags)+
            " physics_flags40c="+std::to_string(r.handoff.before.flags40c));
    return r.pending;
}
horse_races::host_boost::Reason HostBoostReason(const horse_races::MotionContext& context,
    const horse_motion::NativeFrame& frame,Ped player_mount,bool enabled)
{
    horse_races::host_boost::Conditions conditions;
    conditions.racing=enabled && context.racing && !context.eliminated;
    conditions.actor_valid=context.present && context.horse!=player_mount && frame.identity.Valid();
    conditions.frame_valid=frame.valid;
    conditions.pace_braking=(context.npc_braking || !context.host_boost_eligible) && !context.npc_override_intent;
    conditions.unsafe=frame.input.environment.unsafe;
    conditions.water=frame.input.environment.in_water;
    conditions.gait_valid=frame.current_blend_valid && std::isfinite(frame.current_blend_y);
    // A positive active race override is the rider's continuing intent; gait
    // duration is checked separately using the actual current blend.
    conditions.native_fast=context.npc_override_intent || (frame.valid && std::isfinite(frame.input.desired_blend) &&
        frame.input.desired_blend>=horse_motion::kNpcFastRequestBlend);
    return horse_races::host_boost::Classify(conditions);
}
#include "plugin_motion_tick.inl"

#include "plugin_bootstrap.inl"

std::wstring StartupProbeProfile(const wchar_t* ini)
{
    wchar_t profile[64]{};
    GetPrivateProfileStringW(L"HorseLedger",L"Profile",L"",profile,64,ini);
    return profile;
}
void ScriptMain()
{
    horse_races::save_startup_probe::ScriptMainArrived();
    horse_diagnostics::ConfigureFaultLog(g_module);
    // Other ASIs' faults are written beside ours; registered once per process.
    static bool observing=false;
    if(!observing){observing=true;AddVectoredExceptionHandler(0,horse_diagnostics::ForeignFault);}
    __try { RunScript(); }
    __except(horse_diagnostics::RecordFault(GetExceptionInformation())) { /* Filter always continues search. */ }
}
} // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = module;
        DisableThreadLibraryCalls(module);
        scriptRegister(module, ScriptMain);
        horse_races::save_startup_probe::Queue(module,StartupProbeProfile);
    } else if (reason == DLL_PROCESS_DETACH) {
        horse_races::save_startup_probe::RequestStop();
        horse_races::save_observer::RequestStop();
        horse_motion::native_vector::Uninstall();
        scriptUnregister(module);
    }
    return TRUE;
}
