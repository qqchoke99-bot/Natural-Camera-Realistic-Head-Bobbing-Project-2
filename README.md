# CameraOverhaul

Cinematic camera for Minecraft Bedrock on Android ([LeviLauncher](https://github.com/LiteLDev/LeviLaunchroid)).

Pitch when you accelerate or fall, roll when you turn, sway when idle.

## Install

1. Install LeviLauncher.
2. Import the `.levipack` that matches your game build:
   - `CameraOverhaul-1.26.50.levipack` for Minecraft 1.26.50
   - `CameraOverhaul-1.26.62.levipack` for Minecraft 1.26.62
3. Enable CameraOverhaul and launch that Minecraft version.

Each package is compiled for exactly one game build: the signature patterns and
the component offsets are pinned at build time (`mcver` below). A package built
for another build finds no signatures, so the mod stays completely inert — it
does not hook anything and cannot apply offsets from the wrong version. Install
one package at a time; both share the module id.

## Supported game versions

Everything build-specific lives in one table:
[`include/cameraoverhaul/memory/GameProfiles.hpp`](include/cameraoverhaul/memory/GameProfiles.hpp).

| Profile | Game build | Status |
|---|---|---|
| `1.26.50` | 1.26.50.1 | verified |
| `1.26.62` | 1.26.62.1 | patterns copied from 1.26.50, pending scan of the 1.26.62 library |

### Diagnostics

`adb logcat -s CameraOverhaul` reports which profile was compiled in, the
address of each resolved signature, and whether the actor offsets look sane. If
the offsets resolve but read implausible values, the module disables the camera
effects and logs why.

### Adding a new game version

1. Copy `libminecraftpe.so` out of the matching APK (`lib/arm64-v8a/`) and check
   the patterns against it:

   ```sh
   python3 scripts/scan_signatures.py --library /path/to/libminecraftpe.so
   ```

   The script reads every profile and pattern straight out of
   `GameProfiles.hpp` and reports match / no match / ambiguous (multi-hit) per
   signature, plus the RVA of each hit.
2. Add a `GameProfile` entry with that build's offsets. If a signature moved,
   append the new pattern inside the same `SignaturePatterns` block; old
   candidates stay first so the patterns that already worked are tried first.
3. Re-run the scan until the new profile has exactly one hit per signature.
4. `--dump <RVA> <length>` hex-dumps code around a hit when a pattern has to be
   rebuilt from scratch.
5. Add the version to `option("mcver")` in `xmake.lua` and to the build matrix
   in `.github/workflows/build.yml`.

## Build

Android NDK r28c, xmake. `--mcver` selects the target game build and names the
output package.

```sh
xmake f -y -p android -a arm64-v8a -m release --ndk=/path/to/android-ndk-r28c --mcver=1.26.62
xmake -y
# -> build/android/arm64-v8a/release/CameraOverhaul-1.26.62.levipack
```

## License

GNU GPL v3.0.

Inspired by [CameraOverhaul](https://github.com/Mirsario/Minecraft-CameraOverhaul) (Mirsario, GPL-3.0). Independent Bedrock reimplementation.

## Contributors

- [RadiantByte](https://github.com/RadiantByte)
- [Viablecobra](https://github.com/Viablecobra)
