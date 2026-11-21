# Realistic Head Bob

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

## Config

`config/config.json` next to the library:

- `mode`: `default` · `bodycam` · `comfort` · `custom`
- strengths: `globalStrength`, `stepStrength`, `swayStrength`, `cornerStrength`,
  `breathStrength`, `driftStrength`
- feel: `smoothHz`, `damping`, `fadeLerp`, `stepRate`, `breathRate`, `driftRate`
- `enabled`, `idleEnabled`, `enableWhenMounted`, `enableThirdPerson`
- signature overrides: `cameraBlendSig`, `localPlayerTickSig`, `moduleName`

## Build

```sh
cmake -S headbob -B build -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake \
      -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-28 -DANDROID_STL=c++_shared \
      -DCMAKE_BUILD_TYPE=Release -DMCVER=1.26.62
cmake --build build --target levi_package -j$(nproc)
# -> build/realistic_headbob-1.26.62.levipack
```

CI: `.github/workflows/build-headbob.yml` builds both versions.

## Credits

- Head-bob math: *Realistic Head Bobbing* (Java)
- Camera hook and quaternion write: *CameraOverhaul* by Mirsario & contributors
  (GPL-3.0), Bedrock reimplementation by DrukisMC

Licensed GPL-3.0, see `LICENSE`.
