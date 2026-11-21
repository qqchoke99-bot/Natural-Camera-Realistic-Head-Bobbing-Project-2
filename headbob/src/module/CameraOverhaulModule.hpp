#pragma once

#include <nlohmann/json.hpp>
#include <cstdint>
#include <string>
#include <string_view>

class CameraOverhaulModule {
public:
    // Parsed once when the config is applied; the frame path must not compare
    // std::strings 60-120 times a second.
    enum class HeadBobMode : std::uint8_t { Default, Bodycam, Comfort, Custom };

    static HeadBobMode parseHeadBobMode(std::string_view value) {
        if (value == "bodycam") return HeadBobMode::Bodycam;
        if (value == "comfort") return HeadBobMode::Comfort;
        if (value == "custom")  return HeadBobMode::Custom;
        return HeadBobMode::Default;
    }

    static CameraOverhaulModule& get();

    static constexpr std::string_view name = "RealisticHeadBob";
    static constexpr std::string_view moduleId = "realistic_headbob.camera";

    void init();
    void setEnabled(bool state);
    bool enabled() const { return m_enabled; }

    void onTick(void* player);
    void applyToCamera(void* cameraComponent);

    void loadConfig(const nlohmann::json& j);
    void saveConfig(nlohmann::json& j);

    void resetState();

    bool  m_enabled = true;

    // --- cinematic (CameraOverhaul) ---
    bool  m_enablePitch = true;
    float m_forwardPitchIntensity = 1.0f;
    float m_forwardPitchSmoothing = 1.0f;
    float m_verticalPitchIntensity = 1.0f;
    float m_verticalPitchSmoothing = 1.0f;

    bool  m_enableRoll = true;
    float m_turningRollIntensity = 1.0f;
    float m_turningRollAccumulation = 1.0f;
    float m_turningRollSmoothing = 1.0f;
    float m_strafingRollIntensity = 1.0f;

    bool  m_enableSway = true;
    float m_swayIntensity = 1.0f;
    float m_swayFrequency = 1.0f;
    float m_swayFadeInDelay = 3.0f;
    float m_swayFadeInLength = 4.0f;
    float m_swayFadeOutLength = 1.0f;

    float m_transitionSmoothing = 1.0f;

    // --- realistic step head-bob ---
    bool        m_enableHeadBob = true;
    std::string m_headBobMode = "bodycam"; // default | bodycam | comfort | custom
    HeadBobMode m_headBobModeKind = HeadBobMode::Bodycam;
    float       m_headBobStrength = 1.0f;
    float       m_headBobStepRate = 1.0f;
    float       m_headBobSmoothHz = 1.75f;
    float       m_headBobDamping = 1.25f;

    // Every preset carries its own strength, so switching preset changes how
    // strong the bob is without touching the master strength.
    float       m_headBobStrengthDefault = 1.00f;
    float       m_headBobStrengthBodycam = 1.15f;
    float       m_headBobStrengthComfort = 0.70f;
    float       m_headBobStrengthCustom  = 1.00f;

    // What a preset actually applies. Default / Bodycam / Comfort are fixed
    // recipes (only their strength is editable); Custom reads the live sliders.
    struct HeadBobRecipe {
        float strength;  // preset strength, before the master gain
        float stepAmp;
        float rate;
        float smoothHz;
        float damping;
    };

    HeadBobRecipe headBobRecipe() const;

    // A preset also scales the cinematic camera motion, so Default / Bodycam /
    // Comfort / Custom differ in pitch, roll and idle sway - not just in bob.
    bool  m_presetScalesCameraMotion = true;
    float m_cameraMotionDefault = 1.00f;
    float m_cameraMotionBodycam = 1.30f;
    float m_cameraMotionComfort = 0.50f;
    float m_cameraMotionCustom  = 1.00f;

    // 1.0 when the toggle is off, so the sliders keep their authored values.
    float cameraMotionGain() const;

private:
    CameraOverhaulModule() = default;

    bool m_hooked = false;
};
