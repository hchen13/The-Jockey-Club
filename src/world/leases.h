#pragma once
#include <functional>
#include <vector>
#include <utility>

// Something taken from the world that has to be given back.
//
// The mod borrows a great deal: it blacks out the screen, sets the game's mission flag, turns off
// random trains, freezes the player's horse, writes a temporary speed rank onto an animal, holds a
// closure volume over a road. Every one of those has to be undone - on a normal finish, on an abort,
// on a save being loaded, on the mod being switched off in the menu - and today each is undone by
// hand in several places. The code review counted sixty-six abort sites, five separate reset lists
// and seven end-of-race sequences, and the bugs are the ones you would expect: a blackout that was
// never lifted and left the player staring at a black screen until they killed the game.
//
// A lease is the borrowing. It gives back when it is destroyed, so "clean up after a race" becomes
// "destroy the race" and there is no list to keep in step.
//
// Two ways of giving back:
//   Normal   - the ordinary one. Put the world back as it was.
//   Abandon  - a save has been loaded, or the world went away underneath us. The entities and
//              volumes we were holding belong to a world that no longer exists, so touching them by
//              handle would touch whatever is in those slots now. Forget rather than restore.
namespace horse_races::world {

enum class Release { Normal, Abandon };

// One borrowed thing. `give_back` is called once, with the mode, unless the lease was released
// early or never took anything.
class Lease {
public:
    Lease() = default;
    explicit Lease(std::function<void(Release)> give_back) : give_back_(std::move(give_back)) {}
    Lease(const Lease&) = delete;
    Lease& operator=(const Lease&) = delete;
    Lease(Lease&& other) noexcept : give_back_(std::move(other.give_back_)) { other.give_back_ = nullptr; }
    Lease& operator=(Lease&& other) noexcept {
        if (this != &other) {
            Give(Release::Normal);
            give_back_ = std::move(other.give_back_);
            other.give_back_ = nullptr;
        }
        return *this;
    }
    ~Lease() { Give(Release::Normal); }

    bool Held() const { return static_cast<bool>(give_back_); }
    // Gives it back now, in this mode. Doing it twice does nothing the second time, which is what
    // makes "give everything back, then destroy" safe.
    void Give(Release mode) {
        if (!give_back_) return;
        auto give = std::move(give_back_);
        give_back_ = nullptr;
        give(mode);
    }
    // The world is gone; drop the claim without touching it.
    void Forget() { give_back_ = nullptr; }

private:
    std::function<void(Release)> give_back_;
};

// Everything one owner has borrowed. Destroying it gives all of them back, last taken first - the
// order a reader expects, and the order that matters when one borrowing rests on another (a freeze
// on an entity that a blackout was covering).
class Leases {
public:
    Leases() = default;
    Leases(const Leases&) = delete;
    Leases& operator=(const Leases&) = delete;
    ~Leases() { GiveAll(Release::Normal); }

    void Take(Lease&& lease) {
        if (lease.Held()) held_.push_back(std::move(lease));
    }
    std::size_t Count() const {
        std::size_t live = 0;
        for (const auto& lease : held_) live += lease.Held() ? 1 : 0;
        return live;
    }
    void GiveAll(Release mode) {
        while (!held_.empty()) {
            held_.back().Give(mode);
            held_.pop_back();
        }
    }

private:
    std::vector<Lease> held_;
};

}  // namespace horse_races::world
