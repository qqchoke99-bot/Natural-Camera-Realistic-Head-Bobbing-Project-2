# Realistic Head Bob

Your mod, revived for Minecraft Bedrock **1.26.62** (and 1.26.50) on LeviLauncher.

The original source is kept as-is: `MyMod.{h,cpp}`, `HeadBob.{h,cpp}`,
`Config.{h,cpp}`, `config_generator.cpp`, the same class, the same file names,
the same math, the same config keys and the same defaults
(`globalStrength 0.32`, `mode bodycam`, `stepRate 1`, `smoothHz 1.75`,
`damping 1.25`). `HeadBob.cpp` is byte-identical to what you wrote.

## The four things that had to change

Everything else is untouched. Each of these was mandatory - the 1.0.0 tree does
not compile or cannot work without them.

| # | 1.0.0 | now | why |
|---|---|---|---|
| 1 | `#include <ll/api/mod/NativeMod.h>`, `LL_REGISTER_MOD` | `<pl/Mod.hpp>`, `PL_REGISTER_MOD` ; `ll::mod::NativeMod` kept | preloader-android 0.2.2 ships `pl/Mod.hpp` and that macro; the old include does not exist, so the library never linked |
| 2 | player velocity hard-coded `0` | `NormalTick` hook reads the actor's own tick delta | this is *the* mod: with `speedXZ = 0` the step phase can never start and only idle breathing shows |
| 3 | offsets only valid for 1.26.45 | pinned per build (`MCVER`) | the actor state vector and rotation moved |
| 4 | `-C` tar, cross-built config generator, no `liblog` | fixed in `CMakeLists.txt` | the package never assembled; `__android_log_print` was undefined |

`main.cpp`, the manifest, the `.levipack` layout and the package name are the
original ones.

## Signatures, verified on 1.26.62

Both patterns are matched with their wildcards against the shipped
`libminecraftpe.so`, not just pasted - each has exactly **one** hit:

| Function | Hits | Static RVA | Your in-game log |
|---|---|---|---|
| `CameraBlendSystemTick` (camera path, from CameraOverhaul) | 1 | `0x11EA58CC` | `0x6c591e98cc` |
| `NormalTick` (player tick, this mod's velocity source) | 1 | `0xAF5E7A0` | `0x6c522a27a0` |

Both runtime addresses minus their RVAs give the same module base
(`0x6C47344000`), so each hook landed exactly on the function found statically.

`CameraBlendSystemTick` is solved code reused from CameraOverhaul - the camera
hook, the detour signature and the quaternion write at `component + 0x28`. The
player-tick pattern is this mod's own, because the bob needs velocity and
CameraOverhaul never needed a second hook.

Re-check any build with:

```sh
python3 scripts/verify_signatures.py /path/to/libminecraftpe.so
```

## Offsets per build

`src/mod/MyMod.cpp`, selected at compile time by `MCVER` -> `HEADBOB_PROFILE_INDEX`:

| Game build | state vector | actor rotation | camera rotation |
|---|---|---|---|
| 1.26.50 | `0x208` | `0x218` | `0x28` |
| 1.26.62 | `0x228` | `0x238` | `0x28` |

`+0x228` is `{ Vec3 pos @0x0; Vec3 posPrev @0xC; Vec3 mPosDelta @0x18 }` -
confirmed in the library: the delta setter at `0xFD9733C` writes `0x18`/`0x20`,
the posPrev setter at `0xFD8E0AC` writes `0xC`/`0x14`.

## One behavioural detail worth knowing

`onGround` used to be a constant `true` in 1.0.0. Now it is derived from the
tick delta - but the obvious test is wrong: a grounded Bedrock actor still
reports about **-1.6 b/s** of vertical delta every tick, because gravity is
applied and then resolved by collision. Testing "vertical speed is about zero"
reports *airborne forever* and holds the bob fade at 0, which looks exactly like
"the mod does nothing". Grounded therefore means "falling slower than 3 b/s".

## Install

Actions -> **Build Realistic Head Bob** -> run -> Artifacts:

- `realistic-headbob-1.26.62` -> `realistic_headbob-1.26.62.levipack`
- `realistic-headbob-1.26.50` -> `realistic_headbob-1.26.50.levipack`

Unzip, import the `.levipack`, enable it. One package per game build.

## Config

`config/config.json`, original keys and defaults. `mode` is one of `default`,
`bodycam`, `comfort`, `custom`. `globalStrength 0.32` with `bodycam` gives the
original amplitude.
