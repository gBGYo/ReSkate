# ReSkate Hall of Meat

An offline Hall of Meat prototype in ReSkate's **Skater → SLAM** menu. Score falls,
body impacts and fractures, see injuries on an X-ray skeleton, and retry from your
saved starting position. Built for Skate Steam build **25414733**, Windows x64.

## Install

1. Install ReSkate normally and run it once.
2. Drag `ReSkate_HallOfMeat-0.1.0.zip` onto the ReSkate launcher, or install it
   from **MODS**. You can also extract it into `Skate\Mods\ReSkate_HallOfMeat`.
3. Close the game and launcher.
4. Open the installed mod folder and run **Install.bat**. It saves the current
   `ReSkate.dll` and `ReSkateLauncher.exe` in `backup`, then installs this build.
5. Start `ReSkateLauncher.exe`, press **Insert**, open **Skater → SLAM**, choose
   a challenge and select **Start attempt**.

The launcher places files in Mods; the batch file activates the runtime build.
Disabling this entry in MODS does not restore the previous runtime. Close the game
and launcher and run **Uninstall.bat** to restore both backed-up files.
Keep the mod folder and its backup until you have uninstalled.

(Thanks https://github.com/andrewnakas/reskate-trainer for the installation steps)

## What you get

- Free slam, score target, impact chain, fracture target, big drop, hang time and
  long slide challenges, with adjustable targets and scoring rules.
- Impact, chain, fracture, fall, airtime and slide score breakdowns, with personal
  bests saved separately by map, challenge, target and scoring rules.
- **Bail now**, configurable keyboard/controller bail controls, and **Retry from
  saved start**. Console commands: `slam start`, `slam retry`, `slam bail`,
  `slam stop`, `slam dismiss`, `slam status`.
- X-ray skeleton with per-body injury colors, fractured-bone filtering, damage
  thresholds and fracture marks. **X-ray in normal play** also works without an attempt.
- Impact/fracture sounds, impact camera effects, temporary slow motion and pass-out
  fading. **Reduced effects** keeps steady injury colors, marks and sounds.

## Compatibility and limits

This package replaces the two ReSkate binaries, like ReSkate Trainer 0.1.3. It
contains the Hall of Meat branch, **not the trainer's TRAINER page**. Runtime
replacement mods do not combine: if another build is installed afterward, remove
that build first before uninstalling Hall of Meat. The uninstaller refuses to
overwrite files changed by another build.

Uninstall before replacing/updating this mod through the launcher: replacing its
folder can remove the backup. This launcher's automatic binary updates are off;
automatic crash uploads are also off. Ordinary asset/map mods still use Mods.

Challenges require an offline local skater. Loading, online play, Noclip and Park
Editor interrupt the mode; No Bail must be disabled for an attempt. X-ray hides
in first person and currently draws through scenery. Fractures are arcade injury
scores and visual effects; they do not break the game's native ragdoll bones.
The new zoom, fade and crack-sound effects still need subjective in-game validation.

The Dem Bones mesh is read from the user's installed game. No extracted game data
or Steam DLLs are included. ReSkate settings and personal bests remain after uninstall.

Based on ReSkate. Not affiliated with Electronic Arts or the ReSkate developers.
