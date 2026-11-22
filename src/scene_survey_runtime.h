#pragma once
#include <string>

// The survey's natives (scene_survey.cpp). Begin starts one; Tick, called
// every frame, reads the existing entity pools and writes
// <game>\TheJockeyClub\diagnostics\TheJockeyClub-survey.txt. The menu's ordinary survey does not
// make closest-object queries: those can materialize map entities while
// diagnosing a streaming problem.
namespace horse_races::scene_survey {
using LogFn=void (*)(const std::string&);
// Radius in metres around the player. A survey already running is left alone.
// pin: detailed diagnostic mode: pin map entities and probe every known
// model. This may change map streaming; it is intentionally off in the menu.
void Begin(const std::string& directory,float radius,LogFn log,bool pin=false);
void Tick();
bool Running();
// A recording: from BeginRecording to EndRecording, every half second the
// objects, peds and vehicles within `radius` of the player are added to one
// list (each instance once), so a walk round a tent gathers everything in
// it. EndRecording writes <game>\TheJockeyClub\diagnostics\TheJockeyClub-record-<stamp>.txt.
// What the player is aiming at: a ray down the middle of the screen, carried
// on past each thing it meets, so everything standing along it is named -
// map geometry included, which no pool and no closest-object query will name
// (a tent's floor, the board a shelf is made of). In the order met, to the
// log and to <game>\TheJockeyClub\diagnostics\TheJockeyClub-aim.txt.
void ProbeAim(const std::string& directory,LogFn log);
void BeginRecording(const std::string& directory,float radius,LogFn log);
void RecordTick();
void EndRecording();
bool Recording();
} // namespace horse_races::scene_survey
