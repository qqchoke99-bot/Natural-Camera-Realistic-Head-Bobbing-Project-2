/*
 * CameraOverhaul for Minecraft Bedrock (LeviLauncher).
 *
 * Inspired by and derived from the camera math of "CameraOverhaul" by
 * Mirsario & Contributors, a Java Edition mod:
 *   https://www.curseforge.com/minecraft/mc-mods/cameraoverhaul
 *   https://github.com/Mirsario/Minecraft-CameraOverhaul
 *
 * That project is licensed under GPL-3.0, and so is this one.
 */

#include "CameraOverhaulModule.hpp"

#include "core/memory/Hooks.hpp"
#include <cameraoverhaul/events/EventBus.hpp>
#include <cameraoverhaul/events/LocalPlayerTickEvent.hpp>
#include <cameraoverhaul/memory/Signatures.hpp>
#include <cameraoverhaul/sdk/world/Actor.hpp>

#include <chrono>
#include <cmath>
#include <cstdint>

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kDeg2Rad = kPi / 180.0f;

/*
 * Velocity here is in blocks per second, so the Java factors cannot be reused
 * directly. These gains were chosen so the effect reads clearly without
 * causing motion sickness:
 *   forward  0.45 -> about 2.5 deg sprinting, about 13 deg in an elytra dive
 *   vertical 0.30 -> about 6 deg at terminal fall velocity
 */
constexpr float kForwardPitchGain = 0.45f;
constexpr float kVerticalPitchGain = 0.30f;

/*
 * The camera transform written by CameraBlendSystem::_tick:
 *   +0x28  quaternion (x, y, z, w)
 *   +0x38  Vec3 position
 */
constexpr std::uintptr_t kRotationOffset = 0x28;

struct Quat {
    float x, y, z, w;
};

float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

float dampStep(float smoothing, float dt) {
    if (smoothing <= 0.001f) return 1.0f;
    return 1.0f - std::exp(-(16.0f / smoothing) * dt);
}

float easeInOutCubic(float t) {
    if (t < 0.5f) return 4.0f * t * t * t;
    float f = -2.0f * t + 2.0f;
    return 1.0f - (f * f * f) * 0.5f;
}

float signf(float v) { return v < 0.0f ? -1.0f : 1.0f; }

/*
 * The bias is applied by post-multiplying the engine quaternion with a small
 * local rotation. This never converts to Euler angles, so it cannot hit gimbal
 * lock at the poles and does not depend on the engine's Euler convention,
 * which is not observable from the disassembly.
 */
Quat quatMul(const Quat& a, const Quat& b) {
    return Quat{
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z
    };
}

Quat axisAngle(float ax, float ay, float az, float angle) {
    float h = angle * 0.5f;
    float s = std::sin(h);
    return Quat{ax * s, ay * s, az * s, std::cos(h)};
}

Quat quatNormalize(const Quat& q) {
    float n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (!(n > 1e-8f)) return Quat{0.0f, 0.0f, 0.0f, 1.0f};
    return Quat{q.x / n, q.y / n, q.z / n, q.w / n};
}

/*
 * Deterministic value noise used for the idle sway. SimplexNoise in the
 * original is only sampled along one axis per channel, so smoothed 1D gradient
 * noise is perceptually equivalent here.
 */
float hashNoise(int i, int seed) {
    std::uint32_t h = static_cast<std::uint32_t>(i) * 374761393u
                    + static_cast<std::uint32_t>(seed) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= (h >> 16);
    return (static_cast<float>(h & 0xFFFFFFu) / 8388607.5f) - 1.0f;
}

float noise1D(float x, int seed) {
    float fx = std::floor(x);
    int i = static_cast<int>(fx);
    float f = x - fx;
    float u = f * f * (3.0f - 2.0f * f);
    float a = hashNoise(i, seed);
    float b = hashNoise(i + 1, seed);
    return a + (b - a) * u;
}

double nowSeconds() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

struct CameraState {
    float prevYaw = 0.0f;
    bool  hasPrevYaw = false;

    float rollAccumulator = 0.0f;
    float roll = 0.0f;

    float pitchVertical = 0.0f;
    float pitchForward = 0.0f;

    float idleTime = 0.0f;
    float swayFade = 0.0f;

    cameraoverhaul::sdk::Vec3 lastPos{};
    cameraoverhaul::sdk::Vec3 velocity{};
    bool  hasLastPos = false;

    double lastTickTime = 0.0;

    void reset() { *this = CameraState{}; }
};

CameraState g_state;

using CameraBlendTickFn = void (*)(void* component, void* blendState, float factor);
CameraBlendTickFn _cameraBlendTick_orig = nullptr;

void _cameraBlendTick_hook(void* component, void* blendState, float factor) {
    if (_cameraBlendTick_orig) {
        _cameraBlendTick_orig(component, blendState, factor);
    }
    CameraOverhaulModule::get().applyToCamera(component);
}

}

CameraOverhaulModule& CameraOverhaulModule::get() {
    static CameraOverhaulModule instance;
    return instance;
}

void CameraOverhaulModule::resetState() {
    g_state.reset();
}

void CameraOverhaulModule::setEnabled(bool state) {
    if (m_enabled == state) return;
    m_enabled = state;
    resetState();
}

void CameraOverhaulModule::init() {
    if (!m_hooked) {
        const auto addr = cameraoverhaul::memory::resolve(
            cameraoverhaul::memory::SignatureId::CameraBlendSystemTick);
        if (addr) {
            if (cameraoverhaul::hooks::install(reinterpret_cast<void*>(addr),
                                               reinterpret_cast<void*>(_cameraBlendTick_hook),
                                               reinterpret_cast<void**>(&_cameraBlendTick_orig))) {
                m_hooked = true;
            }
        }
    }

    cameraoverhaul::events::bus().subscribe<cameraoverhaul::events::LocalPlayerTickEvent>(
        [](cameraoverhaul::events::LocalPlayerTickEvent& event) {
            CameraOverhaulModule::get().onTick(event.player);
        });
}

void CameraOverhaulModule::applyToCamera(void* cameraComponent) {
    if (!m_enabled || !cameraComponent) return;

    auto* quat = reinterpret_cast<Quat*>(
        reinterpret_cast<std::uintptr_t>(cameraComponent) + kRotationOffset);

    const float len = std::sqrt(quat->x * quat->x + quat->y * quat->y +
                                quat->z * quat->z + quat->w * quat->w);
    if (!(len > 0.5f && len < 1.5f)) return;

    float pitchBias = 0.0f;
    float yawBias = 0.0f;
    float rollBias = 0.0f;

    if (m_enablePitch) {
        pitchBias += (g_state.pitchVertical + g_state.pitchForward) * kDeg2Rad;
    }
    if (m_enableRoll) {
        rollBias += g_state.roll * kDeg2Rad;
    }

    if (m_enableSway && m_swayIntensity > 0.0f && g_state.swayFade > 0.0f) {
        const float t = static_cast<float>(nowSeconds());
        const float freq = m_swayFrequency;
        const float amp = m_swayIntensity * g_state.swayFade;
        const float f = amp * amp * amp;
        pitchBias += noise1D(t * freq, 420) * f * 0.35f * kDeg2Rad;
        yawBias   += noise1D(t * freq, 1337) * f * 0.35f * kDeg2Rad;
        rollBias  += noise1D(t * freq, 6969) * f * 0.50f * kDeg2Rad;
    }

    if (!std::isfinite(pitchBias) || !std::isfinite(yawBias) || !std::isfinite(rollBias)) {
        return;
    }

    if (std::fabs(pitchBias) < 1e-6f &&
        std::fabs(yawBias) < 1e-6f &&
        std::fabs(rollBias) < 1e-6f) {
        return;
    }

    pitchBias = clampf(pitchBias, -0.7f, 0.7f);
    yawBias   = clampf(yawBias, -0.7f, 0.7f);
    rollBias  = clampf(rollBias, -0.7f, 0.7f);

    Quat delta = axisAngle(1.0f, 0.0f, 0.0f, pitchBias);
    if (yawBias != 0.0f)  delta = quatMul(delta, axisAngle(0.0f, 1.0f, 0.0f, yawBias));
    if (rollBias != 0.0f) delta = quatMul(delta, axisAngle(0.0f, 0.0f, 1.0f, rollBias));

    *quat = quatNormalize(quatMul(*quat, delta));
}

void CameraOverhaulModule::onTick(void* playerPtr) {
    if (!m_enabled || !playerPtr) return;

    auto* player = reinterpret_cast<cameraoverhaul::sdk::Player*>(playerPtr);

    const double now = nowSeconds();
    float dt = 0.05f;
    if (g_state.lastTickTime > 0.0) {
        dt = static_cast<float>(now - g_state.lastTickTime);
    }
    g_state.lastTickTime = now;
    dt = clampf(dt, 0.001f, 0.25f);

    const cameraoverhaul::sdk::Vec3 pos = player->position();
    if (!g_state.hasLastPos) {
        g_state.lastPos = pos;
        g_state.hasLastPos = true;
    }

    const cameraoverhaul::sdk::Vec3 rawVelocity{
        (pos.x - g_state.lastPos.x) / dt,
        (pos.y - g_state.lastPos.y) / dt,
        (pos.z - g_state.lastPos.z) / dt
    };
    g_state.lastPos = pos;

    const float blend = clampf(dt * 12.0f, 0.0f, 1.0f);
    g_state.velocity.x += (rawVelocity.x - g_state.velocity.x) * blend;
    g_state.velocity.y += (rawVelocity.y - g_state.velocity.y) * blend;
    g_state.velocity.z += (rawVelocity.z - g_state.velocity.z) * blend;

    const cameraoverhaul::sdk::Vec2 rot = player->rotation();
    const float yaw = rot.y;
    const float pitch = rot.x;
    if (!g_state.hasPrevYaw) {
        g_state.prevYaw = yaw;
        g_state.hasPrevYaw = true;
    }

    float yawDelta = g_state.prevYaw - yaw;
    while (yawDelta > 180.0f) yawDelta -= 360.0f;
    while (yawDelta < -180.0f) yawDelta += 360.0f;
    g_state.prevYaw = yaw;

    const float yawRad = yaw * kDeg2Rad;
    const float sinY = std::sin(-yawRad);
    const float cosY = std::cos(-yawRad);
    const float forwardZ = g_state.velocity.z * cosY - g_state.velocity.x * sinY;
    const float forwardX = g_state.velocity.z * sinY + g_state.velocity.x * cosY;

    const float accumulation = 0.0048f * m_turningRollAccumulation;
    const float intensity = 1.25f * m_turningRollIntensity;
    const float smoothing = 0.0825f * m_turningRollSmoothing;

    const float pitchScale = std::cos(pitch * kDeg2Rad);
    g_state.rollAccumulator = clampf(
        g_state.rollAccumulator + yawDelta * accumulation * pitchScale, -1.0f, 1.0f);
    g_state.rollAccumulator -= g_state.rollAccumulator * dampStep(smoothing * 100.0f, dt);

    const float rollFromTurning =
        clampf(easeInOutCubic(std::fabs(g_state.rollAccumulator)), 0.0f, 1.0f)
        * intensity * signf(g_state.rollAccumulator);
    const float rollFromStrafing = -forwardX * m_strafingRollIntensity;

    const float rollGoal = rollFromTurning + rollFromStrafing;
    g_state.roll += (rollGoal - g_state.roll) * dampStep(m_transitionSmoothing, dt);

    float verticalTarget = g_state.velocity.y;
    if (std::fabs(verticalTarget) < 0.4f) verticalTarget = 0.0f;
    verticalTarget *= kVerticalPitchGain * m_verticalPitchIntensity;
    g_state.pitchVertical +=
        (verticalTarget - g_state.pitchVertical) * dampStep(m_verticalPitchSmoothing, dt);

    const float forwardTarget = forwardZ * kForwardPitchGain * m_forwardPitchIntensity;
    g_state.pitchForward +=
        (forwardTarget - g_state.pitchForward) * dampStep(m_forwardPitchSmoothing, dt);

    const float speed = std::sqrt(g_state.velocity.x * g_state.velocity.x +
                                  g_state.velocity.z * g_state.velocity.z);
    if (speed < 0.05f) {
        g_state.idleTime += dt;
    } else {
        g_state.idleTime = 0.0f;
    }

    float fadeTarget = 0.0f;
    if (g_state.idleTime > m_swayFadeInDelay) {
        const float over = g_state.idleTime - m_swayFadeInDelay;
        fadeTarget = m_swayFadeInLength <= 0.0f ? 1.0f : clampf(over / m_swayFadeInLength, 0.0f, 1.0f);
    }
    const float fadeRate = fadeTarget > g_state.swayFade ? m_swayFadeInLength : m_swayFadeOutLength;
    const float fadeStep = fadeRate <= 0.0f ? 1.0f : clampf(dt / fadeRate, 0.0f, 1.0f);
    g_state.swayFade += (fadeTarget - g_state.swayFade) * fadeStep;
}

void CameraOverhaulModule::loadConfig(const nlohmann::json& j) {
    if (j.contains("enabled")) {
        const bool state = j["enabled"].get<bool>();
        if (state != m_enabled) setEnabled(state);
    }
    if (j.contains("enablePitch"))             m_enablePitch             = j["enablePitch"].get<bool>();
    if (j.contains("forwardPitchIntensity"))   m_forwardPitchIntensity   = j["forwardPitchIntensity"].get<float>();
    if (j.contains("forwardPitchSmoothing"))   m_forwardPitchSmoothing   = j["forwardPitchSmoothing"].get<float>();
    if (j.contains("verticalPitchIntensity"))  m_verticalPitchIntensity  = j["verticalPitchIntensity"].get<float>();
    if (j.contains("verticalPitchSmoothing"))  m_verticalPitchSmoothing  = j["verticalPitchSmoothing"].get<float>();
    if (j.contains("enableRoll"))              m_enableRoll              = j["enableRoll"].get<bool>();
    if (j.contains("turningRollIntensity"))    m_turningRollIntensity    = j["turningRollIntensity"].get<float>();
    if (j.contains("turningRollAccumulation")) m_turningRollAccumulation = j["turningRollAccumulation"].get<float>();
    if (j.contains("turningRollSmoothing"))    m_turningRollSmoothing    = j["turningRollSmoothing"].get<float>();
    if (j.contains("strafingRollIntensity"))   m_strafingRollIntensity   = j["strafingRollIntensity"].get<float>();
    if (j.contains("enableSway"))              m_enableSway              = j["enableSway"].get<bool>();
    if (j.contains("swayIntensity"))           m_swayIntensity           = j["swayIntensity"].get<float>();
    if (j.contains("swayFrequency"))           m_swayFrequency           = j["swayFrequency"].get<float>();
    if (j.contains("swayFadeInDelay"))         m_swayFadeInDelay         = j["swayFadeInDelay"].get<float>();
    if (j.contains("swayFadeInLength"))        m_swayFadeInLength        = j["swayFadeInLength"].get<float>();
    if (j.contains("swayFadeOutLength"))       m_swayFadeOutLength       = j["swayFadeOutLength"].get<float>();
    if (j.contains("transitionSmoothing"))     m_transitionSmoothing     = j["transitionSmoothing"].get<float>();
}

void CameraOverhaulModule::saveConfig(nlohmann::json& j) {
    j["enabled"]                 = m_enabled;
    j["enablePitch"]             = m_enablePitch;
    j["forwardPitchIntensity"]   = m_forwardPitchIntensity;
    j["forwardPitchSmoothing"]   = m_forwardPitchSmoothing;
    j["verticalPitchIntensity"]  = m_verticalPitchIntensity;
    j["verticalPitchSmoothing"]  = m_verticalPitchSmoothing;
    j["enableRoll"]              = m_enableRoll;
    j["turningRollIntensity"]    = m_turningRollIntensity;
    j["turningRollAccumulation"] = m_turningRollAccumulation;
    j["turningRollSmoothing"]    = m_turningRollSmoothing;
    j["strafingRollIntensity"]   = m_strafingRollIntensity;
    j["enableSway"]              = m_enableSway;
    j["swayIntensity"]           = m_swayIntensity;
    j["swayFrequency"]           = m_swayFrequency;
    j["swayFadeInDelay"]         = m_swayFadeInDelay;
    j["swayFadeInLength"]        = m_swayFadeInLength;
    j["swayFadeOutLength"]       = m_swayFadeOutLength;
    j["transitionSmoothing"]     = m_transitionSmoothing;
}
