# CameraOverhaul

Cinematic camera for Minecraft Bedrock on Android ([LeviLauncher](https://github.com/LiteLDev/LeviLaunchroid)).

Pitch when you accelerate or fall, roll when you turn, sway when idle.

## Install

1. Install LeviLauncher.
2. Import `CameraOverhaul.levipack`.
3. Enable CameraOverhaul and launch Minecraft 1.26.50.

## Build

Android NDK r28c, xmake.

```sh
xmake f -y -p android -a arm64-v8a -m release --ndk=/path/to/android-ndk-r28c
xmake -y
```

## License

GNU GPL v3.0.

Inspired by [CameraOverhaul](https://github.com/Mirsario/Minecraft-CameraOverhaul) (Mirsario, GPL-3.0). Independent Bedrock reimplementation.

## Contributors

- [RadiantByte](https://github.com/RadiantByte)
- [Viablecobra](https://github.com/Viablecobra)
