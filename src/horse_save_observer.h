#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include <optional>
#include <memory>
#include "horse_race_ledger.h"

namespace horse_races::save_observer {
struct Raw {
    std::uint32_t load_state{}, load_kind{}, load_phase{}, load_slot{}, pending{};
    std::uint32_t auto_state{}, auto_phase{}, operation{}, save_state{}, save_slot{};
    std::uint32_t pack_state{}, packed_bytes{}, disk_state{}, pack_counter{}, disk_counter{};
    std::array<char,22> filename{};
    bool operator==(const Raw&) const = default;
};
struct Observation {
    std::uint64_t sequence{}, tick_ms{};
    Raw raw{};
    bool healthy{};
    std::string issue;
};
// Script-fiber startup only. Pins the ASI for the remaining process lifetime:
// hot unloading after successful startup is intentionally unsupported.
// Unknown code/build disables this observer, without modifying game state.
// Success means worker startup only; baseline/idle never identifies a loaded save.
bool Start(const std::wstring& profile_directory);
Observation Latest();
std::vector<Observation> Drain();
// Synchronous stable observation, serialized with the worker and queued on change.
Observation ReadNow();
// In-memory, one-process evidence captured at a witnessed completed write.
// The game-save file is never read later to reconstruct a missed completion.
struct CompletedWriteEvidence {
    std::uint64_t sequence{},request_tick_ms{},terminal_tick_ms{};
    std::uint32_t slot{},pack_counter{},disk_counter{};
    std::wstring profile_directory;
    std::string sha256;
    std::uint64_t size{},write_filetime{},read_start_tick_ms{},read_end_tick_ms{};
    bool operator==(const CompletedWriteEvidence&)const=default;
};
std::optional<CompletedWriteEvidence> CompletedWrite(std::uint64_t sequence);
bool ValidateCompletedWrite(const CompletedWriteEvidence&);
void ReleaseCompletedWrite(std::uint64_t sequence);
// Immutable script-owned state handed to the background save publisher.
// expected_sequence is the last observation actually handled by the script.
bool OfferArchiveMirror(std::shared_ptr<const horse_ledger::Frozen>,
    std::uint64_t expected_sequence,std::uint64_t mutation_tick,const std::wstring& store_directory);
void RevokeArchiveMirror();
// Atomic signal only: never joins/waits, including when called from DllMain.
void RequestStop() noexcept;
}
