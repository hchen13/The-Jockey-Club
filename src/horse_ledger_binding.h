#pragma once
#include "horse_save_observer.h"
#include <optional>
#include <limits>

namespace horse_races::horse_ledger::binding {
enum class Kind { LoadBegin, LoadCandidate, LoadFailed, PackRequested, WriteBegin, WriteCandidate, Uncertain, WriteRejected, SaveCaptureUncertain };
// SaveCaptureUncertain invalidates all save captures, not the loaded world.
// Uncertain still means world continuity cannot be established.
struct Action {
    Kind kind;
    std::uint32_t slot = std::numeric_limits<std::uint32_t>::max(), pack_counter = 0, disk_counter = 0;
    std::uint32_t observed_pack_counter = 0;
    std::uint64_t tick_ms = 0, request_tick_ms = 0, sequence = 0;
    std::string reason;
};
// Observed build-specific patterns only; candidates prove neither world readiness
// nor serializer freeze, stable disk revision, successful wallet save, or binding.
class Detector {
public:
    std::vector<Action> Consume(const save_observer::Observation&);
    void Reset();
private:
    struct Pending {
        std::uint32_t slot{}, operation{}, pack_counter{};
        std::uint64_t tick_ms{};
        bool busy{}, phase9{};
    };
    std::optional<save_observer::Observation> previous_;
    std::optional<Pending> load_, write_;
    bool load_blocked_{};
};
}
