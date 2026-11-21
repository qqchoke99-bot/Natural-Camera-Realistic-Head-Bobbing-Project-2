#pragma once

#include <string_view>

namespace cameraoverhaul {

// Thin logging shim. On Android it goes to logcat under the "CameraOverhaul"
// tag (`adb logcat -s CameraOverhaul`), which is the only way to see why a
// signature failed to resolve on a new game build.
void logInfo(std::string_view message);
void logWarn(std::string_view message);

}
