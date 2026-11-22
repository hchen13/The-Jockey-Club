#include "kernel/config.h"
#include "settings_ui.h"
#include "runtime_paths.h"
#include "bg_literal_text.h"
#include "race_localization.h"
#include "race_lap_hud.h"
#include "race_host_profiles.h"
#include "race_dialogue_test.h"
#include "race_opponent_difficulty.h"
#include "race_corridor_override.h"
#include "race_tracking_margin_config.h"
#include "race_cues.h"
#include "race_bgm_volume.h"
#include "race_catchup.h"
#include "horse_force_trial.h"
#include <windows.h>
#include "game_natives.h"
#include "game_clock.h"
#include <array>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace horse_settings {
namespace {
using Clock=horse_time::GameClock;
namespace tr = horse_races::i18n;
std::string ini;
bool menu_open = false, speed_visible = false;
bool game_font_markup = true;
bool native_lap_hud = true;
horse_races::lap_hud::Board lap_board;
horse_races::circuit::View race_laps;
bool bookmaker_lap_mask=false;
Clock::time_point font_setting_poll{};
int selected = 0;
// Named so the order on screen and the key handling cannot drift apart.
// Everything before kDebugFrom is a setting a player uses; everything from it
// on is an internal development tool, hidden from the release menu.
enum Row {RowSpeedOverride,RowShowSpeed,RowCasualMode,RowEncounterPage,RowDifficulty,RowCorridor,
          RowProgressPage,RowMembershipReset,RowEndActivity,
          RowTestHost,RowTestListener,RowShowcase,RowCapturePosition,RowRacePage,RowCampPage,RowMusicPage,kRows};
horse_races::ControlInput control_request;
std::optional<bool> casual_mode_change;
bool casual_mode_save_failed=false;
void ChangeCasualMode(){
    const bool next=!horse_races::catchup::CasualMode();
    casual_mode_save_failed=!horse_races::config::SetRaw(ini,"RaceAI","CasualMode",next);
    if(!casual_mode_save_failed){horse_races::catchup::SetCasualMode(next);casual_mode_change=next;}
    PlayActionFeedback(!casual_mode_save_failed);
}
bool encounter_page=false,encounter_map_save_failed=false;
void ChangeEncounterMap(){
    encounter_map_save_failed=!horse_races::SetRandomRaceWorldMapMarkers(!horse_races::RandomRaceWorldMapMarkers());
    PlayActionFeedback(!encounter_map_save_failed);
}
bool showcase_active=false;
// The two development sub-pages: what the snow trials and the camp need.
enum RaceRow {RaceStartRear,RaceStarterShot,RaceRouteRecord,RaceAutoSurvey,RaceTrialForce,kRaceRows};
enum CampRow {CampSurvey,CampRecording,CampAim,CampArthur,CampMary,CampSweep,kCampRows};
bool race_page=false,camp_page=false;
namespace music=horse_races::bgm::volume;
bool music_page=false,music_save_failed=false,music_saved=false;
int music_selected=0;
music::Values music_values{};
void ChangeMusic(bool restore,int step=0) {
    const auto group=static_cast<music::Group>(music_selected);
    const int next=restore?music::Default(group):std::clamp(music_values[music_selected]+step,0,100);
    music_save_failed=!music::Save(ini,group,next);
    music_saved=!music_save_failed;
    if(music_saved) {
        music_values[music_selected]=next;
        AUDIO::PLAY_SOUND_FRONTEND("MENU_ENTER","HUD_PLAYER_MENU",true,0);
    }
}
enum ProgressRow {ProgressReset,ProgressAll,ProgressNext,ProgressPrevious,kProgressRows};
bool progress_page=false;
int progress_selected=0;
int gait_experiment_mode=0;
bool capture_position_request=false;
horse_races::progression::Action progress_action=horse_races::progression::Action::None;
std::string progress_summary="--",progress_victories,progress_status;
Clock::time_point membership_reset_confirm_until{};
bool membership_reset_request=false;
std::string membership_reset_status;
int race_selected=0,camp_selected=0;
bool scene_recording=false;
bool route_recording=false;   // F11 > Race debug > Route recording: the switch; the plugin runs the recorder.
horse_races::AutoSurveyState auto_survey_state;   // F11 > Race debug > Auto survey: as the race system reports it.
bool auto_survey_request=false;                   // ENTER on that row: start or stop a run.
constexpr int kDebugFrom = RowTestHost;
// Release navigation exposes player settings only; developer actions remain internal.
constexpr int kPlayerRows = kDebugFrom;
namespace difficulty=horse_races::opponent_difficulty;
bool difficulty_page=false,difficulty_save_failed=false;
int difficulty_selected=0;
constexpr int kDifficultyRows=9;
// [RaceAI] CorneringEnvelopeMps2: the shared cornering envelope, the lateral
// acceleration a bend is taken at (the corner speed ceiling is the square
// root of envelope times bend radius). Shown as the default; each opponent's
// own value lives in their rule and is edited on this page.
int cornering_envelope=14;
int ReadCorneringEnvelope() {
    char value[32]{};GetPrivateProfileStringA("RaceAI","CorneringEnvelopeMps2","14",value,sizeof(value),ini.c_str());
    const int v=std::atoi(value);return v>=5 && v<=30?v:14;
}
std::size_t difficulty_opponent=0;
difficulty::Rule difficulty_rule;
void ChangeDifficulty(int step) {
    if(difficulty_selected==0) {
        difficulty_opponent=(difficulty_opponent+difficulty::kCount+step)%difficulty::kCount;
        difficulty_rule=difficulty::Read(ini,difficulty_opponent);difficulty_save_failed=false;return;
    }
    auto next=difficulty_rule;
    switch(difficulty_selected) {
    case 1:next.speed.enabled=!next.speed.enabled;break;
    case 2:next.speed.Edit(true,step);break;
    case 3:next.speed.Edit(false,step);break;
    case 4:next.acceleration.enabled=!next.acceleration.enabled;break;
    case 5:next.acceleration.Edit(true,step);break;
    case 6:next.acceleration.Edit(false,step);break;
    case 7:next=difficulty::Defaults(difficulty_opponent);break;
    case 8:next.cornering=std::clamp((next.cornering>0?next.cornering:cornering_envelope)+step,5,30);break;
    default:return;
    }
    difficulty_save_failed=!difficulty::Save(ini,difficulty_opponent,next);
    if(!difficulty_save_failed)difficulty_rule=next;
}
// The judged corridor page. This menu is the only writer of the width
// ([CorridorHalfWidth] <station key>) and of [UI] CorridorHighlight; the race
// system reads the file when it freezes a plan and follows the request pushed
// through TakeCorridorRequest for a live change. The view it pushes back says
// which station is selected and whether its rules judge a corridor at all.
namespace corridor=horse_races::corridor_override;
namespace tracking_margin=horse_races::tracking_margin;
bool corridor_page=false,corridor_save_failed=false,corridor_saved=false;
float corridor_tracking_margin=tracking_margin::kDefaultMetres;
int corridor_selected=0,corridor_highlight=0; // 0 off, 1 on.
int corridor_line_percent=100;               // [RaceAI] RacingLinePercent: 0 = centreline, 100 = full racing line.
int corridor_line_highlight=0;               // [UI] RacingLineHighlight: draw the computed line on the ground while racing.
horse_races::CorridorView corridor_view;
std::optional<horse_races::CorridorRequest> corridor_request;
bool survey_request=false; // F11 > Survey nearby scene: consumed by the plugin loop.
constexpr int kCorridorRows=7; // Highlight, course, half-width, reset, racing line, show line, tracking margin.
void ChangeCorridor(int step) {
    if(corridor_selected==6) {
        const float next=tracking_margin::Step(corridor_tracking_margin,step);
        corridor_save_failed=!tracking_margin::Save(ini,next);
        corridor_saved=!corridor_save_failed;
        if(corridor_saved) {
            corridor_tracking_margin=next;
            AUDIO::PLAY_SOUND_FRONTEND("MENU_ENTER","HUD_PLAYER_MENU",true,0);
        }
        // No live request: every rider snapshots the setting at the next race.
        return;
    }
    if(corridor_selected==0) {
        corridor_highlight=(corridor_highlight+2+step)%2;
        ::horse_races::config::SetRaw(ini.c_str(),"UI","CorridorHighlight",std::to_string(corridor_highlight).c_str());
        auto& request=corridor_request?*corridor_request:corridor_request.emplace();
        request.highlight=corridor_highlight;return;
    }
    if(corridor_selected==5) {
        corridor_line_highlight=(corridor_line_highlight+2+step)%2;
        ::horse_races::config::SetRaw(ini.c_str(),"UI","RacingLineHighlight",std::to_string(corridor_line_highlight).c_str());
        auto& request=corridor_request?*corridor_request:corridor_request.emplace();
        request.racing_line_highlight=corridor_line_highlight;return;
    }
    if(corridor_selected==4) {
        // The other difficulty knob: how much of the corridor the rivals use.
        // Global, not per course, and applied live (a restart hitch mid-race).
        corridor_line_percent=std::clamp(corridor_line_percent+5*step,0,100);
        corridor_save_failed=::horse_races::config::SetRaw(ini.c_str(),"RaceAI","RacingLinePercent",std::to_string(corridor_line_percent).c_str())==0;
        corridor_saved=!corridor_save_failed;
        if(corridor_save_failed)return;
        auto& request=corridor_request?*corridor_request:corridor_request.emplace();
        request.racing_line_percent=corridor_line_percent;return;
    }
    if(corridor_selected!=2 && corridor_selected!=3)return;
    // An open race has no corridor: nothing to edit, nothing written.
    if(!corridor_view.applicable || corridor_view.station_key.empty())return;
    const char* key=corridor_view.station_key.c_str();
    std::optional<float> next;
    if(corridor_selected==2)next=corridor::Step(corridor_view.effective,step);
    const bool saved=next?corridor::Save(ini,key,*next):corridor::Clear(ini,key);
    corridor_save_failed=!saved;corridor_saved=saved;
    if(!saved)return;
    corridor_view.override_width=next;
    corridor_view.effective=corridor::Effective(next,corridor_view.route_default);
    // Widening applies at once; a narrowing during a race is judged from the next one.
    corridor_view.deferred=corridor_view.racing && corridor_view.effective<corridor_view.judged;
    auto& request=corridor_request?*corridor_request:corridor_request.emplace();
    request.width_changed=true;request.width=next;
}
std::string test_host="Auto";
horse_races::dialogue_test::Listener test_listener=horse_races::dialogue_test::Listener::Auto;
void ChangeTestListener(int step) {
    test_listener=horse_races::dialogue_test::Step(test_listener,step);
    ::horse_races::config::SetRaw(ini.c_str(),"RacePrototype","TestDialoguePlayer",horse_races::dialogue_test::Name(test_listener));
}
void ChangeTestHost(int step) {
    // A test-only override. Production polling applies it at a safe boundary,
    // never by changing the identity or horse of an ongoing race instance.
    std::vector<std::string> options{"Auto"};
    const Hash player=ENTITY::GET_ENTITY_MODEL(PLAYER::PLAYER_PED_ID());
    for(const auto& p:horse_races::hosts::kProfiles)
        if(horse_races::hosts::AllowedPlayer(p,player,test_listener!=horse_races::dialogue_test::Listener::Auto))options.emplace_back(p.key);
    auto found=std::find(options.begin(),options.end(),test_host);
    const int index=found==options.end()?0:static_cast<int>(found-options.begin());
    test_host=options[(index+static_cast<int>(options.size())+step)%options.size()];
    ::horse_races::config::SetRaw(ini.c_str(),"RacePrototype","TestHostOverride",test_host.c_str());
}
constexpr std::array<int, 7> kKeys{VK_F11, VK_UP, VK_DOWN, VK_RETURN, VK_BACK,VK_LEFT,VK_RIGHT};
std::array<bool, kKeys.size()> was_down{};
float hud_x = 0.018F, hud_y = 0.955F, hud_scale = 0.24F;
Clock::time_point speed_sample{};
std::string speed_label;
Ped sampled_mount = 0;

float Setting(const char* key, const char* fallback, float lo, float hi)
{
    char value[64]{};
    GetPrivateProfileStringA("UI", key, fallback, value, sizeof(value), ini.c_str());
    try { const float f = std::stof(value); if (std::isfinite(f)) return std::clamp(f, lo, hi); } catch (...) {}
    return std::stof(fallback);
}
std::string HudFontText(const std::string& label)
{
    // Stock font aliases, not a bundled font: Font5 is also used by SUHM.
    // Keep non-ASCII UTF-8 runs on the localized body font instead of forcing
    // Chinese through the limited Latin face. This never changes the UI language.
    if (label.empty()) return {};
    // Match SUHM's complete live format: "~s~<FONT FACE='{0}'>{1}</FONT>".
    // A bare FONT wrapper (1.9.1/1.9.2) was printed literally by this game build.
    std::string formatted = "~s~";
    formatted.reserve(label.size() + 96);
    std::size_t offset = 0;
    while (offset < label.size()) {
        const bool latin = static_cast<unsigned char>(label[offset]) < 0x80;
        formatted += latin ? "<FONT FACE='$Font5'>" : "<FONT FACE='$body'>";
        do {
            const char ch = label[offset++];
            // Labels are text, never Scaleform markup.
            if (ch == '&') formatted += "&amp;";
            else if (ch == '<') formatted += "&lt;";
            else if (ch == '>') formatted += "&gt;";
            else formatted += ch;
        } while (offset < label.size() && (static_cast<unsigned char>(label[offset]) < 0x80) == latin);
        formatted += "</FONT>";
    }
    return formatted;
}
// SUHM's option rows, values and footers use $body (verified in installed ASI).
// Keep the same native markup, including UTF-8 and literal escaping.
std::string GameFontText(const std::string& label) {
    if(label.empty())return {};
    std::string out="~s~<FONT FACE='$body'>";
    for(char ch:label) {
        if(ch=='&')out+="&amp;";else if(ch=='<')out+="&lt;";
        else if(ch=='>')out+="&gt;";else out+=ch;
    }
    return out+"</FONT>";
}
void PollFontSetting()
{
    if (Clock::now() < font_setting_poll) return;
    font_setting_poll = Clock::now() + std::chrono::seconds(1);
    // Emergency plain-text fallback can be changed while testing, without a restart.
    game_font_markup = GetPrivateProfileIntA("UI", "UseGameFontMarkup", 1, ini.c_str()) != 0;
    // Optional emergency comparison/fallback, also reloadable without a restart.
    native_lap_hud = GetPrivateProfileIntA("UI", "NativeLapHud", 1, ini.c_str()) != 0;
    char selected_host[48]{};
    GetPrivateProfileStringA("RacePrototype","TestHostOverride","Auto",selected_host,sizeof(selected_host),ini.c_str());
    test_host=selected_host;
    char listener[24]{};
    GetPrivateProfileStringA("RacePrototype","TestDialoguePlayer","Auto",listener,sizeof(listener),ini.c_str());
    test_listener=horse_races::dialogue_test::Parse(listener);
}
void Text(const std::string& label, float x, float y, float scale, bool selected_row = false, bool hud = false)
{
    // SUHM uses these live BG natives, not the legacy HUD text natives (nullsubs
    // on current RDR2). Each draw selects its own font explicitly.
    const std::string formatted = game_font_markup ? (hud?HudFontText(label):GameFontText(label)) : label;
    UIDEBUG::_BG_SET_TEXT_SCALE(0.0F, scale);
    UIDEBUG::_BG_SET_TEXT_COLOR(selected_row ? 255 : 230, selected_row ? 245 : 230, selected_row ? 220 : 230, 255);
    horse_races::bg_text::Display(formatted, x, y);
}
bool Foreground()
{
    DWORD process{};
    GetWindowThreadProcessId(GetForegroundWindow(), &process);
    return process == GetCurrentProcessId();
}
bool aim_request=false;
bool aim_mode=false,aim_was_firing=false,aim_was_holding=false;
// The camp's activities, as its layout lists them, and the one chosen here.
std::vector<std::string> camp_activities,mary_activities;
std::size_t camp_activity=0,mary_activity=0;
bool camp_sweep_learning=false,camp_sweep_request=false;std::size_t camp_sweep_known=0;
std::optional<std::size_t> camp_activity_request,mary_activity_request;
// Object aiming: while the right mouse button is down a crosshair sits in the
// middle of the screen and no weapon comes out; the left button fires a ray
// down it, with a shutter click. There is no crosshair of the game's own with
// the menu up, and aiming a ray without one was guesswork (2026-09-16).
void DrawCrosshair() {
    constexpr float cx=.5F,cy=.5F;
    GRAPHICS::DRAW_RECT(cx,cy,.0016F,.0300F,240,232,210,225,false,true);  // the upright
    GRAPHICS::DRAW_RECT(cx,cy,.0098F,.0050F,240,232,210,225,false,true);  // the crossbar
    GRAPHICS::DRAW_RECT(cx,cy,.0026F,.0044F,180,40,30,255,false,true);    // the pip
}
bool WorldVisible();
void AimMode() {
    if(!aim_mode || !WorldVisible()){aim_was_firing=aim_was_holding=false;return;}
    const bool holding=(GetAsyncKeyState(VK_RBUTTON)&0x8000)!=0;
    if(!holding){aim_was_firing=false;aim_was_holding=false;return;}
    if(!aim_was_holding){aim_was_holding=true;AUDIO::PLAY_SOUND_FRONTEND("Zoom_In","CAMERA_SOUNDSET",true,0);}
    // No weapon, no shot, no lock-on while the crosshair is up.
    for(const char* action:{"INPUT_ATTACK","INPUT_ATTACK2","INPUT_AIM","INPUT_MELEE_ATTACK",
            "INPUT_INTERACT_LOCKON_POS","INPUT_INTERACT_LOCKON_NEG"})
        PAD::DISABLE_CONTROL_ACTION(0,MISC::GET_HASH_KEY(action),true);
    DrawCrosshair();
    const bool firing=(GetAsyncKeyState(VK_LBUTTON)&0x8000)!=0;
    if(firing && !aim_was_firing) {
        aim_request=true;
        AUDIO::PLAY_SOUND_FRONTEND("Craft_Items_Enter","SSCRFT_Sounds",true,0);
    }
    aim_was_firing=firing;
}
bool WorldVisible()
{
    const Ped player = PLAYER::PLAYER_PED_ID();
    return !NETWORK::NETWORK_IS_SESSION_ACTIVE() && !HUD::IS_PAUSE_MENU_ACTIVE() &&
        !CAM::IS_SCREEN_FADED_OUT() && !CAM::IS_CINEMATIC_CAM_RENDERING() && CAM::IS_GAMEPLAY_CAM_RENDERING() &&
        player && ENTITY::DOES_ENTITY_EXIST(player) && !ENTITY::IS_ENTITY_DEAD(player);
}
std::string LapPlayerName()
{
    const Hash model=ENTITY::GET_ENTITY_MODEL(PLAYER::PLAYER_PED_ID());
    const char* label=model==0x0D7114C9?"PLYR_ARTHUR":model==0x00B69710?"COMP_JOHN":nullptr;
    if(label) {
        const char* name=HUD::_GET_LABEL_TEXT_2(label);
        if(name && *name && std::string(name)!="NULL" && std::string(name)!=label)return name;
    }
    return tr::Text("You","你");
}
void LapHud()
{
    if(bookmaker_lap_mask){lap_board.Mask();return;}
    if(!race_laps.visible || race_laps.total<1 || HUD::IS_HUD_HIDDEN()) {lap_board.Close();return;}
    if(native_lap_hud) {
        if(lap_board.Show(race_laps,LapPlayerName(),race_laps.opponent_name,tr::Chinese()))return;
    } else lap_board.Close();
    // UI failure/conflict must never cancel a race or remove the standing.
    const bool chinese=tr::Chinese();
    Text(race_laps.PlayerRow(LapPlayerName(),chinese)+"   "+race_laps.Place(),0.43F,0.08F,0.32F,false,true);
    Text(race_laps.RivalRow(race_laps.opponent_name,chinese)+"   "+race_laps.Gap(),0.43F,0.112F,0.23F,false,true);
}
void Speedometer()
{
    if (!speed_visible || HUD::IS_HUD_HIDDEN() || HUD::IS_RADAR_HIDDEN() || HUD::IS_RADAR_HIDDEN_BY_SCRIPT()) return;
    const auto trial = horse_races::GetTrialObservedHorse();
    const Ped player = trial ? 0 : PLAYER::PLAYER_PED_ID();
    const Ped mount = trial ? *trial : PED::IS_PED_ON_MOUNT(player) ? PED::GET_MOUNT(player) : 0;
    if (!mount || !ENTITY::DOES_ENTITY_EXIST(mount) || ENTITY::IS_ENTITY_DEAD(mount)) {
        sampled_mount = 0; speed_label.clear(); return;
    }
    if (mount != sampled_mount || Clock::now() >= speed_sample) {
        sampled_mount = mount; speed_sample = Clock::now() + std::chrono::milliseconds(100);
        const Vector3 velocity = ENTITY::GET_ENTITY_VELOCITY(mount, 0);
        const float kmh = std::hypot(velocity.x, velocity.y) * 3.6F;
        std::ostringstream label;
        if (std::isfinite(kmh)) label << std::fixed << std::setprecision(1) << kmh;
        else label << "--";
        label << " km/h"; speed_label = label.str();
    }
    // Normalized placement is adjustable in INI for expanded radar/ultrawide.
    Text(speed_label, hud_x, hud_y, hud_scale,false,true);
}
#include "settings_ui_draw_menu.inl"
} // namespace

void Initialize(const std::string& directory)
{
    lap_board.Close();race_laps={};bookmaker_lap_mask=false;
    ini = horse_races::runtime_paths::Resolve(directory).MainConfig().string();
    horse_races::runtime_paths::EnsureDirectory(std::filesystem::path(ini).parent_path());
    font_setting_poll = {};
    PollFontSetting();
    speed_visible = GetPrivateProfileIntA("UI", "ShowSpeed", 0, ini.c_str()) != 0;
    casual_mode_change=horse_races::config::ReadInt(ini,"RaceAI","CasualMode",0,0,1)!=0;
    horse_races::catchup::SetCasualMode(*casual_mode_change);casual_mode_save_failed=false;
    encounter_page=false;encounter_map_save_failed=false;
    gait_experiment_mode=0;
    horse_motion::force_trial::probe.ResetSession();
    control_request={};showcase_active=false;
    capture_position_request=false;
    progress_action=horse_races::progression::Action::None;
    progress_summary="--";progress_victories.clear();progress_status.clear();
    progress_page=false;progress_selected=0;
    music_page=false;music_selected=0;music_saved=false;music_save_failed=false;
    membership_reset_confirm_until={};membership_reset_request=false;
    membership_reset_status.clear();
    corridor_highlight = std::clamp(static_cast<int>(GetPrivateProfileIntA("UI", "CorridorHighlight", 0, ini.c_str())), 0, 1);
    corridor_line_percent = std::clamp(static_cast<int>(GetPrivateProfileIntA("RaceAI", "RacingLinePercent", 100, ini.c_str())), 0, 100);
    corridor_line_highlight = std::clamp(static_cast<int>(GetPrivateProfileIntA("UI", "RacingLineHighlight", 0, ini.c_str())), 0, 1);
    hud_x = Setting("SpeedHudX", "0.018", 0.0F, 0.9F);
    hud_y = Setting("SpeedHudY", "0.955", 0.0F, 0.97F);
    hud_scale = Setting("SpeedHudScale", "0.24", 0.15F, 0.60F);
    for (std::size_t i = 0; i < kKeys.size(); ++i) was_down[i] = (GetAsyncKeyState(kKeys[i]) & 0x8000) != 0;
}
bool Open() { return menu_open; }
std::optional<bool> TakeCasualModeChange(){const auto value=casual_mode_change;casual_mode_change.reset();return value;}
void SetShowcaseActive(bool active){showcase_active=active;}
horse_races::ControlInput TakeControlInput(){
    auto result=control_request;control_request={};result.menu_open=menu_open;return result;
}
int PlayerGaitExperimentMode(){return gait_experiment_mode;}
bool TakeCapturePositionRequest(){const bool result=capture_position_request;capture_position_request=false;return result;}
void SetProgressView(const std::string& summary,const std::string& status,const std::string& victories){
    progress_summary=summary;progress_status=status;progress_victories=victories;
}
void PlayActionFeedback(bool success){
    AUDIO::PLAY_SOUND_FRONTEND(success?"SELECT":"NAV_ERROR","HUD_SHOP_SOUNDSET",true,0);
}
horse_races::progression::Action TakeProgressAction(){const auto result=progress_action;progress_action=horse_races::progression::Action::None;return result;}
bool TakeMembershipReset(){const bool result=membership_reset_request;membership_reset_request=false;return result;}
void SetMembershipResetStatus(const std::string& status){membership_reset_status=status;}
void SetRaceLapView(const horse_races::circuit::View& v,bool bookmaker_owns_screen){
    if(bookmaker_owns_screen){
        bookmaker_lap_mask=true;race_laps={};lap_board.Mask();return;
    }
    if(bookmaker_lap_mask){lap_board.Close();bookmaker_lap_mask=false;}
    race_laps=v;
    if(!v.visible || v.total<1)lap_board.Close();
}
void SetCorridorView(const horse_races::CorridorView& v){corridor_view=v;}
std::optional<horse_races::CorridorRequest> TakeCorridorRequest(){auto result=corridor_request;corridor_request.reset();return result;}
bool TakeSurveyRequest(){const bool result=survey_request;survey_request=false;return result;}
bool SceneRecordingWanted(){return scene_recording;}
bool RouteRecordingWanted(){return route_recording;}
void SetRouteRecording(bool on){route_recording=on;}
void SetAutoSurveyView(const horse_races::AutoSurveyState& state){auto_survey_state=state;}
bool TakeAutoSurveyRequest(){const bool r=auto_survey_request;auto_survey_request=false;return r;}
bool TakeAimRequest(){const bool result=aim_request;aim_request=false;return result;}
void SetCampActivities(const std::vector<std::string>& names) {
    if(names==camp_activities)return;
    camp_activities=names;
    if(camp_activity>=camp_activities.size())camp_activity=0;
}
std::optional<std::size_t> TakeCampActivityRequest() {
    const auto result=camp_activity_request;camp_activity_request.reset();return result;
}
void SetMaryActivities(const std::vector<std::string>& names) {
    if(names==mary_activities)return;
    mary_activities=names;
    if(mary_activity>=mary_activities.size())mary_activity=0;
}
std::optional<std::size_t> TakeMaryActivityRequest() {
    const auto result=mary_activity_request;mary_activity_request.reset();return result;
}
void SetCampSweep(bool learning,std::size_t known){camp_sweep_learning=learning;camp_sweep_known=known;}
bool TakeCampSweepRequest(){const bool result=camp_sweep_request;camp_sweep_request=false;return result;}
// One simple page: labels, values, a hint; UP/DOWN selects, ENTER acts.
#include "settings_ui_draw_pages.inl"

#include "settings_ui_input.inl"
std::optional<bool> Tick(bool speed_override)
{
    std::array<bool, kKeys.size()> pressed{};
    for (std::size_t i = 0; i < kKeys.size(); ++i) {
        const bool down = (GetAsyncKeyState(kKeys[i]) & 0x8000) != 0;
        pressed[i] = down && !was_down[i]; was_down[i] = down;
    }
    return HandleInput(speed_override, pressed, Foreground());
}
} // namespace horse_settings
