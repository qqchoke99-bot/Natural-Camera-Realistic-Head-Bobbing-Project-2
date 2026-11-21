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

## Menu UI (Mod Menu v2)

The module registers with the preloader Mod Menu and then submits a **V2 config
schema** (`pl::modmenu::setConfigSchemaJson`), which is the modern card layout:

- four categories - **Preset**, **Head Bob**, **Camera Motion**, **Advanced**
- a sectioned layout with titles and per-control descriptions
- the preset as a **segmented** choice instead of a plain radio row
- sliders with real ranges, steps and units (`x`, `Hz`, `s`)
- controls that grey out when their feature toggle is off (`enabled_when`)

The V2 schema API landed on preloader main after the `0.2.3` release, so it is
declared **weak** here: on a preloader that does not export it the symbol
resolves to null, the call is skipped, and the mod keeps working on the flat V1
entry list. Nothing fails to load either way.

## Preset strengths

Every preset carries its own strength, so switching preset changes the bob
without touching the master strength:

| Key | Default | Meaning |
|---|---|---|
| `headBobStrengthDefault` | 1.00 | used while the Default preset is active |
| `headBobStrengthBodycam` | 1.15 | used while Bodycam is active |
| `headBobStrengthComfort` | 0.70 | used while Comfort is active |
| `headBobStrengthCustom` | 1.00 | used while Custom is active |

Effective gain is `headBobStrength` (master) x the active preset's strength. The
defaults reproduce the old hard-coded 1.15 / 0.70 exactly, so existing configs
look identical until you move a slider.

## Performance

The mod is not CPU bound: the whole per-frame camera maths is about 18 ns on a
host core (two springs + two sines), which is nothing against a frame budget. So
this pass targets *jitter* - work the render thread should never do - rather
than frame rate.

| Change | Before | After | Effect |
|---|---|---|---|
| `EventBus::publish` with a listener | 30.3 ns, one heap allocation + `std::function` copy per publish | 2.4 ns, lock-free atomic load | removes allocator traffic from the tick path |
| `EventBus::publish` with no listener | 21.9 ns (shared_mutex + hash lookup) | 0.6 ns | the eglSwapBuffers detour publishes `FrameEvent` every frame and nothing subscribes |
| `headBobMode` check in the frame path | 3.1 ns, `std::string` vs literal, twice | 1.2 ns, cached enum | parsed once in `loadConfig` |
| `-Oz` vs `-O2` for the target | 18.5 ns per frame | 18.2 ns | **not worth it** - kept `-Oz`; the cost is libm, not codegen |

Measured with `eventbus`/`math` micro-benchmarks on the host; the publish numbers
are per call, so at 20 ticks + 120 frames per second the saving is sub-microsecond
per second. It is a correctness-of-design win (no allocation, no lock on the
render thread), not a speedup you would feel.

Publishing is now lock-free in the read direction: each event type holds an
atomic pointer to an immutable listener list, rebuilt copy-on-write on
subscribe/unsubscribe, with replaced lists kept alive so a publisher walking an
old list never touches freed memory.

## Changes from the supplied v2 tree

Signatures are **not** among them: both patterns already match the 1.26.62
library once each, so they are byte-for-byte as you wrote them.

| File | Change | Why |
|---|---|---|
| `include/cameraoverhaul/sdk/offsets/World.hpp` | `0x208`/`0x218` -> `0x228`/`0x238` | the actor state vector and rotation moved in 1.26.62; with the old values the pointer lands on the wrong member |
| `include/cameraoverhaul/Version.hpp` | `2.0.0-1.26.62`, description mentions 1.26.62 | version string feeds the manifest |
| `levimod.json` | `2.0.0-1.26.62`, `minecraft_versions: ["1.26.62.1"]` | one package per build; the offsets are compile-time constants |
| `src/launcher/ModMenu.cpp` | dropped the inline Radio option list from the `headBobMode` entry | `ModuleBuilder::config` takes at most 7 arguments in every released preloader - the 8-argument call never compiled |
| `include/cameraoverhaul/events/EventBus.hpp` | lock-free, allocation-free publish | see Performance above |
| `src/module/CameraOverhaulModule.{hpp,cpp}` | `headBobMode` parsed into an enum on config load | no `std::string` comparisons in the frame path |

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
