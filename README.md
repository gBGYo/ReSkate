# ReSkate

play it offline, host your own lobbies and dedicated servers, and mod it.
the launcher, the runtime that loads into the game, and the dedicated server.

> ReSkate is a fan project. It is not affiliated with or endorsed by Electronic Arts or Full Circle.
> You need your own copy of skate. on Steam.

## Features

- **Offline play.** No EA servers needed. Your skater, outfits, unlocks and progress are saved on your PC,
  and the game runs even with Steam closed.
- **Multiplayer.**
  - Host a Steam lobby for up to 32 players: public, or joined with a code, with an optional password.
    Everyone in a lobby is in one party.
  - Join dedicated servers from the in-game server browser.
  - Proximity voice chat, and text chat with emotes and an optional bad-word filter.
  - Parties on dedicated servers: invite, join, leave, promote.
  - Throwdowns with other players (Jam, Spot Battle and S.K.A.T.E.), and co-op challenges.
- **In-game menu and console.**
  - Map and fast travel.
  - World: time of day, population, district levels, rotating parks.
  - The **Park Editor**: place, move and save objects with freecam, snapping and undo.
  - Skater options: first person, movement, boosts, noclip.
  - Offline **Slam Challenge** prototype: score falls and body impacts, with injury highlights and retries.
  - Progression, controls, graphics and multiplayer settings.
- **Mods.**
  - Drop a mod in `Mods/` and it is merged into the game at launch. Mods can add custom maps, loading
    screens, cosmetics and scripts.
  - Browse and install mods from the [Thunderstore community](https://thunderstore.io/c/reskate/) in the
    launcher.
  - Most mod changes apply in game without a restart.
- **Launcher.**
  - Checks that you have the supported game build, and can download exactly that build with your Steam
    account.
  - Keeps ReSkate itself up to date.

## Getting started

1. Download the latest `ReSkate-<version>.zip` from
   [Releases](https://github.com/Dingo-Shenanigans/ReSkate/releases).
2. Extract `ReSkateLauncher.exe` and `ReSkate.dll` into either folder:
   - **your skate. folder**, beside `Skate.exe` (Steam → skate. → Manage → Browse local files); or
   - **an empty folder**, where the launcher installs the game for you (about 14 GB).
3. Run `ReSkateLauncher.exe`.
   - It checks for ReSkate updates, then checks the game files against the supported build.
   - If the game is missing, or Steam has updated it past the supported build, sign in when asked. You
     can scan a QR code with the Steam app, or use your username and password with Steam Guard. It then
     downloads only the files it needs.
4. Press **PLAY**.

ReSkate supports one game build at a time (Steam build `25414733`).
### Controls

| Key | Opens |
|---|---|
| **Insert** | the ReSkate menu |
| **~** (grave/tilde) | the command console (`help` lists the commands) |
| **T** | chat, in multiplayer |

The menu and console keys can be changed in the launcher's Settings.

### Slam Challenge

Open **Skater → SLAM** in the ReSkate menu and choose **Start attempt**, then take a fall.
With **Manual bail** enabled, press **F8** to request a native wipeout.
It also works without an attempt when **X-ray in normal play** is on. **Skater → SLAM → BAIL CONTROLS**
records and saves a different keyboard key or controller chord; controller is initially unbound.
Release after menus, recovery or reconnecting before pressing again. Menu/console keys and controller
chords overlapping Noclip or velocity boosts are disabled. **Bail now** and `slam bail` request the
same transition. Manual bail requires offline play, No Bail, Noclip, Park Editor and First person off,
an owned local skater, and game focus. Manual bail requests the authored gameplay wipeout through its
exact condition graph, only when that graph's ContextKey resolves to the verified local skater.
The compiled output instructions verify the condition's byte binding and the weight's float binding.
Accepted gameplay delivery stops synthetic physics requests; unrelated scripts and players retain native results.
This removes the earlier animation delay: native ragdoll entry was measured 7–14ms after F8, versus
roughly 560ms through the previous physics-only trigger. Riding velocity comes from the owned board's
world-space rigid body, rather than facing or animation. At entry it transfers horizontal velocity once to the ragdoll trajectory
and bodies, preserving relative limb motion and native vertical/angular velocities. Constraints and
recovery remain native. Gameplay requests expire after 500ms; captured velocity has a separate
1.5-second limit and is discarded after entry, cancellation or an ownership change.
All five Slam test suites pass. Forward riding, fakie and running off board passed the live playtest:
bails start promptly and carry the existing direction of travel.
Additional airborne steering and ragdoll controls are still unfinished.
The HUD scores distinct impacts, fall distance, airtime and sliding. A skinned 3D X-ray mesh reuses the
installed game's **Dem Bones** skeleton, with an ivory-white surface blended with injury-color patches
and gradients. It highlights injured regions through the skater: orange for
bruises and red for fractures. Each region awards its
fracture bonus once per attempt. These are arcade injury scores; the mode does not alter the game's
ragdoll or damage the skater's bones.

Choose **Free slam**, **Score target**, **Impact chain**, **Fracture target**, **Big drop**, **Hang time**
or **Long slide** under **Skater → SLAM → CHOOSE A CHALLENGE**. Set a target or expand **Scoring rules**
to change impact and fracture rewards, fall/airtime/slide rates, chain bonus/window and fracture thresholds.
Settings save automatically and apply to the next attempt; an active attempt retains its original rules.
The HUD shows target progress and the current and best impact chain. Completed results show all six
score components, measured distances/times and injuries in each body region. Personal bests and completed
attempt counts are saved separately for each map, challenge, target and scoring rule set. Canceled attempts
do not record a score or personal best. A missed target still records the completed score.

After the round, recover, then choose **Retry from saved start** to return to the original attempt's
location and orientation, keeping its challenge and scoring rules. The next attempt begins after the
native teleport finishes and two distinct fresh physics samples confirm that pose. **Start new attempt here**
instead captures your current location and selected settings. **Cancel retry** stops waiting; a native teleport
already dispatched remains under the game's control. Retry waits at most five seconds for a busy teleport
manager and ten seconds for arrival. Loading, map/owner changes, another teleport and mode conflicts stop it.
The saved start lasts for this skater and map in the current session. **Cancel attempt** discards
the round; **Exit Slam** or **Dismiss results** hides its HUD. Console equivalents are `slam start`, `slam retry`, `slam bail`, `slam stop`,
`slam dismiss` and `slam status`.

The prototype requires offline play with No Bail, Noclip and the Park Editor off. Respawns, map changes,
teleports or lost physics telemetry cancel an active attempt. First person also blocks saved-start retry.
The challenge picker, target HUD, completed results and persisted personal bests passed a live playtest.
The new saved-start native return is implemented and covered by lifecycle tests; live validation is pending.
The X-ray mesh loads once in the background when an attempt or normal-play X-ray first opens. It reads
the player animation and native skinning buffers, including hands and corrective bones, and uses a
separate depth buffer for the skeleton's own surfaces. On the supported build, it captures the native
main rendering camera and skinning palette together rather than refreshing a CPU camera at overlay
draw time. A focused live playtest confirmed smooth alignment while running off board, skating,
turning and falling.
Under **Skater → SLAM → X-RAY**, choose whether the skeleton appears during attempts, after bailing,
after scored impacts, or stays off. Opacity, impact flash strength and the duration after impacts are
adjustable and saved automatically. **Reduced effects** keeps steady injury colors and removes flashes.
Enable **X-ray in normal play** under **Skater → CAMERA** or **Skater → SLAM → X-RAY** to use the
skeleton while skating without starting or resetting a Slam attempt. It follows successive falls,
automatically clears injury highlights after recovery, and does not open the Slam score HUD.
The same visibility settings apply; **During attempts** becomes **Always** in normal play.
Choose **Always** and disable **Only impacted bones** for a continuously visible full skeleton, or
choose **After impacts** with **Only impacted bones** for repeatable impact effects. The toggle is saved.
Enable **Show only fractured bones** under **Skater → SLAM → X-RAY** to show only red broken bones,
hiding yellow bruises and uninjured bones. It takes priority over **Only impacted bones**, saves
automatically and applies to attempts and normal-play X-ray. Turn it off to restore your previous display.
The **Bruise damage threshold**, **Head fracture damage threshold** and **Other bone fracture damage
threshold** sliders in **X-RAY** control when accumulated confirmed damage marks a bone yellow or red.
Higher values require more damage. Bruising starts at any positive damage by default; head and other
fracture thresholds remain 160 and 225. Damage below the bruise threshold stays unmarked and is hidden
by **Only impacted bones**. These are the same fracture rules used for scoring, fracture sounds and marks.
Settings save automatically and apply to new attempts and the next normal-play fall; a fall already in
progress retains its starting thresholds. Normal-play X-ray uses the selected damage rules too.
Normal-play X-ray works with No Bail enabled; Noclip, Park Editor, loading and online play pause it.
Enable **Only impacted bones** to hide untouched parts and reveal the body bones involved in detected
contacts. A forearm hit reveals the forearm separately from the hand or upper arm. Fingers follow their
hand collider. The native head-region bodies attach to Neck (101) and Neck1 (102); the latter now
highlights the skull (Head, 103), while both rendered neck joints follow the neck body. This explicit
anatomical attribution uses the captured body map and the installed render skeleton; the game does
not report individual finger or skull-bone contacts. Injuries and fractures update per body independently
of the 250ms region scoring cooldown, so rapid neck-to-head or forearm-to-hand hits reveal both parts.
Challenge scoring and fracture bonuses remain grouped into six body regions. A held contact can injure
once until observed separation, avoiding repeated damage from resting solver or animation changes.

Detection selects the stronger of the native record's two contact normals. It requires at least 2m/s
approach and 3m/s normal velocity change, with gravity removed. At an observed contact onset, the
native record's relative normal speed of at least 3m/s can also preserve an impact already slowed by
physics or animation. Confirmed body impacts while upright are retained for up to 200ms of simulation
time and applied only when a natural bail follows. Ordinary landings remain undamaged; manual bails,
missing telemetry, changed body identities and teleports cannot replay retained impacts. A ridden-board
landing can bruise both feet when native hard-landing cause 6 coincides with an upward support normal
and normal velocity loss on the same owned board, and a natural bail follows. Its arcade severity is
shared equally between the two feet. Loose-board collisions, ordinary landings, wall contacts and
board velocity changes without the native hard-landing cause cannot establish transmitted foot damage.
When three consecutive rigid-body
poses and one attributable world contact point are available, rotation supplements linear velocity
using the estimated velocity at that point. Missing poses, gaps, ambiguous two-direction points,
turns over 60 degrees per sample or remote points retain the linear path. Native angular-velocity and
impulse fields remain unverified. Brief contacts between samples, strikes without sufficient measured
deceleration and renewed hits without an observed separation can still be missed. These changes pass
automated regressions and installed-asset checks. In-game checks confirm skull impacts and damage to
both feet on hard skateboard landings that cause a bail, with upright landings remaining undamaged.
Rotating limb falls still need targeted in-game validation on the supported build.
Impact flashes affect the injured bones and fade once; they do not flash the whole screen. The mesh
adds a short bright hit pulse and optional persistent jagged fracture marks anchored to each bone's
geometry. A fresh fracture briefly opens the split before it settles; further contacts with that bone
do not restart the opening. Its position and angle vary between bones and falls, staying fixed after
that bone breaks. Cuts can occur anywhere from 10% to 90% along each bone's actual geometry,
leaving a small margin at either end.
Under **Skater → SLAM → SLAM EFFECTS**, **Impact sound** adds a thud for confirmed contacts.
**Bone cracking sound** adds separate dry splinter transients with four texture variants when a bone
first fractures. Both have independent switches and volume sliders. An optional **Custom bone crack
WAV** path replaces the generated crack with your own recording: PCM16, mono, 48 kHz, at most two
seconds. Enter the file path and press Enter; an empty path uses built-in cracks. Missing or unsupported
files fall back to built-in cracks and show a status message. Audio prepares in the background while
skating, and playback stops on recovery, menus or focus loss.
**Impact slow motion** slows severe hits and newly fractured bones, holds the configured speed,
then smoothly returns to the previous game speed. Speed, duration (0.15–3 seconds), hold fraction
and severity threshold are adjustable; further contacts cannot extend a running pulse. The new default
is 30% speed for 0.85 seconds, holding for 45% of that duration before recovering. Existing saved
durations and sound-off preferences are retained. Loading, online play, recovery, mode conflicts,
an open menu or focus loss restore the prior speed. A later player or engine speed change takes priority.
**Impact camera shake** adds a bounded 450ms punch-in and damped shake on severe hits or new fractures,
with adjustable strength. It applies to the submitted gameplay view and the skeleton together,
preserving the game's original camera state. **Zoom on player** independently eases the view toward
the ragdoll center during a successful slow-motion pulse, then restores native framing with the speed
recovery. **Zoom easing time** controls the approach (0.25 seconds by default). **Player follow smoothing**
adds damped tracking on the render clock, keeping movement continuous between physics samples;
higher values follow more gently. Impact shake is suppressed during player zoom. Its switch and zoom
amount are adjustable; framing turns are capped at 20 degrees and do not move the camera through
scenery. First person and Free camera pause camera effects.
**Pass-out fade** gradually darkens the view and closes its edges after a severe hit or new fracture,
then fades back. It triggers once per fall; recovery immediately clears it and rearms the next fall.
Its switch, strength, duration and severity threshold are configurable. **Pass-out redness** adds a deep
red tint to the darkening and vignette; zero restores black, with a subtle 0.15 tint by default.
The redness slider changes color independently of fade strength and timing. **Fade Slam HUD** also fades
the score interface, including its background, and can be disabled independently. Menus, loading,
focus loss, stale telemetry and mode conflicts clear the fade. These effects also work with normal-play
X-ray enabled, and their controls remain available when skeleton visibility is Off. Settings save automatically.
**Reduced effects** removes flashes, animated fracture opening, camera effects, automatic slow motion
and pass-out fading while retaining steady injury colors, fracture marks and sounds. These effects
are presentation; they do not split native ragdoll bones. Native time-scale sampling during repeated
live falls confirmed the earlier 30% pulse easing back to 100%. The new zoom, fade and crack sounds
pass automated regressions and compile in the runtime; their subjective feel still needs in-game validation.
The mesh hides in First person to keep the skull from covering the camera, while the challenge keeps scoring.
The current X-ray pass draws through scenery as well as the skater; scene occlusion remains part of
the full-mode work. No extracted game assets are shipped.

### Where things are

| What | Where |
|---|---|
| Mods | `Mods\<mod name>\` beside `Skate.exe`, ordered by `Mods\mods.json` |
| Logs | `logs\ReSkate.log` beside `Skate.exe` |
| Profile (skater, progress, parks) | `%LOCALAPPDATA%\ReSkate\profiles\offline\` |
| The game's own settings and saves | `%LOCALAPPDATA%\ReSkate\Game\` (kept apart from the normal game) |
| Launcher settings | `ReSkateLauncher.settings.json` beside the launcher |

## Mods

- Install mods from the launcher's **MODS** page: browse Thunderstore, or drag a `.zip` or folder onto the
  window.
- In game, the **MODS** tab of the ReSkate menu (**Insert**) turns mods on and off and applies the changes.
- Mods are checked against the game build they were made for. Outdated mods, or mods that can't be merged
  cleanly, are left out with a message naming them, and the rest still load.

Only install mods you trust. Mods change game data, and custom scripts can run code.

## Dedicated servers

`ReSkateServer.exe` is a headless lobby that needs neither the game nor Steam installed. It is in the
`ReSkateServer-<version>.zip` of each release. See [Server/README.txt](Server/README.txt) for setup,
`ReSkateServer.json`, admin commands, votes and the anti-cheat checks.

### Linux servers

Each release also ships `ReSkateServer-Linux-<version>.zip`: the same headless lobby as a native
x86_64 Linux binary (no Wine, no game install). Setup in short:

```sh
unzip ReSkateServer-Linux-<version>.zip -d reskate-server && cd reskate-server
./setup-linux-server-libs.sh
./ReSkateServer            # writes ReSkateServer.json on first run; edit name/admins, restart
```

Details (Steam `.so` files, `world-layers.json`, `systemd`, ports, building from source with
`cmake --preset linux-x64`) are in [Server/README-linux.md](Server/README-linux.md).

## Building from source

### Requirements

- Windows 10 or 11, x64.
- Visual Studio 2022 with the **Desktop development with C++** workload (MSVC v143 and a Windows SDK).
- CMake **3.24** or newer. Visual Studio's *C++ CMake tools for Windows* component provides one.

All third-party libraries are in the repository, so the build needs no package manager or network
access.

### Build

From a *Developer PowerShell for VS 2022* in the repository folder:

```powershell
cmake --preset vs2022-x64
cmake --build --preset release --parallel 4
```

The files are written to `build/vs2022-x64/Release/`:

| File | What it is |
|---|---|
| `ReSkateLauncher.exe` | the launcher (also hosts crash reporting) |
| `ReSkate.dll` | the runtime the launcher loads into the game |
| `ReSkateServer.exe` | the dedicated server |
| `ReSkateEmotePacker.exe` | builds the chat emote pack in `assets/emotes` |

To work in Visual Studio, open `build/vs2022-x64/DingoSDK.sln` after configuring and select
**Release | x64**.

### Running your build

Close the game. Copy `ReSkateLauncher.exe` and `ReSkate.dll` beside `Skate.exe`, then run the launcher.

Local builds never replace themselves. They only report that an update exists, so a release can't
overwrite the DLL you are testing.

Useful launcher flags:

| Flag | Effect |
|---|---|
| `--no-gui` | start the game straight away, without the launcher window |
| `--no-update` | skip the update check |
| `--offline` | play offline, without Steam running |
| `--windowed`, `--width=N`, `--height=N` | windowed mode and its size |
| `--log-level=<trace\|debug\|info\|warning\|error>` | how much `ReSkate.log` records |
| `--menu-key=0x2D`, `--console-key=0xC0` | menu and console keys (virtual-key codes) |
| `--no-loose-files` | ignore loose Lua and config files beside the game |
| `--gpu-diagnostics` | record extra detail when the graphics driver crashes (DRED) |

### CMake options

| Option | Default | Effect |
|---|---|---|
| `DINGOSDK_VERSION` | `0.0.0` | version stamped into the binaries |
| `DINGOSDK_LAUNCHER_AUTO_UPDATE` | `OFF` | let the launcher replace itself and `ReSkate.dll`, and the server replace itself (release builds) |
| `DINGOSDK_RELEASE_REPO` | `Dingo-Shenanigans/ReSkate` | public GitHub repository whose latest release carries `launcher.json` and the files it pins |
| `DINGOSDK_BACKTRACE_URL` | the project's endpoint | Backtrace minidump submission URL; empty turns crash uploads off |
| `DINGOSDK_TEST_GAME_ROOT` | empty | a Skate folder, for the tests that read real game data |

### Tests

Each module keeps its regression tests in a `Test/` folder. Turn them on with any of
`DINGOSDK_BUILD_MULTIPLAYER_TESTS`, `DINGOSDK_BUILD_LAUNCHER_TESTS`, `DINGOSDK_BUILD_PARK_EDITOR_TESTS`,
`DINGOSDK_BUILD_BACKTRACE_TESTS` and `DINGOSDK_BUILD_SLAM_TESTS`, then run CTest:

```powershell
cmake --preset vs2022-x64 -DDINGOSDK_BUILD_MULTIPLAYER_TESTS=ON -DDINGOSDK_BUILD_LAUNCHER_TESTS=ON
cmake --build --preset release --parallel 4
ctest --test-dir build/vs2022-x64 -C Release
```

The Slam suites also check impact timing, saved visual options and temporary speed ownership:

```powershell
cmake --preset vs2022-x64 -DDINGOSDK_BUILD_SLAM_TESTS=ON
cmake --build --preset release --target dingosdk_slam_tests dingosdk_slam_visuals_tests dingosdk_slam_settings_tests dingosdk_slam_bail_tests dingosdk_slam_retry_tests --parallel 4
ctest --test-dir build/vs2022-x64 -C Release --output-on-failure -R "^slam_"
```

The project builds with `/W4 /WX`: warnings are errors.

## Project layout

The source tree loosely follows Frostbite's own layout.

| Folder | Contents |
|---|---|
| `Engine/Core/` | logging, console, JSON, storage, platform helpers, Detours hooks, crash reporting (`Debug/`) |
| `Engine/Resource/` | Frostbite data formats: EBX, TOC, CAS, bundles, shader lookup |
| `Engine/Vfs/` | InitFS, the Mods folder, mod merging, the content cache |
| `Engine/Scripting/` | custom Lua scripts |
| `Engine/Game/` | the game's native ABI and data models |
| `Engine/Game/Build/<build>/` | every address, fingerprint and patch tied to one `Skate.exe` build, as `addr::<area>::<name>` |
| `Extension/` | ReSkate's features, the in-game UI and diagnostics |
| `Runtime/` | `ReSkate.dll`: entry point, startup and exports |
| `Launcher/` | `ReSkateLauncher.exe` |
| `Server/` | `ReSkateServer.exe` |
| `EmotePacker/` | `ReSkateEmotePacker.exe` |
| `External/` | third-party libraries, pinned and hashed (see [External/README.md](External/README.md)) |
| `assets/`, `config/` | files built into the binaries: launcher art, emotes, default profile settings |
| `cmake/` | the build, split by area |

### Supporting a new game build

Feature code never contains addresses. Everything tied to one `Skate.exe` lives in
`Engine/Game/Build/<build>/` and in the pins of
[supported_build.h](Engine/Game/Build/supported_build.h): file hashes, the Steam depot and manifest, and
the content cache. A new game build means a new table and new pins. Most hooks and patches check the
bytes they expect before touching them, so code from the wrong build is refused rather than patched.

## Crash reports and privacy

When the launcher or the game crashes, ReSkate uploads a minidump and that session's log to the
project's Backtrace account, so crashes can be fixed. The log never contains your Steam password or
login name.

To turn it off, untick **Send crash reports** in the launcher's Settings (ADVANCED), or set the
environment variable `RESKATE_CRASH_REPORTING=0`.

## Third-party code

ReSkate uses Dear ImGui, Microsoft Detours, RapidJSON, spdlog, SQLite, LZ4, Zstandard, miniz, bcdec and
Valve's networking headers, and the Montserrat and Permanent Marker fonts. Versions, licenses and local
patches are in [External/README.md](External/README.md). Every release ships their license files in
`licenses/`.

## License

Copyright © 2026 the ReSkate contributors.

ReSkate's source code is free software: you can redistribute it and modify it under the terms of the
[GNU General Public License, version 3](LICENSE). If you share a modified ReSkate, share its source
code under the same license.

The license covers ReSkate's own code. It does not cover:

- **Third-party libraries and fonts** in `External/`, which keep their own licenses (see
  [External/README.md](External/README.md)).
- **The chat emotes** in `assets/emotes/`, which were made by other people.
- **Images of the game** (`assets/launcher/background.jpg`, `assets/startup_splash.jpg`) and anything
  else from skate. The game and its content belong to Electronic Arts, and nothing here grants any
  rights to them.

## AI disclosure

Parts of ReSkate, including code, reverse-engineering notes and documentation, were written with the
help of AI coding assistants. The maintainers direct and test that work, but AI-written code can contain
mistakes like any other. If something looks wrong, please open an issue.
