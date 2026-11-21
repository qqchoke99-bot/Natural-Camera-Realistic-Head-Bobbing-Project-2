#include <cameraoverhaul/memory/GameProfiles.hpp>

namespace cameraoverhaul::memory {

std::string_view targetVersion() {
    return targetProfile().version;
}

std::size_t actorStateVectorComponentOffset() {
    return targetProfile().actorStateVectorComponent;
}

std::size_t actorRotationComponentOffset() {
    return targetProfile().actorRotationComponent;
}

std::size_t cameraRotationOffset() {
    return targetProfile().cameraRotation;
}

}
