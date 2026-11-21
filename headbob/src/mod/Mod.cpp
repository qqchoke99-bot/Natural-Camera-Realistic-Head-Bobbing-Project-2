#include "mod/Mod.h"

#include <pl/Mod.hpp>
#include <pl/memory/Hook.hpp>
#include <pl/memory/Signature.hpp>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

#include "version/Profile.h"

/*
 * Camera path: CameraOverhaul (GPL-3.0, Mirsario-derived) - hook
 * CameraBlendSystemTick, multiply the camera quaternion at component + 0x28.
 *
 * Head-bob math: spring-damper port of Realistic Head Bobbing (Java), unchanged
 * in mod/HeadBob.cpp.
 *
 * What was missing in 1.0.0: the velocity input. It passed horizontalSpeed = 0,
 * so the step phase could never start, and dt was a hard-coded 1/60. This build
 * hooks the actor tick too, derives the real speed, and integrates the springs
 * with the measured frame time.
 */

namespace {

// CameraOverhaul Signatures.cpp - CameraBlendSystemTick. Byte-identical in
// 1.26.50 and 1.26.62 (verified: unique hit in both libraries).
constexpr const char* kCameraBlendSig =
    "? ? ? D1 ? ? ? 6D ? ? ? 6D ? ? ? 6D ? ? ? 6D ? ? ? A9 ? ? ? F9 ? ? ? A9 ? ? ? A9 ? ? ? 91 55 D0 3B D5 F3 03 01 AA F4 03 00 AA ? ? ? F9 ? ? ? 91 ? ? ? 91";

// CameraOverhaul Signatures.cpp - NormalTick (the local player tick).
constexpr const char* kLocalPlayerTickSig =
    "? ? ? FC ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? 91 ? ? ? D1 54 D0 3B D5 F3 03 00 AA ? ? ? F9 ? ? ? F8 ? ? ? 39";

constexpr float kPi      = 3.14159265358979323846f;
constexpr float kDeg2Rad = kPi / 180.0f;

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

bool finiteVec(const headbob::memory::Vec3& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

bool plausiblePosition(const headbob::memory::Vec3& p) {
    if (!finiteVec(p)) return false;
    if (std::fabs(p.x) >= 3.0e7f || std::fabs(p.z) >= 3.0e7f) return false;
    return p.y > -512.f && p.y < 8192.f;
}

} // namespace

RealisticHeadBobMod& RealisticHeadBobMod::instance() {
    static RealisticHeadBobMod inst;
    return inst;
}

bool RealisticHeadBobMod::load(pl::mod::ModContext& context) {
    mContext = &context;
    auto& log = logger();
    log.info("Realistic Head Bob {} (build target: {}, state vector {:#x})",
             context.info().version,
             headbob::profile::layout().version,
             static_cast<unsigned long long>(headbob::profile::layout().stateVectorComponent));

    const auto cfgPath = context.configDir() / "config.json";
    auto& cfg = headbob::Config::get();
    if (!cfg.loadFromFile(cfgPath.string())) {
        cfg.loadDefaults();
        if (cfg.cameraBlendSig.empty())  cfg.cameraBlendSig = kCameraBlendSig;
        if (cfg.localPlayerTickSig.empty()) cfg.localPlayerTickSig = kLocalPlayerTickSig;
        std::error_code ec;
        std::filesystem::create_directories(cfgPath.parent_path(), ec);
        cfg.saveToFile(cfgPath.string());
        log.info("Wrote default config");
    } else {
        if (cfg.cameraBlendSig.empty())  cfg.cameraBlendSig = kCameraBlendSig;
        if (cfg.localPlayerTickSig.empty()) cfg.localPlayerTickSig = kLocalPlayerTickSig;
        log.info("Config loaded mode={}", headbob::modeToString(cfg.mode));
    }
    return true;
}

bool RealisticHeadBobMod::enable(pl::mod::ModContext&) {
    auto& log = logger();
    // No ModuleBuilder on purpose: Levi 1.5.25 crashed in registerModule.

    const bool cameraOk = hookCameraBlend();
    const bool tickOk   = hookPlayerTick();

    if (cameraOk && tickOk) {
        log.info("hooks ready - head bob active");
    } else if (cameraOk) {
        log.warn("camera hook ready but the player tick hook failed: only idle sway will run");
    } else {
        log.error("camera hook failed - this package is built for another Minecraft build?");
    }

    log.info("enable done mode={}", headbob::modeToString(headbob::Config::get().mode));
    return cameraOk;
}

bool RealisticHeadBobMod::disable(pl::mod::ModContext&) {
    unhookAll();
    return true;
}

bool RealisticHeadBobMod::unload(pl::mod::ModContext&) {
    unhookAll();
    return true;
}

bool RealisticHeadBobMod::hookCameraBlend() {
    auto& log = logger();
    auto& cfg = headbob::Config::get();

    const std::string sig = cfg.cameraBlendSig.empty() ? kCameraBlendSig : cfg.cameraBlendSig;
    const std::string modName = cfg.moduleName.empty() ? "libminecraftpe.so" : cfg.moduleName;

    const std::uintptr_t addr = pl::memory::resolveSignature(sig, modName);
    if (addr == 0) {
        log.error("resolveSignature failed for CameraBlendSystemTick");
        return false;
    }
    log.info("CameraBlendSystemTick @ {:#x}", static_cast<unsigned long long>(addr));

    mCameraTarget = reinterpret_cast<void*>(addr);
    if (pl::memory::hook(mCameraTarget,
                         reinterpret_cast<void*>(&RealisticHeadBobMod::cameraBlendDetour),
                         reinterpret_cast<void**>(&mOrigCameraBlend)) != 0) {
        log.error("pl::memory::hook failed for CameraBlendSystemTick");
        mCameraTarget = nullptr;
        mOrigCameraBlend = nullptr;
        return false;
    }
    return true;
}

bool RealisticHeadBobMod::hookPlayerTick() {
    auto& log = logger();
    auto& cfg = headbob::Config::get();

    const std::string sig = cfg.localPlayerTickSig.empty() ? kLocalPlayerTickSig : cfg.localPlayerTickSig;
    const std::string modName = cfg.moduleName.empty() ? "libminecraftpe.so" : cfg.moduleName;

    const std::uintptr_t addr = pl::memory::resolveSignature(sig, modName);
    if (addr == 0) {
        log.error("resolveSignature failed for NormalTick");
        return false;
    }
    log.info("NormalTick @ {:#x}", static_cast<unsigned long long>(addr));

    mTickTarget = reinterpret_cast<void*>(addr);
    if (pl::memory::hook(mTickTarget,
                         reinterpret_cast<void*>(&RealisticHeadBobMod::tickDetour),
                         reinterpret_cast<void**>(&mOrigTick)) != 0) {
        log.error("pl::memory::hook failed for NormalTick");
        mTickTarget = nullptr;
        mOrigTick = nullptr;
        return false;
    }
    return true;
}

void RealisticHeadBobMod::unhookAll() {
    if (!mContext) return;
    auto& log = logger();

    if (mTickTarget && mOrigTick) {
        pl::memory::unhook(mTickTarget, reinterpret_cast<void*>(&RealisticHeadBobMod::tickDetour));
        log.info("NormalTick unhooked");
    }
    if (mCameraTarget && mOrigCameraBlend) {
        pl::memory::unhook(mCameraTarget, reinterpret_cast<void*>(&RealisticHeadBobMod::cameraBlendDetour));
        log.info("CameraBlendSystemTick unhooked");
    }
    mTickTarget = nullptr;
    mOrigTick = nullptr;
    mCameraTarget = nullptr;
    mOrigCameraBlend = nullptr;
}

void RealisticHeadBobMod::cameraBlendDetour(void* component, void* blendState, float factor) {
    auto& self = RealisticHeadBobMod::instance();
    if (self.mOrigCameraBlend) self.mOrigCameraBlend(component, blendState, factor);
    self.applyToCamera(component);
}

void RealisticHeadBobMod::tickDetour(void* actor) {
    auto& self = RealisticHeadBobMod::instance();
    if (self.mOrigTick) self.mOrigTick(actor);
    self.onActorTick(actor);
}

void RealisticHeadBobMod::onActorTick(void* actor) {
    if (!actor) return;

    const auto& layout = headbob::profile::layout();
    const auto* base = static_cast<const std::uint8_t*>(actor);

    void* stateVector = nullptr;
    void* rotationComponent = nullptr;
    std::memcpy(&stateVector, base + layout.stateVectorComponent, sizeof(stateVector));
    std::memcpy(&rotationComponent, base + layout.actorRotationComponent, sizeof(rotationComponent));

    // Never follow a pointer that a moved structure offset might have produced.
    if (!headbob::memory::readable(stateVector, sizeof(headbob::memory::Vec3)) ||
        !headbob::memory::readable(rotationComponent, sizeof(headbob::memory::Vec2))) {
        mHasSample.store(false, std::memory_order_relaxed);
        return;
    }

    const headbob::memory::Vec3 pos = headbob::memory::field<headbob::memory::Vec3>(stateVector, 0);
    const headbob::memory::Vec2 rot = headbob::memory::field<headbob::memory::Vec2>(rotationComponent, 0);
    if (!plausiblePosition(pos) || !std::isfinite(rot.x) || !std::isfinite(rot.y)) {
        mHasSample.store(false, std::memory_order_relaxed);
        return;
    }

    const double now = nowSeconds();
    float dt = 0.05f;
    if (mLastTickTime > 0.0) dt = static_cast<float>(now - mLastTickTime);
    mLastTickTime = now;
    dt = clampf(dt, 0.001f, 0.25f);

    if (!mHasLastPos) {
        mLastPos = pos;
        mHasLastPos = true;
    }

    const float vx = (pos.x - mLastPos.x) / dt;
    const float vy = (pos.y - mLastPos.y) / dt;
    const float vz = (pos.z - mLastPos.z) / dt;
    mLastPos = pos;

    const float blend = clampf(dt * 12.f, 0.f, 1.f);
    mSmoothSpeed += (std::sqrt(vx * vx + vz * vz) - mSmoothSpeed) * blend;
    mSmoothVy += (vy - mSmoothVy) * blend;

    mSpeed.store(mSmoothSpeed > 0.05f ? mSmoothSpeed : 0.f, std::memory_order_relaxed);
    mVerticalSpeed.store(mSmoothVy, std::memory_order_relaxed);
    // Bedrock has no cheap onGround query here: treat "barely moving vertically"
    // as grounded. Jump/fall produce a clear vertical speed within one tick.
    mOnGround.store(std::fabs(mSmoothVy) < 0.8f, std::memory_order_relaxed);
    // Walk ~4.3 b/s, sprint ~5.6 b/s.
    mSprinting.store(mSmoothSpeed > 5.0f, std::memory_order_relaxed);
    mHasSample.store(true, std::memory_order_relaxed);
}

void RealisticHeadBobMod::applyToCamera(void* component) {
    auto& cfg = headbob::Config::get();
    if (!cfg.enabled || !component) return;
    if (!mHasSample.load(std::memory_order_relaxed)) return;

    const double now = nowSeconds();
    float dt = 1.f / 60.f;
    if (mLastFrameTime > 0.0) dt = static_cast<float>(now - mLastFrameTime);
    mLastFrameTime = now;
    dt = clampf(dt, 1.f / 1000.f, 0.1f);
    mGameTime += dt;

    if (!mLoggedSample) {
        char buffer[192];
        std::snprintf(buffer, sizeof(buffer),
                      "first sample speed=%.2f b/s vy=%.2f onGround=%d sprint=%d (profile %s)",
                      static_cast<double>(mSpeed.load(std::memory_order_relaxed)),
                      static_cast<double>(mVerticalSpeed.load(std::memory_order_relaxed)),
                      mOnGround.load(std::memory_order_relaxed) ? 1 : 0,
                      mSprinting.load(std::memory_order_relaxed) ? 1 : 0,
                      headbob::profile::layout().version);
        mLoggedSample = true;
        logger().info("{}", buffer);
    }

    const auto delta = headbob::update(
        mState, cfg, dt,
        mSpeed.load(std::memory_order_relaxed),
        mOnGround.load(std::memory_order_relaxed),
        mSprinting.load(std::memory_order_relaxed),
        /*isMounted=*/false,
        /*isFirstPerson=*/true,
        mGameTime);

    float pitchBias = delta.pitch * kDeg2Rad;
    float yawBias   = delta.yaw * kDeg2Rad;

    if (!std::isfinite(pitchBias) || !std::isfinite(yawBias)) return;
    if (std::fabs(pitchBias) < 1e-7f && std::fabs(yawBias) < 1e-7f) return;

    pitchBias = clampf(pitchBias, -0.7f, 0.7f);
    yawBias   = clampf(yawBias, -0.7f, 0.7f);

    auto* quat = reinterpret_cast<Quat*>(
        reinterpret_cast<std::uintptr_t>(component) + headbob::profile::layout().cameraRotation);

    const float len = std::sqrt(quat->x * quat->x + quat->y * quat->y +
                                quat->z * quat->z + quat->w * quat->w);
    if (!(len > 0.5f && len < 1.5f)) return;

    Quat d = axisAngle(1.f, 0.f, 0.f, pitchBias);
    if (yawBias != 0.f) d = quatMul(d, axisAngle(0.f, 1.f, 0.f, yawBias));

    *quat = quatNormalize(quatMul(*quat, d));
}

PL_REGISTER_MOD(RealisticHeadBobMod, RealisticHeadBobMod::instance());
