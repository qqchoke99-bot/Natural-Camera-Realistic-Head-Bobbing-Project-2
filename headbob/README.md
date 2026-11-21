# Realistic Head Bob (v2.0.0)

Step head-bob integrated into the cinematic camera, for Minecraft Bedrock on
Android via LeviLauncher.

**Build: 1.26.62.** This tree is the 2.0.0 sources pinned to game build
1.26.62 - actor offsets `0x228` / `0x238`, signatures re-verified against that
library (`CameraBlendSystemTick` `0x11EA58CC`, `NormalTick` `0xAF5E7A0`, one hit
each; `python3 scripts/verify_signatures.py <libminecraftpe.so>` reproduces it).
Other game builds need their own package: the offsets are compile-time
constants, not runtime lookups.

---

## Changes from the supplied v2 tree

Signatures are **not** among them: both patterns already match the 1.26.62
library once each, so they are byte-for-byte as you wrote them.

| File | Change | Why |
|---|---|---|
| `include/cameraoverhaul/sdk/offsets/World.hpp` | `0x208`/`0x218` -> `0x228`/`0x238` | the actor state vector and rotation moved in 1.26.62; with the old values the pointer lands on the wrong member |
| `include/cameraoverhaul/Version.hpp` | `2.0.0-1.26.62`, description mentions 1.26.62 | version string feeds the manifest |
| `levimod.json` | `2.0.0-1.26.62`, `minecraft_versions: ["1.26.62.1"]` | one package per build; the offsets are compile-time constants |
| `src/launcher/ModMenu.cpp` | dropped the inline Radio option list from the `headBobMode` entry | `ModuleBuilder::config` takes at most 7 arguments in every released preloader - the 8-argument call never compiled |

Everything else - the bob maths, the mode presets, the config keys, the
cinematic pitch/roll/sway, the packaging script, the icon - is untouched.


Built on the **CameraOverhaul** hook path, pinned to 1.26.62.

## Features

- **Step head-bob** (modes: default / bodycam / comfort / custom)
- Original cinematic pitch / roll / idle sway

## Install

1. LeviLauncher  
2. Import `RealisticHeadBob.levipack`  
3. Minecraft **1.26.50** or **1.26.52** (same signature set as CO 1.26.50)

## Config

```text
/sdcard/games/CameraOverhaul/config.json
```
(key group still `RealisticHeadBob` after rename)

Or in-game Mod Menu if PL menu works on your Levi version.

## Build

```sh
xmake f -y -p android -a arm64-v8a -m release --ndk=/path/to/ndk
xmake -y
```

GitHub Actions: push → workflow Build → artifact `RealisticHeadBob.levipack`

## Credits

- Camera hooks / signatures: CameraOverhaul Bedrock (GPL-3.0)
- Step bob: Realistic Head Bobbing style spring-damper
