# Standalone blood

`feature/blood` ports the blood effects from `feature/hall-of-meat` (through `d69e2a9`) onto `main`.

Enable **Skater > Blood > Blood effects** after installing the blood asset mod. Blood is off by default. This page controls effect size, minimum impact damage, trail density, smear width and length, bleeding duration, mark lifetime, and red/green/blue/pink presets. Preferences use the independent `Blood.Options.v1` local-profile key.

Confirmed local-skater impacts create spray and attached droplets; bleeding body parts leave persistent drops and smears on supported ground, walls, and moving vehicles. The feature runs during normal skating and does not require Hall of Meat. It suspends during loading, replay editing, Noclip, and park editing. Native handles and the temporary decal draw allowance are released when disabled or the world changes.

Blood is a client-side effect for your own skater in solo and ReSkate multiplayer. The client tick resolves the current local player directly, and physics callbacks revalidate that same player's rig. Remote skaters cannot produce blood, and blood events are not added to multiplayer packets. The blood asset mod must be installed locally.

The asset package places blood and its dependencies in the common `DingoLevel_Root` bundle. Earlier packages used the San Vansterdam-only `BAM_CoreGameAssets` bundle: impacts were detected after joining a stadium server, but all blood assets disappeared when the city unloaded. Updating the DLL alone does not fix that package; rebuild/update the blood asset mod too. The merge verifier checks that all colors' effects and ground materials are present in the common root.

## Build and test

```powershell
cmake --preset vs2022-x64 -DDINGOSDK_BUILD_BLOOD_TESTS=ON -DDINGOSDK_BUILD_LAUNCHER_TESTS=ON
cmake --build build/vs2022-x64 --config Release --target dingosdk_runtime dingosdk_blood_tests dingosdk_blood_runtime_tests dingosdk_blood_asset_author dingosdk_mod_merge_added_assets_tests --parallel 4
ctest --test-dir build/vs2022-x64 -C Release -R '^(blood|blood_runtime|mod_merge_added_assets)$' --output-on-failure
```

Build the matching particle and decal assets using the supported game installation and ReSkate Studio CLI:

```powershell
./contrib/build-blood.ps1 -GameDirectory 'C:/path/to/skate' -StudioCli 'C:/path/to/reskate_cli.exe'
```

Back up the installed blood mod, replace its `Win32` directory with the newly built one, and copy the remaining contents of `artifacts/Blood/Patch` into the game's `Mods/ReSkate_Blood` directory. Use the newly built `ReSkate.dll`. Replacing `Win32` removes the older city-only bundle files. The compiled mod contains the shared-bundle declaration needed to carry its textures and shaders into maps. Private asset names and identities are preserved, but older Hall of Meat blood packages also need the common-root asset update for stadium multiplayer. Install one copy of the blood asset mod.

## Integration

`Extension/Blood` owns the contact detector, simulation, native handles, settings, tests, and asset tools. The detector retains the original native speed/rotation evidence, pre-bail contact retention, and contact-episode deduplication, with challenge scoring and timers removed. It automatically rearms on recovery.

The existing No Bail hooks provide read-only local-owner validation, contact capture before skeleton response, and the resulting physics pose afterward. Client tick publishes immutable scenes to the native particle/decal APIs. Level transitions invalidate the generation before scene teardown. The existing Hall of Meat implementation on `main` is unchanged; no Slam challenge, X-ray, sound, camera, replay-export, or manual-bail implementation is imported.

In-game verification: enable blood, fall and slide on a flat surface, compare low/high damage and smear settings, switch each color, and confirm wall/vehicle attachment. Disable/re-enable, recover and bail again, teleport, reload a map, open the replay/park editor, and use Noclip to check cleanup and rearming. Automated model tests cannot verify final rendered appearance.

Multiplayer verification requires a live host/guest session: fall locally with another player present, confirm local spray and trails, and confirm that the other player's falls produce no blood. Repeat after joining, leaving, respawning and changing maps. The `blood_runtime` test exercises the real runtime against synthetic physics memory and substitutes ownership/rendering functions; it verifies callback isolation and cleanup, not a live connection or native rendering. While enabled, `Blood:` entries in `logs/ReSkate.log` report availability, sampled physics steps, bail/contact/impact evidence and effect counts every five seconds. The Blood page also reports rejected physics samples instead of treating an idle renderer as proof that blood is working.

## Redistributable ZIP

Build the branded runtime and launcher with binary auto-updates disabled, then package the compiled assets:

```powershell
cmake --preset vs2022-x64 '-DDINGOSDK_VERSION=1.0.0-blood' '-DDINGOSDK_LAUNCHER_AUTO_UPDATE=OFF'
cmake --build build/vs2022-x64 --config Release --target dingosdk_runtime dingosdk_launcher --parallel 4
python contrib/pack-blood.py
./contrib/test-blood-package.ps1
```

The output is `artifacts/ReSkate_Blood-1.0.0.zip` plus its SHA-256 sidecar. Commit source changes before packaging: `Source.zip` contains the exact Git revision recorded in `build-info.json`. The package imports through the launcher; its `Install.bat` then activates the required custom runtime with a verified backup. Users must run `Uninstall.bat` before removing or updating the package.
