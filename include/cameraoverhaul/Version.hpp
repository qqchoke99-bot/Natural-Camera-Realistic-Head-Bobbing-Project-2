#pragma once

#include <cstdint>
#include <string_view>

namespace cameraoverhaul {

// The package version is completed by scripts/package_levipack.py, which
// appends the Minecraft build this .levipack was compiled for, e.g.
// "1.1.4-beta" -> "1.1.4-beta-1.26.62".
inline constexpr std::string_view Name = "CameraOverhaul";
inline constexpr std::string_view Author = "DrukisMC";
inline constexpr std::string_view Description = "Cinematic camera for Minecraft Bedrock: movement pitch, turning roll and idle sway.";
inline constexpr std::string_view Version = "1.1.4-beta";
inline constexpr std::uint32_t SdkVersion = 1;
inline constexpr std::string_view RuntimeLibrary = "libCameraOverhaul.so";

}
