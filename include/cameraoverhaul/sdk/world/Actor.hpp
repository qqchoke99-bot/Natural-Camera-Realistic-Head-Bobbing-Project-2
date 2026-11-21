#pragma once

#include <cameraoverhaul/sdk/Memory.hpp>
#include <cameraoverhaul/sdk/Offsets.hpp>
#include <cameraoverhaul/sdk/Types.hpp>

namespace cameraoverhaul::sdk {

class Actor {
public:
    void* stateVectorComponent() const { return field<void*>(this, offsets::Actor::stateVectorComponent()); }
    void* rotationComponent() const { return field<void*>(this, offsets::Actor::rotationComponent()); }

    Vec3 position() const {
        auto* component = stateVectorComponent();
        return component ? field<Vec3>(component, 0) : Vec3{};
    }

    Vec2 rotation() const {
        auto* component = rotationComponent();
        return component ? field<Vec2>(component, 0) : Vec2{};
    }
};

class Player : public Actor {};

}
