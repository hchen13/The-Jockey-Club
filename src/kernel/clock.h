#pragma once
#include <chrono>
#include <compare>

// Two clocks that must never be mistaken for each other.
//
// Game time stops when the game is paused and when a loading screen is up; wall time does not. The
// mod has been bitten by the difference more than once - a deadline measured against the wall while
// the player was in the pause menu, a file poll measured against a clock that had stopped. Making
// them separate types means the compiler catches the mix-up instead of the user.
//
// The rule: anything the player experiences - a countdown, how long a horse has been stuck, how long
// a camp activity lasts - is GameTime. Only watching files, debouncing a setting and measuring our
// own frame cost use WallTime.
namespace horse_races::clock {

template <class Tag>
class Stamp {
public:
    constexpr Stamp() = default;
    constexpr explicit Stamp(double seconds) : seconds_(seconds) {}
    constexpr double Seconds() const { return seconds_; }
    constexpr auto operator<=>(const Stamp&) const = default;
    constexpr Stamp operator+(double seconds) const { return Stamp{seconds_ + seconds}; }
    constexpr Stamp operator-(double seconds) const { return Stamp{seconds_ - seconds}; }
    constexpr double operator-(const Stamp& other) const { return seconds_ - other.seconds_; }
    constexpr Stamp& operator+=(double seconds) { seconds_ += seconds; return *this; }
    // A default-constructed stamp is "never", which is in the past for every real reading.
    constexpr bool Known() const { return seconds_ != 0; }

private:
    double seconds_ = 0;
};

struct GameTag {};
struct WallTag {};
// Seconds since the mod started, frozen while the game is paused or loading.
using GameTime = Stamp<GameTag>;
// Seconds on the machine's own clock, which never stops.
using WallTime = Stamp<WallTag>;

static_assert(!std::is_convertible_v<GameTime, WallTime>, "the two clocks must not convert");

inline WallTime Wall() {
    using namespace std::chrono;
    static const auto start = steady_clock::now();
    return WallTime{duration<double>(steady_clock::now() - start).count()};
}

}  // namespace horse_races::clock
