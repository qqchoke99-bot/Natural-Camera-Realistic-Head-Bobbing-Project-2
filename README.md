# CameraOverhaul

Cinematic camera for **Minecraft Bedrock** on Android, running as a native [LeviLauncher](https://github.com/LiteLDev/LeviLaunchroid) mod.

The camera reacts to how you move: it pitches when you accelerate or fall, rolls when you turn or strafe, and sways gently when you stand still.

> **Beta.** Tested by building and static analysis, not yet by extensive in-game play. Feedback welcome.

---

## Credits

Inspired by and derived from **[CameraOverhaul](https://www.curseforge.com/minecraft/mc-mods/cameraoverhaul)** by **Mirsario & Contributors**, a Java Edition mod ([source](https://github.com/Mirsario/Minecraft-CameraOverhaul)).

That project is licensed under GPL-3.0, and so is this one. This is an independent reimplementation for Bedrock — no code was copied from the Java mod, but the camera behaviour and tuning philosophy follow it closely.

| | |
|---|---|
| **Author** | LENDS DZIN |
| **Version** | 1.0.0-beta |
| **Platform** | Minecraft Bedrock (Android, arm64-v8a) |
| **Loader** | LeviLauncher / preloader-android |
| **License** | GPL-3.0 |

---

## Features

### Movement pitch
The camera tips forward as you accelerate and lifts as you fall, scaled by real velocity.

- ~2.5° while sprinting
- ~13° in an elytra dive
- ~6° at terminal fall velocity

### Turning roll
Turning the camera banks it into the corner, with an accumulator and eased falloff so quick flicks feel snappy while sustained turns settle smoothly. Strafing adds its own roll.

### Idle sway
Stand still and the camera drifts on smoothed gradient noise, fading in after a configurable delay and fading out the moment you move.

All three groups can be toggled independently, and every factor can be set to `0` to disable it.

---

## Settings

Everything is configurable from the LeviLauncher mod menu. Sliders have explicit ranges, and sub-settings are nested under their group toggle.

| Setting | Range | Default |
|---|---|---|
| **Pitch** | toggle | on |
| Forward Pitch Intensity | 0.0 – 3.0 | 1.0 |
| Forward Pitch Smoothing | 0.1 – 5.0 | 1.0 |
| Vertical Pitch Intensity | 0.0 – 3.0 | 1.0 |
| Vertical Pitch Smoothing | 0.1 – 5.0 | 1.0 |
| **Roll** | toggle | on |
| Turning Roll Intensity | 0.0 – 3.0 | 1.0 |
| Turning Roll Accumulation | 0.0 – 3.0 | 1.0 |
| Turning Roll Smoothing | 0.1 – 5.0 | 1.0 |
| Strafing Roll Intensity | 0.0 – 3.0 | 1.0 |
| **Idle Sway** | toggle | on |
| Sway Intensity | 0.0 – 3.0 | 1.0 |
| Sway Frequency | 0.1 – 5.0 | 1.0 |
| Sway Fade In Delay | 0.0 – 15.0 s | 3.0 |
| Sway Fade In Length | 0.1 – 15.0 s | 4.0 |
| Sway Fade Out Length | 0.1 – 15.0 s | 1.0 |
| Transition Smoothing | 0.1 – 5.0 | 1.0 |

Settings persist to the mod's config directory.

---

## Building

Builds run in GitHub Actions — open the **Actions** tab, pick **Build**, and press **Run workflow**. The `CameraOverhaul.levipack` artifact appears when it finishes.

To build locally you need xmake and Android NDK r28c:

```sh
xmake f -y -p android -a arm64-v8a -m release --ndk=/path/to/ndk
xmake -y
```

The `.levipack` is produced automatically after a successful build.

---

## How it works

The injection point is `CameraBlendSystem::_tick`, located by RTTI rather than by guesswork:

```
34StrictTickingSystemFunctionAdapterIXadL_ZN17CameraBlendSystem5_tickE5ViewTI19StrictEntityContextJN15MinecraftCamera15CameraComponentENS3_25CameraBlendStateComponentEEE...
```

It is a genuine per-frame camera system. The camera shake path was rejected: it only runs while a shake is active, and its rotational branch hard-zeroes the third axis, which would destroy roll.

The camera transform lives in the `CameraComponent`:

```
+0x28  quaternion (x, y, z, w)
+0x38  Vec3 position
```

The rotation bias is applied by **post-multiplying the engine quaternion** with a small local rotation, rather than converting to Euler angles and back. This matters for two reasons:

- it cannot hit gimbal lock when you look straight up or down;
- it does not depend on the engine's Euler convention, which is not observable from the disassembly.

An Euler round-trip was implemented first and discarded after numeric testing showed 700 failures with up to 2.57 rad of error near the poles. The quaternion path holds normalization to 1.2e-7.

### Safety

- The original function always runs first; the hook only adjusts the result.
- The quaternion is sanity-checked before use, and NaN/Inf values are rejected.
- Bias is clamped to 0.7 rad (~40°).
- When every bias is ~0 the hook returns immediately, so disabling costs nothing.
- **Player rotation is never modified** — only the camera transform. No `MovePlayerPacket` is affected, so this stays a purely visual mod.

---

## Not included yet

Screen shakes (explosions, lightning) are deliberately absent rather than shipped as dead toggles. The native shake queue caps at 2 entries where the original design needs 64, so it needs a full C++ implementation. Planned for a later release.
