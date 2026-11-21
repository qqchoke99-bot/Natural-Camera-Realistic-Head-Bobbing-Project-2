#pragma once

#include <array>
#include <cstddef>
#include <string_view>

#include <cameraoverhaul/memory/Signatures.hpp>

namespace cameraoverhaul::memory {

// A signature may carry several alternative patterns. When a game update moves
// a function, append the new pattern instead of replacing the old one, so the
// same profile keeps working for several builds of that version line.
inline constexpr std::size_t kMaxPatternsPerSignature = 3;

struct SignaturePatterns {
    std::array<std::string_view, kMaxPatternsPerSignature> candidates{};
};

// One binary layout of libminecraftpe.so: where the two hooked functions live
// and which component offsets that build uses. A .levipack is compiled for
// exactly one profile (see CAMERAOVERHAUL_TARGET_VERSION below), so a package
// can never be applied to a game build it was not checked against.
//
// scripts/scan_signatures.py parses this table, so keep the shape: a
// SignaturePatterns block is introduced by `// <SignatureId name>` and holds
// one quoted pattern per line, and GameProfile header values stay in the order
// version, stateVector, actorRotation, cameraRotation.
struct GameProfile {
    std::string_view version;
    std::size_t actorStateVectorComponent;
    std::size_t actorRotationComponent;
    std::size_t cameraRotation;
    std::array<SignaturePatterns, SignatureCount> signatures{};
};

inline constexpr std::size_t kGameProfileCount = 2;

inline constexpr std::array<GameProfile, kGameProfileCount> kGameProfiles{{
    GameProfile{
        "1.26.50",
        0x208,
        0x218,
        0x28,
        {{
            SignaturePatterns{{ // NormalTick
                "? ? ? FC ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? 91 ? ? ? D1 54 D0 3B D5 F3 03 00 AA ? ? ? F9 ? ? ? F8 ? ? ? 39",
            }},
            SignaturePatterns{{ // CameraBlendSystemTick
                "? ? ? D1 ? ? ? 6D ? ? ? 6D ? ? ? 6D ? ? ? 6D ? ? ? A9 ? ? ? F9 ? ? ? A9 ? ? ? A9 ? ? ? 91 55 D0 3B D5 F3 03 01 AA F4 03 00 AA ? ? ? F9 ? ? ? 91 ? ? ? 91",
            }},
        }},
    },
    // 1.26.62 (libminecraftpe.so build 2026-11-06, 342 MB) - verified in game
    // on 1.26.62.1 with 1.1.5-beta-1.26.62:
    //   signatures - both patterns match exactly once,
    //                NormalTick @ 0xAF5E7A0, CameraBlendSystemTick @ 0x11EA58CC
    //   cameraRotation 0x28 - unchanged; the tick body still does
    //                `ldur q0, [x22, #0x28]` / `stur q0, [x20, #0x28]` and the
    //                euler-to-quat stores `stp s4, s1, [x20, #0x28]`
    //   actor components - both moved +0x20 in this build (0x208/0x218 stop
    //                being component pointers, which is what broke the first
    //                1.26.62 test):
    //                  state vector 0x228 - Vec3 getter @0xFD8ED18:
    //                    ldr x8, [x0, #0x228]; ldp s0, s1, [x8];
    //                    ldr s2, [x8, #8]; ret
    //                  actor rotation 0x238 - component taken @0xFD8709C and
    //                    single-field setters @0xFD8FD90 (+0), 0xFD8FD9C (+4),
    //                    0xFD8FDA8 (+8), 0xFD8FDB4 (+0xc): current pitch/yaw
    //                    then previous, so the Vec2 the mod reads is at +0
    GameProfile{
        "1.26.62",
        0x228,
        0x238,
        0x28,
        {{
            SignaturePatterns{{ // NormalTick
                "? ? ? FC ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? 91 ? ? ? D1 54 D0 3B D5 F3 03 00 AA ? ? ? F9 ? ? ? F8 ? ? ? 39",
            }},
            SignaturePatterns{{ // CameraBlendSystemTick
                "? ? ? D1 ? ? ? 6D ? ? ? 6D ? ? ? 6D ? ? ? 6D ? ? ? A9 ? ? ? F9 ? ? ? A9 ? ? ? A9 ? ? ? 91 55 D0 3B D5 F3 03 01 AA F4 03 00 AA ? ? ? F9 ? ? ? 91 ? ? ? 91",
            }},
        }},
    },
}};

// The build pins one profile. xmake passes -DCAMERAOVERHAUL_TARGET_VERSION="1.26.62"
// (option `mcver`), and an unknown value is a compile error rather than a
// package that silently does nothing.
#if !defined(CAMERAOVERHAUL_TARGET_VERSION)
#error "CAMERAOVERHAUL_TARGET_VERSION must be defined; build with the xmake option mcver"
#endif

inline constexpr std::string_view kTargetVersion = CAMERAOVERHAUL_TARGET_VERSION;

constexpr std::size_t profileIndexFor(std::string_view version) {
    for (std::size_t index = 0; index < kGameProfiles.size(); ++index) {
        if (kGameProfiles[index].version == version) return index;
    }
    return kGameProfiles.size();
}

inline constexpr std::size_t kTargetProfileIndex = profileIndexFor(kTargetVersion);
static_assert(kTargetProfileIndex < kGameProfiles.size(),
              "CAMERAOVERHAUL_TARGET_VERSION has no entry in kGameProfiles");
static_assert(kGameProfiles.size() == kGameProfileCount, "kGameProfiles/kGameProfileCount mismatch");

inline constexpr const GameProfile& targetProfile() {
    return kGameProfiles[kTargetProfileIndex];
}

// Offsets of the target profile. Reading them through functions (instead of
// constants in sdk/offsets) keeps the version table in one place.
std::string_view targetVersion();
std::size_t actorStateVectorComponentOffset();
std::size_t actorRotationComponentOffset();
std::size_t cameraRotationOffset();

}
