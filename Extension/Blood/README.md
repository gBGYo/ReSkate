# Standalone blood

`feature/blood` ports the blood effects from `feature/hall-of-meat` (through `d69e2a9`) onto `main`.

Enable **Skater > Blood > Blood effects** after installing the blood asset mod. Blood is off by default. This page controls effect size, minimum impact damage, trail density, smear width and length, bleeding duration, mark lifetime, and red/green/blue/pink presets. Preferences use the independent `Blood.Options.v1` local-profile key.

Confirmed local-skater impacts create spray and attached droplets; bleeding body parts leave persistent drops and smears on supported ground, walls, and moving vehicles. The feature runs during normal skating and does not require Hall of Meat. It suspends during loading, replay editing, Noclip, and park editing. Native handles and the temporary decal draw allowance are released when disabled or the world changes.

## Build and test

```powershell
cmake --preset vs2022-x64 -DDINGOSDK_BUILD_BLOOD_TESTS=ON -DDINGOSDK_BUILD_LAUNCHER_TESTS=ON
cmake --build build/vs2022-x64 --config Release --target dingosdk_runtime dingosdk_blood_tests dingosdk_blood_asset_author dingosdk_mod_merge_added_assets_tests --parallel 4
ctest --test-dir build/vs2022-x64 -C Release -R '^(blood|mod_merge_added_assets)$' --output-on-failure
```

Build the matching particle and decal assets using the supported game installation and ReSkate Studio CLI:

```powershell
./contrib/build-blood.ps1 -GameDirectory 'C:/path/to/skate' -StudioCli 'C:/path/to/reskate_cli.exe'
```

Copy the contents of `artifacts/Blood/Patch` into the game's `Mods/ReSkate_Blood` directory and use the newly built `ReSkate.dll`. The compiled mod contains the shared-bundle declaration needed to carry its textures and shaders into maps. Existing Hall of Meat blood assets remain compatible: their private asset names and identities are deliberately preserved. Install one copy of the blood asset mod.

## Integration

`Extension/Blood` owns the contact detector, simulation, native handles, settings, tests, and asset tools. The detector retains the original native speed/rotation evidence, pre-bail contact retention, and contact-episode deduplication, with challenge scoring and timers removed. It automatically rearms on recovery.

The existing No Bail hooks provide read-only local-owner validation, contact capture before skeleton response, and the resulting physics pose afterward. Client tick publishes immutable scenes to the native particle/decal APIs. Level transitions invalidate the generation before scene teardown. The existing Hall of Meat implementation on `main` is unchanged; no Slam challenge, X-ray, sound, camera, replay-export, or manual-bail implementation is imported.

In-game verification: enable blood, fall and slide on a flat surface, compare low/high damage and smear settings, switch each color, and confirm wall/vehicle attachment. Disable/re-enable, recover and bail again, teleport, reload a map, open the replay/park editor, and use Noclip to check cleanup and rearming. Automated model tests cannot verify final rendered appearance.
