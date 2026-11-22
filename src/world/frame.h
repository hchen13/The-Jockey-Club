#pragma once
#include "../kernel/clock.h"
#include <cstdint>

// What the game is doing this frame, read once.
//
// Today the same questions are asked over and over within one frame: fifteen calls to
// PLAYER_PED_ID, four separate places reading the keyboard, nine copies of "is the protagonist
// Arthur or John", and several different answers to "is the player on a mission" - some of which
// count OUR OWN mission flag and so think the player is busy because we made them busy. That last
// one has cost real bugs, so it is answered here and nowhere else.
//
// The snapshot is a value. Nothing in it can change under a module's feet halfway through a frame,
// which is the other half of the problem it solves.
namespace horse_races::world {

// The two playable models. Every other place that wanted to know asked with a bare hex literal.
inline constexpr std::uint32_t kArthurModel = 0x0D7114C9;
inline constexpr std::uint32_t kJohnModel = 0x00B69710;

enum class Protagonist : std::uint8_t { None, Arthur, John };

inline constexpr Protagonist ProtagonistOf(std::uint32_t model) {
    if (model == kArthurModel) return Protagonist::Arthur;
    if (model == kJohnModel) return Protagonist::John;
    return Protagonist::None;   // A custom player model: not a protagonist, and not an error.
}

// The keys the mod watches, as bits. F9 is deliberately absent: the showcase it opened moved into
// F11's debug page, and the key was freed (R-UI-17).
enum Key : std::uint32_t {
    KeyF10 = 1 << 0, KeyF11 = 1 << 1, KeyEnter = 1 << 2,
    KeyUp = 1 << 3, KeyDown = 1 << 4, KeyLeft = 1 << 5, KeyRight = 1 << 6,
    KeyRightMouse = 1 << 7, KeyLeftMouse = 1 << 8,
};

struct Frame {
    std::uint32_t index = 0;        // Frames since the mod started. Cadences are counted in these.
    clock::GameTime now;            // Stops with the game.
    clock::WallTime wall;           // Does not.
    float dt = 0;

    int player = 0, mount = 0;      // Ped handles; mount is 0 on foot.
    float x = 0, y = 0, z = 0;      // Where the player is.
    float speed = 0;                // Metres a second.

    Protagonist real = Protagonist::None;      // Who the player actually is. The camp asks this.
    Protagonist listener = Protagonist::None;  // Who the dialogue should address; F11 can override it.

    bool in_control = false;        // False during a cutscene, a fade, a stable's exit.
    bool faded = false;             // The screen is black right now.
    bool loading = false;
    bool paused = false;
    bool foreground = true;
    // The GAME's mission flag, with ours discounted. A race we are running sets the flag, and code
    // that reads the flag raw then concludes the player is unavailable - because of us.
    bool story_mission = false;

    std::uint32_t held = 0;         // Keys down this frame.
    std::uint32_t pressed = 0;      // Keys that went down this frame: the edge, computed once.

    bool Down(Key key) const { return (held & key) != 0; }
    bool Pressed(Key key) const { return (pressed & key) != 0; }
};

// The edge. Separated from the reading so it can be tested without a keyboard, and so that every
// module gets the SAME edge - four separate readers used to disagree about whether a press had been
// consumed, and a menu could eat a key the race was waiting for.
inline constexpr std::uint32_t Edges(std::uint32_t previous_held, std::uint32_t now_held) {
    return now_held & ~previous_held;
}

}  // namespace horse_races::world
