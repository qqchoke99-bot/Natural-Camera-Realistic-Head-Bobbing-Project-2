#pragma once

#include <cstddef>

namespace headbob::profile {

// One binary layout of libminecraftpe.so. The package is compiled for exactly
// one of these (CMake option MCVER -> HEADBOB_PROFILE_INDEX), so it can never
// read the wrong build. Values verified against the shipped libraries:
//
//   1.26.50: state vector 0x208, actor rotation 0x218
//            (what CameraOverhaul 1.1.3 shipped with)
//   1.26.62: state vector 0x228, actor rotation 0x238
//            state vector: Vec3 getter @0xFD8ED18
//              ldr x8, [x0, #0x228]; ldp s0, s1, [x8]; ldr s2, [x8, #8]; ret
//            actor rotation: accessor @0xFD8709C, field setters @0xFD8FD90 (+0),
//              0xFD8FD9C (+4), 0xFD8FDA8 (+8), 0xFD8FDB4 (+0xc), i.e. current
//              pitch/yaw then previous pitch/yaw.
//
// camera rotation is +0x28 in both (the quaternion CameraBlendSystemTick
// writes: `stur q0, [x20, #0x28]`).
struct Layout {
    const char* version;
    std::size_t stateVectorComponent;
    std::size_t actorRotationComponent;
    std::size_t cameraRotation;
};

inline constexpr std::size_t kLayoutCount = 2;

inline constexpr Layout kLayouts[kLayoutCount]{
    {"1.26.50", 0x208, 0x218, 0x28},
    {"1.26.62", 0x228, 0x238, 0x28},
};

#if !defined(HEADBOB_PROFILE_INDEX)
#error "HEADBOB_PROFILE_INDEX must be defined; configure with the CMake option MCVER"
#endif

inline constexpr std::size_t kLayoutIndex = HEADBOB_PROFILE_INDEX;
static_assert(kLayoutIndex < kLayoutCount, "HEADBOB_PROFILE_INDEX has no layout");

inline constexpr const Layout& layout() {
    return kLayouts[kLayoutIndex];
}

}
