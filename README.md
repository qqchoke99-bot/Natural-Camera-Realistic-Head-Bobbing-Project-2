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
| `1.26.50` | 1.26.50.1 | shipped and played |
| `1.26.62` | 1.26.62.1 | shipped and played; verified in game (1.1.5-beta-1.26.62) |

Scan result for the 1.26.62 library (342 MB, `aarch64`): both patterns match
**exactly once**, `NormalTick` @ `0xAF5E7A0`, `CameraBlendSystemTick` @
`0x11EA58CC`. The rotation quaternion in the camera component is still at
`+0x28` (`ldur q0, [x22, #0x28]` / `stur q0, [x20, #0x28]` in the tick body).

The **actor component offsets moved `+0x20`** in 1.26.62, which is what the
first 1.26.62 test tripped over and the reason the guard exists (`0x208`/`0x218` no longer hold component
pointers). The new values come from the binary itself:

| Field | 1.26.50 | 1.26.62 | Evidence in 1.26.62 |
|---|---|---|---|
| state vector component | `0x208` | `0x228` | `Vec3` getter @ `0xFD8ED18`: `ldr x8,[x0,#0x228]` / `ldp s0,s1,[x8]` / `ldr s2,[x8,#8]` / `ret` |
| actor rotation component | `0x218` | `0x238` | component accessor @ `0xFD8709C`; field setters @ `0xFD8FD90` (`+0`), `0xFD8FD9C` (`+4`), `0xFD8FDA8` (`+8`), `0xFD8FDB4` (`+0xc`) — current pitch/yaw then previous, so the `Vec2` is at `+0` |
| camera rotation | `0x28` | `0x28` | unchanged, see the tick body above |

### Diagnostics

`adb logcat -s CameraOverhaul` reports which profile was compiled in and the
address of each resolved signature, then prints one sample from the local player
on the first tick:

```
signatures: NormalTick at 0xAF5E7A0
signatures: CameraBlendSystemTick at 0x11EA58CC
signatures: matched game profile 1.26.62
sdk: first sample pos=(112.5, 64.0, -38.2) yaw=91.4 pitch=-3.0
```

Those numbers are the direct proof that the actor offsets fit the build. Every
component pointer is also probed with `mincore()` before it is dereferenced, so
a pointer read from a stale offset is rejected instead of faulting. If the
offsets do not fit, the effects stay off after 40 ticks and the log says why.

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
   For the component offsets, look for the `Vec3` getter shape
   `ldr x8, [x0, #imm]; ldp s0, s1, [x8]; ldr s2, [x8, #8]; ret` (state vector)
   and the single-float setters `ldr x8, [x0, #imm]; str s0, [x8, #0/#4]; ret`
   (rotation: pitch at `+0`, yaw at `+4`, previous values at `+8`/`+0xc`).
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

## Realistic Head Bob (revived)

`headbob/` holds the revived **Realistic Head Bob** mod (formerly
[`qqchoke99-bot/1xmake.lua`](https://github.com/qqchoke99-bot/1xmake.lua) 1.0.0).
It ships on its own, per game build:

| Package | Game build |
|---|---|
| `realistic_headbob-1.26.62.levipack` | 1.26.62 (1.26.62.1) |
| `realistic_headbob-1.26.50.levipack` | 1.26.50 (1.26.50.1) |

Built by `.github/workflows/build-headbob.yml`; details in
[`headbob/README.md`](headbob/README.md). Signatures verified uniquely against
the 1.26.62 library (CameraBlendSystemTick `0x11EA58CC`, NormalTick
`0xAF5E7A0`). This mod is standalone - CameraOverhaul here is unchanged at
1.1.5-beta, and the two are independent.

## License

GNU GPL v3.0.

Inspired by [CameraOverhaul](https://github.com/Mirsario/Minecraft-CameraOverhaul) (Mirsario, GPL-3.0). Independent Bedrock reimplementation.

## Contributors

- [RadiantByte](https://github.com/RadiantByte)
- [Viablecobra](https://github.com/Viablecobra)
