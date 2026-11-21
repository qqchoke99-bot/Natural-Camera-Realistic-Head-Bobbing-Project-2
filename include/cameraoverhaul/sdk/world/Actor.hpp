#pragma once

#include <cameraoverhaul/sdk/Memory.hpp>

#include <limits>
#include <cameraoverhaul/sdk/Offsets.hpp>
#include <cameraoverhaul/sdk/Types.hpp>

namespace cameraoverhaul::sdk {

class Actor {
public:
    void* stateVectorComponent() const { return field<void*>(this, offsets::Actor::stateVectorComponent()); }
    void* rotationComponent() const { return field<void*>(this, offsets::Actor::rotationComponent()); }

    // Both components are pointers owned by the actor. They are validated
    // before dereferencing: if the offsets do not fit the running build, the
    // slots hold arbitrary game data and following them would fault.
    Vec3 position() const {
        auto* component = stateVectorComponent();
        if (!readable(component, sizeof(Vec3))) {
            constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
            return Vec3{kNaN, kNaN, kNaN};
        }
        return field<Vec3>(component, 0);
    }

    Vec2 rotation() const {
        auto* component = rotationComponent();
        if (!readable(component, sizeof(Vec2))) {
            constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
            return Vec2{kNaN, kNaN};
        }
        return field<Vec2>(component, 0);
    }
};

class Player : public Actor {};

}
