#pragma once
#include <cstddef>
#include <string>
#include <vector>

// The camp's runtime (arthur_camp.cpp): ticked every frame from the plugin
// loop, reads <game>\TheJockeyClub\config\ArthurCamp.ini, builds and strikes the scene. The
// layout and every decision live in arthur_camp.h.
namespace horse_races::arthur_camp {
using LogFn=void (*)(const std::string&);
void Initialize(const std::string& directory,LogFn log);
// Call immediately after the ledger observes the world, before race/guard Tick.
// Pure bookkeeping: invalidates old actor protection without entity natives.
void ObserveWorld();
// F11's end/reset command shares the retained F10 camp rebuild behavior.
void Tick(bool end_activity=false);
bool Present();
// The activities in the layout, in order, and a request to go and do one now:
// he plays his way out of what he is doing first (F11 > Arthur's camp).
std::vector<std::string> ActivityNames();
void AskActivity(std::size_t index);
std::vector<std::string> MaryActivityNames();
void AskMaryActivity(std::size_t index);
// The map's clutter was learned once and lives in ArthurCamp.sweep. Whether
// anything is still being learned (nothing is), how many pieces the table
// holds, and a reading of the file again on request (F11).
bool SweepLearning();
std::size_t KnownPieces();
void RelearnSweep();
void Shutdown();
} // namespace horse_races::arthur_camp
