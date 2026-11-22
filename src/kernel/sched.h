#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

// Where the mod's time is handed out.
//
// Today every module decides for itself how often to run, with its own `if(now<next_)return;` and its
// own idea of what "often" costs. The result is measurable: the frame timer caught six different
// once-a-second jobs landing on the SAME frame, one of them 9 ms (2026-09-22). Nobody wrote that; it
// is what happens when twenty independent timers all start at zero.
//
// So a task declares its cadence and its budget and the scheduler decides when it runs:
//
//   * A cadence is counted in FRAMES, not seconds. Ten minutes in the pause menu must not make every
//     task in the mod come due on the same frame when the game resumes.
//   * Tasks of the same cadence are given different slots, so they land on different frames.
//   * Must runs when it is due, full stop: correctness does not live on leftovers. Spare runs only if
//     the frame has room, with a floor so it cannot starve for ever.
//   * When the low-frequency tasks due on one frame add up to more than the frame may spend, the ones
//     that do not fit wait a frame. A task that is late is better than a frame that is late.
namespace horse_races::sched {

// Which of the game's situations a task wants to run in. A task asks for none of them to mean "always".
enum When : std::uint32_t {
    Always = 1, HasRace = 2, Racing = 4, NearCamp = 8, MenuOpen = 16, DevActive = 32,
};

enum class Cls : std::uint8_t {
    Must,   // Runs at its cadence. Clearance at 4 Hz, the restore job at 2 Hz: correctness.
    Spare,  // Runs on what is left. The survey probes and the magenta line's vertex cache.
};

// The cadence is in frames, so this is the frame rate it is reckoned against. At half this the mod
// runs its slow tasks at half their nominal rate, which is the right way round: a machine that is
// struggling should not be asked to do more per frame.
inline constexpr float kNominalFps = 60;
// What all the low-frequency tasks on one frame may add up to before the rest wait a frame.
inline constexpr float kSlowBudgetMs = 0.30f;
// Spare work is never starved for longer than this many frames.
inline constexpr std::uint32_t kSpareFloorFrames = 4;

// How many frames apart two runs of this cadence are. 0 Hz means every frame.
inline constexpr std::uint32_t Period(float hz) {
    if (!(hz > 0)) return 1;
    const auto frames = static_cast<std::uint32_t>(std::lround(kNominalFps / hz));
    return frames < 1 ? 1u : frames;
}

// The one place a cadence is decided. `slot` is what keeps two tasks of the same cadence off the same
// frame; a module picks a slot once and keeps it.
inline constexpr bool Due(float hz, std::uint16_t slot, std::uint32_t frame) {
    const std::uint32_t period = Period(hz);
    return period == 1 || (frame + slot) % period == 0;
}

// The overload ladder, in the order the architecture says to climb it. Each step is a thing the mod
// stops doing; the player loses a debug overlay before they lose a frame.
enum class Ladder : std::uint8_t {
    None = 0,
    NoOverlays = 1,      // The magenta line and the corridor stop drawing.
    SlowTelemetry = 2,   // Telemetry's extra reads drop to 5 Hz; its fields carry a stale bit.
    CapRects = 3,        // The sixth core's rectangles are capped (they are capped at 96 anyway).
};

template <class Ctx>
struct Task {
    const char* name = "";
    void (*fn)(Ctx&) = nullptr;
    float hz = 0;                 // 0 = every frame.
    Cls cls = Cls::Must;
    float budget_ms = 0;          // What it is expected to cost when it runs.
    std::uint32_t when = Always;  // Which situations it wants.
    std::uint16_t slot = 0;       // Its phase within its cadence.
};

template <class Ctx>
class Sched {
public:
    void Add(const Task<Ctx>& task) { tasks_.push_back(task); }
    std::size_t Size() const { return tasks_.size(); }
    Ladder Rung() const { return rung_; }
    void SetRung(Ladder rung) { rung_ = rung; }

    // One frame. `situation` is the When bits that hold right now; `spare_ms` is what the frame has
    // left over for Spare work. Returns how many tasks ran, which is what the tests watch.
    int Run(Ctx& ctx, std::uint32_t frame, std::uint32_t situation, float spare_ms) {
        int ran = 0;
        float slow_spent = 0;
        for (std::size_t i = 0; i < tasks_.size(); ++i) {
            const Task<Ctx>& task = tasks_[i];
            if (!Wanted(task, situation)) continue;
            if (task.cls == Cls::Spare) continue;   // Taken below, after the Must work is accounted for.
            const bool waiting = Waiting(i);
            if (!waiting && !Due(task.hz, task.slot, frame)) continue;
            // Every-frame work is the frame's floor, not part of the slow budget. Nor is a task that
            // already waited once: deferring is for smoothing a frame, not for deciding what runs.
            // A task whose own cost is over the whole slow budget - the clearance sweep is 6-11 ms -
            // would otherwise be deferred on every frame for ever, behind any every-frame task, and
            // never run at all.
            if (task.hz > 0 && !waiting) {
                if (slow_spent + task.budget_ms > kSlowBudgetMs && slow_spent > 0) {
                    SetWaiting(i, true);   // It goes on the next frame, budget or no budget.
                    continue;
                }
                slow_spent += task.budget_ms;
            }
            SetWaiting(i, false);
            if (task.fn) task.fn(ctx);
            ++ran;
        }
        // Spare work: what the frame has left, with a floor so it is never starved outright.
        const bool floor_due = frame - last_spare_ >= kSpareFloorFrames;
        float left = std::max(spare_ms, floor_due ? 0.1f : 0.0f);
        for (std::size_t i = 0; i < tasks_.size(); ++i) {
            const Task<Ctx>& task = tasks_[i];
            if (task.cls != Cls::Spare) continue;
            if (!Wanted(task, situation)) continue;
            if (!Due(task.hz, task.slot, frame)) continue;
            if (task.budget_ms > left) continue;
            left -= task.budget_ms;
            last_spare_ = frame;
            if (task.fn) task.fn(ctx);
            ++ran;
        }
        return ran;
    }

private:
    // `Always` is a bit like any other, so a task that asks for Always AND something else still runs
    // everywhere: a mask is what the caller asked for, not a special case.
    static bool Wanted(const Task<Ctx>& task, std::uint32_t situation) {
        return (task.when & Always) || (task.when & situation);
    }
    bool Waiting(std::size_t i) const { return i < waiting_.size() && waiting_[i]; }
    void SetWaiting(std::size_t i, bool on) {
        if (waiting_.size() <= i) waiting_.resize(tasks_.size(), false);
        waiting_[i] = on;
    }

    std::vector<Task<Ctx>> tasks_;
    std::vector<bool> waiting_;
    std::uint32_t last_spare_ = 0;
    Ladder rung_ = Ladder::None;
};

}  // namespace horse_races::sched
