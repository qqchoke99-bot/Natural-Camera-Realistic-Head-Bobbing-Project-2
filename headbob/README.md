# Realistic Head Bob

> **Superseded.** The step bob now lives inside CameraOverhaul (`enableHeadBob`,
> on by default). This standalone package is kept for reference; running both
> stacks two bob effects. The fix below is still applied so it is not left
> broken on its own.

FPS-style step head bob for Minecraft Bedrock on Android (LeviLauncher).
Camera dips and sways in step with your walk cycle, springs back when you stop,
and drifts gently while you stand still.

This is the revived version of `qqchoke99-bot/1xmake.lua` (1.0.0).

## What was broken in 1.0.0

`MyMod.cpp` fed the head-bob model a hard-coded `speedXZ = 0`:

```cpp
const float speedXZ   = 0.f;   // "Player velocity stubs — optional NormalTick later"
const bool  onGround  = true;
```

With zero speed the step phase never starts, so the camera only ever showed the
idle breathing/drift — the mod was effectively dead while walking. `dt` was also
a fixed `1/60` stub, ignoring the real frame time.

Both are fixed:

| | 1.0.0 | this build |
|---|---|---|
| velocity | hard-coded `0` | derived from the local player's position each tick |
| `onGround` | always `true` | `|vertical speed| < 0.8 b/s` |
| sprint detection | always `false` | `speed > 5.0 b/s` (walk ≈ 4.3, sprint ≈ 5.6) |
| `dt` | fixed `1/60` | measured per frame, clamped to `[1 ms, 100 ms]` |

The bob math in `HeadBob.cpp` is untouched — the spring-damper port from the
Java *Realistic Head Bobbing* mod.

## One package per game build

Struct offsets differ between Bedrock builds, so the package is compiled for
exactly one version (`src/version/Profile.h`, CMake option `MCVER`):

| Game build | state vector | actor rotation | camera rotation |
|---|---|---|---|
| 1.26.50 | `0x208` | `0x218` | `0x28` |
| 1.26.62 | `0x228` | `0x238` | `0x28` |

The 1.26.62 values were read out of that library (the `Vec3` getter at
`0xFD8ED18`, and the rotation component's per-field setters at `0xFD8FD90`…
`0xFD8FDB4`). Both signature patterns are byte-identical in 1.26.50 and 1.26.62
and each hits exactly once.

Every component pointer is probed with `mincore()` before it is dereferenced
(`src/util/Memory.h`), so a build with moved offsets makes the mod go quiet
instead of faulting.

## Install

1. Download the artifact for your game build from the latest
   **Build Realistic Head Bob** run (Actions -> that run -> Artifacts):
   `realistic-headbob-1.26.62` or `realistic-headbob-1.26.50`.
2. Unzip it: `realistic_headbob-<version>.levipack`.
3. In LeviLauncher, install that `.levipack` like any other mod and enable it.

Packages are pinned per game build, so installing the 1.26.50 pack on 1.26.62
finds no signature and stays silent instead of crashing.

## Config

`config/config.json` next to the library:

- `mode`: `default` · `bodycam` · `comfort` · `custom`
- strengths: `globalStrength`, `stepStrength`, `swayStrength`, `cornerStrength`,
  `breathStrength`, `driftStrength`
- feel: `smoothHz`, `damping`, `fadeLerp`, `stepRate`, `breathRate`, `driftRate`
- `enabled`, `idleEnabled`, `enableWhenMounted`, `enableThirdPerson`
- signature overrides: `cameraBlendSig`, `localPlayerTickSig`, `moduleName`

## If it looks dead

The mod logs what it sees (first 15 samples, one every 2 s; `beat` lines every
1 s then every 5 s):

```
tick#1 pos=(12.30, 64.00, -8.10) delta=(0.210, 0.000, 0.000) posSpeed=4.20 deltaSpeed=4.20 vy=0.00
beat ticks=42 frames=900 posSpeed=4.20 deltaSpeed=4.20 vy=0.00 ground=1 sprint=0 fade=1.00 mode=delta profile=1.26.62
```

Read it in order:

| Symptom | Meaning |
|---|---|
| no `tick#` lines at all | the `NormalTick` hook never ran - the signature resolved somewhere inert |
| `tick#` but `delta=(0,0,0)` and `posSpeed=0` | the player really is not moving, or the pointer is not the player |
| `mode=posdiff` | the tick delta read as dead; the fallback took over |
| `fade=1.00` but no visible bob | the camera write is being overwritten or is too weak: raise `globalStrength` |
| `ground=0` while walking | the vertical-speed test is too tight for that terrain |

## Build

Run from the repository root:

```sh
cmake -S headbob -B build -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake \
      -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-28 -DANDROID_STL=c++_shared \
      -DCMAKE_BUILD_TYPE=Release -DMCVER=1.26.62
cmake --build build --target levi_package -j$(nproc)
# -> build/realistic_headbob-1.26.62.levipack
```

CI: `.github/workflows/build-headbob.yml` builds both versions.

Default config files are checked in under `config/` (see `config/README.md`);
no host tool has to run during the cross build.

## Credits

- Head-bob math: *Realistic Head Bobbing* (Java)
- Camera hook and quaternion write: *CameraOverhaul* by Mirsario & contributors
  (GPL-3.0), Bedrock reimplementation by DrukisMC

Licensed GPL-3.0, see `LICENSE`.
