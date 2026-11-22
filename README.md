# The Jockey Club

The Jockey Club 1.18.0 is a horse-racing mod for Red Dead Redemption 2 Story Mode on Windows. Ride out to challenge other riders, meet strangers on the trail, enter breed-restricted races, or watch the field as a spectator with a wager on the result. Clubhouse is the companion app for reviewing rides, comparing racing lines and managing settings.

Player downloads will be linked here when the Nexus Mods release is available.

Older archives also use version 1.18.0 for *HorseSpeedLab*, the project's earlier horse-speed plugin. The racing mod uses the filename `TheJockeyClub.asi`. The name `HorseSpeedLab.motion.v2` still appears in recordings so existing tools can read them.

## Source and player builds

This repository contains the project's racing and betting systems, route configurations, horse records, source for the Clubhouse launchers and dashboard, installer, selected offline tests, and the promotional trailer's editable timeline and bilingual scripts. See [the trailer project](branding/trailer/README.md).

The public build produces the Clubhouse launchers, analysis and settings tools, horse archive utilities, installer executable and standalone gameplay tests. A complete installable ASI and an assembled player installer require additional components. The omitted native adapters handle version-specific speed and physics integration, save observation and binding, literal HUD text, shop integration, and compatibility with an optional third-party mod. The source build also omits game-derived artwork, original game resources and the embedded Python distribution. The installer executable alone has no install payload. Re-rendering the trailer requires the editor’s game footage, approved opening sound and narration tracks.

## Building on Windows

Use Visual Studio 2022 with Desktop development with C++, a Windows SDK, and CMake 3.25 or later. Supply the separately obtained ScriptHookRDR2 SDK for the game types used by the course tools and offline tests. Its root contains the `inc` directory.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 "-DSCRIPHOOK_SDK_ROOT=C:/SDK/ScriptHookRDR2"
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

The C++ outputs are under `build/Release`. For dashboard development, use Python 3.11 or newer and run `python tools/race_analysis/dashboard_server.py --help` for the development server options. The default artwork in this source version uses system typography and the bundled Rye font. The [license scope](LICENSE-SCOPE.md) describes third-party dependencies and assets.
