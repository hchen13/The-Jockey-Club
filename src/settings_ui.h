#pragma once
#include <string>
#include <cstddef>
#include <optional>
#include <vector>
#include "race_lap_view.h"
#include "race_system.h"
#include "race_progression.h"

namespace horse_settings {
void Initialize(const std::string& directory);
// A requested override change; opening/closing the menu never changes it.
std::optional<bool> Tick(bool speed_override);
bool Open();
// One-shot persisted setting change, also emitted once at initialization.
std::optional<bool> TakeCasualModeChange();
void SetShowcaseActive(bool active);
horse_races::ControlInput TakeControlInput();
// Temporary, non-persistent free-roam experiment: 0 off, 1 simulated input,
// 2 minimum move blend. Always starts off after a game restart.
int PlayerGaitExperimentMode();
// One-shot F11 capture; the plugin records current player coordinates and heading.
bool TakeCapturePositionRequest();
void SetProgressView(const std::string& summary,const std::string& status,const std::string& victories={});
// Called only after the requested operation has returned its actual result.
void PlayActionFeedback(bool success);
horse_races::progression::Action TakeProgressAction();
// One-shot F11 request; the plugin safely clears only the current protagonist's
// sale/membership records while retaining horse wins.
bool TakeMembershipReset();
// Result stays visible on the clear row until selection/session changes.
void SetMembershipResetStatus(const std::string& status);
void SetRaceLapView(const horse_races::circuit::View& view,bool bookmaker_owns_screen=false);
void SetCorridorView(const horse_races::CorridorView& view);
std::optional<horse_races::CorridorRequest> TakeCorridorRequest();
// F11 > Survey nearby scene was chosen: write TheJockeyClub-survey.txt once.
bool TakeSurveyRequest();
// F11 > Gait probe: whether the player's mount is being sampled to
// F11 > Arthur's camp > Scene recording: the switch as it stands; the
// plugin begins a recording when it goes on and ends it when it goes off.
bool SceneRecordingWanted();
// F11 > Race debug > Route recording: the switch as it stands; the plugin
// starts the recorder when it goes on, stops it when it goes off, and turns
// the switch off itself when the recorder stops on its own (a teleport).
bool RouteRecordingWanted();
void SetRouteRecording(bool on);
// F11 > Race debug > Auto survey: the state the row shows (pushed in by the
// plugin every tick) and the request to start or stop a run.
void SetAutoSurveyView(const horse_races::AutoSurveyState& state);
bool TakeAutoSurveyRequest();
// F11 > Arthur's camp > Object aiming: hold the right mouse button for a
// crosshair, the left one fires a ray at whatever is under it.
bool TakeAimRequest();
// F11 > Arthur's camp > Arthur: the activities to choose between (pushed in
// by the plugin from the camp's layout), and the one he has been asked to go
// and do now.
void SetCampActivities(const std::vector<std::string>& names);
std::optional<std::size_t> TakeCampActivityRequest();
void SetMaryActivities(const std::vector<std::string>& names);
std::optional<std::size_t> TakeMaryActivityRequest();
// F11 > Arthur's camp > clutter: what the sweep knows and whether it is still
// learning (pushed in by the plugin), and the request to learn again.
void SetCampSweep(bool learning,std::size_t known);
bool TakeCampSweepRequest();
} // namespace horse_settings
