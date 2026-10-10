# ReSkate Blood 1.0.0

Standalone impact spray, droplets, persistent blood trails and body smears, including walls and moving vehicles. Choose red, green, blue or pink, and adjust impact threshold, effect size, density, width, length, bleeding duration and lifetime.

## Install

1. In ReSkateLauncher, import this ZIP in Mods and enable it.
2. Close the launcher and game. Open the installed mod folder and run `Install.bat`. This backs up and replaces `ReSkate.dll` and `ReSkateLauncher.exe` beside `Skate.exe`.
3. Start the installed launcher. Open **Skater > Blood** and enable **Blood effects** (off by default).

ZIP import installs assets; `Install.bat` is required to activate the custom runtime. You can also extract the ZIP into `Mods/ReSkate_Blood` under your game's active Mods directory. The installer detects both ordinary Mods and ModData/Default/Mods locations, or accepts `powershell -File Manage.ps1 -Action Install -GameDirectory "C:/path/to/skate"`.

Requires Steam game build 25414733 and an existing ReSkate installation. The installer verifies the game and packaged files. This is a custom runtime build: its launcher has automatic binary updates disabled so stock updates do not remove Blood. It replaces other custom runtime builds, including the experimental Hall of Meat runtime. Disable any other copy of the Hall of Meat blood asset mod to avoid duplicate assets.

## Remove or update

To stop blood temporarily, turn off Blood effects. To remove or replace this package, close the game and launcher, run `Uninstall.bat` in the mod folder, then remove it from the launcher. Uninstall restores the original binaries. **Do not delete or replace the mod folder before uninstalling: it contains your backup.** The installer refuses to overwrite a runtime changed by another installation.

## Redistribution and source

Share the complete ZIP with its SHA-256 sidecar. `Source.zip` contains the corresponding tracked source, build instructions and dependency notices. ReSkate code is GPL-3.0; see LICENSE and THIRD-PARTY-NOTICES.md. Blood assets are generated from the supported game's asset formats and donors; no game executable is included.

Release validation: Release runtime/launcher build, blood model and asset-merge regression tests, compiled-asset merge against installed mods, ZIP integrity, and installer backup/restore tests. Final in-game appearance has not been reverified for this standalone branch.
