#pragma once

#include <cstddef>

#include <cameraoverhaul/memory/GameProfiles.hpp>

namespace cameraoverhaul::sdk::offsets {

// Resolved at runtime from the active GameProfile so that one .so can serve
// several Minecraft builds. Values are stable for a whole session.

namespace Actor {
inline std::size_t stateVectorComponent() {
    return cameraoverhaul::memory::actorStateVectorComponentOffset();
}
inline std::size_t rotationComponent() {
    return cameraoverhaul::memory::actorRotationComponentOffset();
}
}

inline std::size_t cameraRotation() {
    return cameraoverhaul::memory::cameraRotationOffset();
}

}
