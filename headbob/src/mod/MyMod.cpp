#include "mod/MyMod.h"

#include <pl/Mod.hpp>
#include <pl/memory/Hook.hpp>
#include <pl/memory/Signature.hpp>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <sys/mman.h>
#include <unistd.h>

/*
 * Camera path copied from CameraOverhaul 1.1.2-beta (GPL-3.0 / Mirsario-derived):
 *   - SignatureId::CameraBlendSystemTick
 *   - detour void(component, blendState, factor)
 *   - write quaternion at component + 0x28 after original
 *
 * Head-bob math remains the spring-damper port from Realistic Head Bobbing (Java).
 */

namespace {

// CameraOverhaul Signatures.cpp — CameraBlendSystemTick
constexpr const char* kCameraBlendSig =
    "? ? ? D1 ? ? ? 6D ? ? ? 6D ? ? ? 6D ? ? ? 6D ? ? ? A9 ? ? ? F9 ? ? ? A9 ? ? ? A9 ? ? ? 91 55 D0 3B D5 F3 03 01 AA F4 03 00 AA ? ? ? F9 ? ? ? 91 ? ? ? 91";

// NormalTick — the local player tick. This mod's own signature (CameraOverhaul
// never needed a second hook); verified to match exactly once in 1.26.50 and
// 1.26.62, see scripts/verify_signatures.py.
constexpr const char* kLocalPlayerTickSig =
    "? ? ? FC ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? 91 ? ? ? D1 54 D0 3B D5 F3 03 00 AA ? ? ? F9 ? ? ? F8 ? ? ? 39";

// Camera transform written by CameraBlendSystem::_tick — same in both builds.
constexpr std::uintptr_t kRotationOffset = 0x28;

// Actor field offsets, per game build. Pinned at compile time (CMake MCVER), so
// a package can never read the wrong layout.
#if HEADBOB_PROFILE_INDEX == 0
constexpr std::uintptr_t kStateVectorOffset   = 0x208;
constexpr std::uintptr_t kActorRotationOffset = 0x218;
#elif HEADBOB_PROFILE_INDEX == 1
constexpr std::uintptr_t kStateVectorOffset   = 0x228;
constexpr std::uintptr_t kActorRotationOffset = 0x238;
#else
#error "HEADBOB_PROFILE_INDEX must be 0 (1.26.50) or 1 (1.26.62)"
#endif

// ActorStateVectorComponent { Vec3 mPos; Vec3 mPosPrev; Vec3 mPosDelta; }
constexpr std::uintptr_t kPosDeltaOffset = 0x18;
constexpr float kTicksPerSecond = 20.f;

constexpr float kPi     = 3.14159265358979323846f;
constexpr float kDeg2Rad = kPi / 180.0f;

struct Vec3 {
    float x, y, z;
};

struct Quat {
    float x, y, z, w;
};

float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

Quat quatMul(const Quat& a, const Quat& b) {
    return Quat{
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

Quat axisAngle(float ax, float ay, float az, float angle) {
    const float h = angle * 0.5f;
    const float s = std::sin(h);
    return Quat{ax * s, ay * s, az * s, std::cos(h)};
}

Quat quatNormalize(const Quat& q) {
    const float n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (!(n > 1e-8f)) return Quat{0.f, 0.f, 0.f, 1.f};
    return Quat{q.x / n, q.y / n, q.z / n, q.w / n};
}

double nowSeconds() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

std::uintptr_t field(const void* object, std::uintptr_t offset) {
    return *reinterpret_cast<const std::uintptr_t*>(
        reinterpret_cast<std::uintptr_t>(object) + offset);
}

// Never follow a pointer that a moved structure offset might have produced:
// mincore() answers "is this mapped" without touching the memory.
bool readable(const void* address, std::size_t length) {
    if (!address || length == 0) return false;
    const auto value = reinterpret_cast<std::uintptr_t>(address);
    if (value < 0x1000) return false;
    const long pageValue = sysconf(_SC_PAGESIZE);
    if (pageValue <= 0) return false;
    const auto pageSize = static_cast<std::uintptr_t>(pageValue);
    const auto firstPage = value & ~(pageSize - 1);
    const auto lastPage = (value + length - 1) & ~(pageSize - 1);
    unsigned char present = 0;
    if (mincore(reinterpret_cast<void*>(firstPage), pageSize, &present) != 0) return false;
    if (lastPage != firstPage &&
        mincore(reinterpret_cast<void*>(lastPage), pageSize, &present) != 0) return false;
    return true;
}

} // namespace

RealisticHeadBobMod& RealisticHeadBobMod::instance() {
    static RealisticHeadBobMod inst;
    return inst;
}

bool RealisticHeadBobMod::load() {
    auto& log = getSelf().getLogger();
    log.info("Realistic Head Bob load");

    const auto cfgPath = getSelf().getModDir() / "config" / "config.json";
    auto& cfg = headbob::Config::get();
    if (!cfg.loadFromFile(cfgPath.string())) {
        cfg.loadDefaults();
        if (cfg.cameraBlendSig.empty()) {
            cfg.cameraBlendSig = kCameraBlendSig;
        }
        if (cfg.localPlayerTickSig.empty()) {
            cfg.localPlayerTickSig = kLocalPlayerTickSig;
        }
        std::error_code ec;
        std::filesystem::create_directories(cfgPath.parent_path(), ec);
        cfg.saveToFile(cfgPath.string());
        log.info("Wrote default config");
    } else {
        if (cfg.cameraBlendSig.empty()) {
            cfg.cameraBlendSig = kCameraBlendSig;
        }
        if (cfg.localPlayerTickSig.empty()) {
            cfg.localPlayerTickSig = kLocalPlayerTickSig;
        }
        log.info("Config loaded mode={}", headbob::modeToString(cfg.mode));
    }
    return true;
}

bool RealisticHeadBobMod::enable() {
    auto& log = getSelf().getLogger();
    // No ModuleBuilder — Levi 1.5.25 crashed in registerModule.

    if (!resolveAndHook()) {
        log.warn("CameraBlendSystemTick hook failed — check cameraBlendSig / MC version");
    } else {
        log.info("CameraBlendSystemTick hooked");
    }

    // The bob needs velocity: without this hook the camera stays still while you
    // walk, because headbob::update is fed a speed of zero.
    if (!resolveAndHookPlayerTick()) {
        log.warn("NormalTick hook failed — head bob would stay at speed 0");
    } else {
        log.info("NormalTick hooked — player velocity live");
    }

    log.info("enable done mode={}", headbob::modeToString(headbob::Config::get().mode));
    return true;
}

bool RealisticHeadBobMod::disable() {
    unhookAll();
    return true;
}

bool RealisticHeadBobMod::unload() {
    unhookAll();
    return true;
}

bool RealisticHeadBobMod::resolveAndHook() {
    auto& log = getSelf().getLogger();
    auto& cfg = headbob::Config::get();

    std::string sig = cfg.cameraBlendSig.empty() ? kCameraBlendSig : cfg.cameraBlendSig;
    const std::string modName = cfg.moduleName.empty() ? "libminecraftpe.so" : cfg.moduleName;

    const uintptr_t addr = pl::memory::resolveSignature(sig, modName);
    if (addr == 0) {
        log.error("resolveSignature failed for CameraBlendSystemTick");
        return false;
    }

    log.info("CameraBlendSystemTick @ {:#x}", static_cast<unsigned long long>(addr));

    mHookTarget = reinterpret_cast<void*>(addr);
    if (pl::memory::hook(mHookTarget,
                         reinterpret_cast<void*>(&RealisticHeadBobMod::cameraBlendDetour),
                         reinterpret_cast<void**>(&mOrigCameraBlend)) != 0) {
        log.error("pl::memory::hook failed");
        mHookTarget = nullptr;
        mOrigCameraBlend = nullptr;
        return false;
    }
    return true;
}

bool RealisticHeadBobMod::resolveAndHookPlayerTick() {
    auto& log = getSelf().getLogger();
    auto& cfg = headbob::Config::get();

    std::string sig = cfg.localPlayerTickSig.empty() ? kLocalPlayerTickSig : cfg.localPlayerTickSig;
    const std::string modName = cfg.moduleName.empty() ? "libminecraftpe.so" : cfg.moduleName;

    const uintptr_t addr = pl::memory::resolveSignature(sig, modName);
    if (addr == 0) {
        log.error("resolveSignature failed for NormalTick");
        return false;
    }

    log.info("NormalTick @ {:#x}", static_cast<unsigned long long>(addr));

    mTickTarget = reinterpret_cast<void*>(addr);
    if (pl::memory::hook(mTickTarget,
                         reinterpret_cast<void*>(&RealisticHeadBobMod::playerTickDetour),
                         reinterpret_cast<void**>(&mOrigPlayerTick)) != 0) {
        log.error("pl::memory::hook failed for NormalTick");
        mTickTarget = nullptr;
        mOrigPlayerTick = nullptr;
        return false;
    }
    return true;
}

void RealisticHeadBobMod::unhookAll() {
    if (mTickTarget && mOrigPlayerTick) {
        pl::memory::unhook(mTickTarget,
                           reinterpret_cast<void*>(&RealisticHeadBobMod::playerTickDetour));
    }
    mTickTarget = nullptr;
    mOrigPlayerTick = nullptr;

    if (mHookTarget && mOrigCameraBlend) {
        pl::memory::unhook(mHookTarget,
                           reinterpret_cast<void*>(&RealisticHeadBobMod::cameraBlendDetour));
    }
    mHookTarget = nullptr;
    mOrigCameraBlend = nullptr;
}

void RealisticHeadBobMod::cameraBlendDetour(void* component, void* blendState, float factor) {
    auto& self = RealisticHeadBobMod::instance();
    if (self.mOrigCameraBlend) {
        self.mOrigCameraBlend(component, blendState, factor);
    }
    self.applyToCamera(component, factor);
}

void RealisticHeadBobMod::playerTickDetour(void* actor) {
    auto& self = RealisticHeadBobMod::instance();
    if (self.mOrigPlayerTick) self.mOrigPlayerTick(actor);
    self.onPlayerTick(actor);
}

void RealisticHeadBobMod::onPlayerTick(void* actor) {
    if (!actor) return;

    const std::uintptr_t stateVector = field(actor, kStateVectorOffset);
    if (!readable(reinterpret_cast<const void*>(stateVector), kPosDeltaOffset + sizeof(Vec3))) {
        return;
    }

    const Vec3 pos = *reinterpret_cast<const Vec3*>(stateVector);
    const Vec3 delta = *reinterpret_cast<const Vec3*>(stateVector + kPosDeltaOffset);
    if (!std::isfinite(pos.x) || !std::isfinite(pos.y) || !std::isfinite(pos.z) ||
        !std::isfinite(delta.x) || !std::isfinite(delta.y) || !std::isfinite(delta.z)) {
        return;
    }
    if (std::fabs(pos.x) >= 3.0e7f || std::fabs(pos.z) >= 3.0e7f || pos.y < -512.f || pos.y > 8192.f) {
        return;
    }

    const double now = nowSeconds();
    float dt = 0.05f;
    if (mLastTickTime > 0.0) dt = static_cast<float>(now - mLastTickTime);
    mLastTickTime = now;
    dt = clampf(dt, 0.001f, 0.25f);

    // The engine already computed this tick's movement; multiply by the tick
    // rate to get blocks per second.
    float speed = std::sqrt(delta.x * delta.x + delta.z * delta.z) * kTicksPerSecond;
    float vy    = delta.y * kTicksPerSecond;

    // Nobody walks faster than this; if the numbers are nonsense (another actor,
    // moved offsets) drop the sample instead of bobbing on garbage.
    if (speed > 15.f || std::fabs(vy) > 60.f) {
        speed = 0.f;
        vy = 0.f;
    }

    const float blend = clampf(dt * 12.f, 0.f, 1.f);
    mSmoothSpeed += (speed - mSmoothSpeed) * blend;
    mSmoothVy += (vy - mSmoothVy) * blend;

    mSpeedXZ = mSmoothSpeed > 0.05f ? mSmoothSpeed : 0.f;
    mVerticalSpeed = mSmoothVy;
    // A grounded Bedrock actor still reports about -1.6 b/s of vertical delta
    // every tick (gravity applied, then resolved by collision), so "vertical
    // speed is zero" is the wrong test. Grounded means "not actually falling".
    mOnGround = mSmoothVy > -3.f;

    mLastX = pos.x;
    mLastY = pos.y;
    mLastZ = pos.z;
    mHasLastPos = true;
}

void RealisticHeadBobMod::applyToCamera(void* component, float /*factor*/) {
    auto& cfg = headbob::Config::get();
    if (!cfg.enabled || !component) return;

    // dt from the render path; clamp like CameraOverhaul
    const double now = nowSeconds();
    float dt = 1.f / 60.f;
    if (mLastFrameTime > 0.0) dt = static_cast<float>(now - mLastFrameTime);
    mLastFrameTime = now;
    dt = clampf(dt, 1.f / 1000.f, 0.1f);

    static float gameTime = 0.f;
    gameTime += dt;

    if (gameTime > 1.0e6f) gameTime = std::fmod(gameTime, 1.0e6f);

    const float speedXZ   = mSpeedXZ;
    const bool  onGround  = mOnGround;
    const bool  sprinting = mSpeedXZ > 5.0f;
    const bool  mounted   = false;
    const bool  firstPerson = true;

    const auto delta = headbob::update(
        mState, cfg, dt,
        speedXZ, onGround, sprinting, mounted, firstPerson, gameTime);

    // HeadBob returns degrees → radians for local bias
    float pitchBias = delta.pitch * kDeg2Rad;
    float yawBias   = delta.yaw * kDeg2Rad;
    float rollBias  = 0.f;

    if (!std::isfinite(pitchBias) || !std::isfinite(yawBias)) return;
    if (std::fabs(pitchBias) < 1e-7f && std::fabs(yawBias) < 1e-7f) return;

    pitchBias = clampf(pitchBias, -0.7f, 0.7f);
    yawBias   = clampf(yawBias, -0.7f, 0.7f);

    auto* quat = reinterpret_cast<Quat*>(
        reinterpret_cast<std::uintptr_t>(component) + kRotationOffset);

    const float len = std::sqrt(quat->x * quat->x + quat->y * quat->y +
                                quat->z * quat->z + quat->w * quat->w);
    if (!(len > 0.5f && len < 1.5f)) return;

    Quat d = axisAngle(1.f, 0.f, 0.f, pitchBias);
    if (yawBias != 0.f) d = quatMul(d, axisAngle(0.f, 1.f, 0.f, yawBias));
    if (rollBias != 0.f) d = quatMul(d, axisAngle(0.f, 0.f, 1.f, rollBias));

    *quat = quatNormalize(quatMul(*quat, d));
}

PL_REGISTER_MOD(RealisticHeadBobMod, RealisticHeadBobMod::instance())
