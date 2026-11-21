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
