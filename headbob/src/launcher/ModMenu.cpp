#include "ModMenu.hpp"

#include "config/ConfigManager.hpp"
#include "module/CameraOverhaulModule.hpp"

#include <pl/ModMenu.hpp>
#include <pl/ModMenuConfig.hpp>

#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pl::modmenu {
// The V2 config schema API landed on preloader main after the 0.2.3 release.
// Declaring these weak keeps the mod loadable on preloaders that do not export
// them: the symbols resolve to null and we stay on the flat V1 entry list.
__attribute__((weak)) bool setConfigSchemaJson(std::string_view moduleId, std::string_view schemaJson);
__attribute__((weak)) void clearConfigSchema(std::string_view moduleId);
}

namespace {

std::string modeName(CameraOverhaulModule::HeadBobMode kind) {
    switch (kind) {
        case CameraOverhaulModule::HeadBobMode::Bodycam: return "bodycam";
        case CameraOverhaulModule::HeadBobMode::Comfort: return "comfort";
        case CameraOverhaulModule::HeadBobMode::Custom:  return "custom";
        case CameraOverhaulModule::HeadBobMode::Default:
        default:                                          return "default";
    }
}

using pl::modmenu::ConfigChoiceStyleV2;
using pl::modmenu::ConfigConditionV2;
using pl::modmenu::ConfigConditionOpV2;
using pl::modmenu::ConfigControlTypeV2;
using pl::modmenu::ConfigNodeV2;
using pl::modmenu::ConfigOptionV2;
using pl::modmenu::ConfigSchemaBuilder;

bool applySchemaV2();

void onToggle(std::string_view, bool enabled) {
    CameraOverhaulModule::get().setEnabled(enabled);
    cameraoverhaul::config::ConfigManager::get().save();
}

void onConfigChanged(std::string_view, std::string_view key, std::string_view value) {
    auto& mod = CameraOverhaulModule::get();

    nlohmann::json j;
    mod.saveConfig(j);

    const std::string safeKey(key);
    const std::string safeValue(value);

    if (!safeValue.empty()) {
        try {
            if (j.contains(safeKey)) {
                if (j[safeKey].is_boolean()) {
                    if (safeValue == "true") j[safeKey] = true;
                    else if (safeValue == "false") j[safeKey] = false;
                } else if (j[safeKey].is_number_float()) {
                    char* end = nullptr;
                    const float parsed = std::strtof(safeValue.c_str(), &end);
                    if (end != safeValue.c_str()) j[safeKey] = parsed;
                } else if (j[safeKey].is_number_integer()) {
                    char* end = nullptr;
                    const long parsed = std::strtol(safeValue.c_str(), &end, 10);
                    if (end != safeValue.c_str()) j[safeKey] = static_cast<int>(parsed);
                } else {
                    j[safeKey] = safeValue;
                }
            }
        } catch (...) {
            return;
        }
    }

    // Accept every spelling the launcher can hand us ("bodycam", "Bodycam",
    // "body_cam", ...) by routing the preset through the one parser.
    if (safeKey == "headBobMode" && !safeValue.empty()) {
        j["headBobMode"] = modeName(CameraOverhaulModule::parseHeadBobMode(safeValue));
    }

    mod.loadConfig(j);
    cameraoverhaul::config::ConfigManager::get().save();

    // Re-publish with the module's own state after a preset change: the
    // launcher's chips read current_value from the schema, and it only learns
    // about a change if the revision moves. Discrete tap, so this is cheap.
    if (safeKey == "headBobMode") (void)applySchemaV2();
}

std::string f(float value) {
    return std::to_string(value);
}

std::string b(bool value) {
    return value ? "true" : "false";
}

// ---------------------------------------------------------------- V2 schema --

// Snapshot of the live config, so every schema node carries its real current
// value. Without it the launcher only knows the values it captured when the
// module registered, and a stale/blank value can hide visible_when sections.
std::map<std::string, std::string> configSnapshot() {
    std::map<std::string, std::string> values;
    nlohmann::json j;
    CameraOverhaulModule::get().saveConfig(j);
    for (auto it = j.begin(); it != j.end(); ++it) {
        const auto& value = it.value();
        if (value.is_boolean()) {
            values[it.key()] = value.get<bool>() ? "true" : "false";
        } else if (value.is_number_float()) {
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "%g", static_cast<double>(value.get<float>()));
            values[it.key()] = buffer;
        } else if (value.is_number_integer()) {
            values[it.key()] = std::to_string(value.get<long long>());
        } else if (value.is_string()) {
            values[it.key()] = value.get<std::string>();
        }
    }
    return values;
}

void bindLiveValue(ConfigNodeV2& node, const std::map<std::string, std::string>& values) {
    if (node.key.empty()) return;
    auto it = values.find(node.key);
    if (it == values.end()) return;
    node.currentValue = it->second;
    node.hasCurrentValue = true;
    if (node.defaultValue.empty()) node.defaultValue = it->second;
}

ConfigNodeV2 makeNode(std::string id, std::string key, std::string category, std::string title,
                      ConfigControlTypeV2 type) {
    static const std::map<std::string, std::string> values = configSnapshot();
    ConfigNodeV2 node;
    node.id = std::move(id);
    node.key = std::move(key);
    node.category = std::move(category);
    node.title = std::move(title);
    node.type = type;
    bindLiveValue(node, values);
    return node;
}

ConfigNodeV2 section(std::string id, std::string category, std::string title) {
    auto node = makeNode(std::move(id), {}, std::move(category), std::move(title),
                         ConfigControlTypeV2::Section);
    return node;
}

ConfigNodeV2 toggle(std::string id, std::string key, std::string category, std::string title,
                    std::string description) {
    auto node = makeNode(std::move(id), std::move(key), std::move(category), std::move(title),
                         ConfigControlTypeV2::Toggle);
    node.description = std::move(description);
    return node;
}

ConfigNodeV2 slider(std::string id, std::string key, std::string category, std::string title,
                    std::string description, std::string minValue, std::string maxValue,
                    std::string step, std::string unit = {}) {
    auto node = makeNode(std::move(id), std::move(key), std::move(category), std::move(title),
                         ConfigControlTypeV2::SliderFloat);
    node.description = std::move(description);
    node.minValue = std::move(minValue);
    node.maxValue = std::move(maxValue);
    node.step = std::move(step);
    node.unit = std::move(unit);
    return node;
}

ConfigNodeV2 info(std::string id, std::string category, std::string title, std::string description) {
    auto node = makeNode(std::move(id), {}, std::move(category), std::move(title),
                         ConfigControlTypeV2::Info);
    node.description = std::move(description);
    return node;
}

void greyOutUnless(ConfigNodeV2& node, std::string key, std::string value) {
    node.enabledWhen.push_back(ConfigConditionV2{std::move(key), ConfigConditionOpV2::Equals,
                                                 std::move(value)});
}

void visibleOnlyFor(ConfigNodeV2& node, std::string value) {
    node.visibleWhen.push_back(ConfigConditionV2{"headBobMode", ConfigConditionOpV2::Equals,
                                                 std::move(value)});
}

void greyOutWithout(ConfigNodeV2& node, std::string key) {
    node.enabledWhen.push_back(ConfigConditionV2{std::move(key), ConfigConditionOpV2::Truthy, {}});
}

std::string buildSchemaJson() {
    ConfigSchemaBuilder schema;

    schema.category("profile", "Preset",
                    "Pick how the camera walks. Every preset keeps its own strength.")
          .category("headbob", "Head Bob",
                    "Tune the step rhythm that sits under every preset.")
          .category("camera", "Camera Motion",
                    "Base amounts for pitch, roll and idle sway. The active preset scales them.")
          .category("advanced", "Advanced",
                    "Global smoothing and file locations.")
          .defaultCategory("profile");

    // --- Preset ---------------------------------------------------------
    schema.node(section("sec_preset", "profile", "Walking profile"));

    auto preset = makeNode("headBobMode", "headBobMode", "profile", "Preset",
                           ConfigControlTypeV2::Choice);
    preset.description =
            "A preset is a whole-camera profile: bob strength, step rhythm and how much the "
            "camera reacts while you move. Only the active preset's controls are shown below.";
    preset.choiceStyle = ConfigChoiceStyleV2::Segmented;
    preset.options = {
            ConfigOptionV2{.value = "default", .label = "Default", .description = "Neutral walking bob and gentle camera motion"},
            ConfigOptionV2{.value = "bodycam", .label = "Bodycam", .description = "Strong bob, heavy pitching and hard banking"},
            ConfigOptionV2{.value = "comfort", .label = "Comfort", .description = "Soft bob, camera motion cut in half or off"},
            ConfigOptionV2{.value = "custom", .label = "Custom", .description = "Your own strength and rhythm sliders"},
    };
    preset.currentValue = modeName(CameraOverhaulModule::get().m_headBobModeKind);
    preset.hasCurrentValue = true;
    preset.defaultValue = "default";
    schema.node(std::move(preset));

    schema.node(info("info_preset", "profile", "What a preset changes",
                     "Presets scale the head bob and the cinematic camera motion (pitch, roll, "
                     "idle sway) together, so Bodycam really feels like a chest-mounted camera "
                     "and Comfort really calms the whole view. Reading the module right now: " +
                             modeName(CameraOverhaulModule::get().m_headBobModeKind) +
                             "."));

    auto scaleMotion = toggle("presetScalesCameraMotion", "presetScalesCameraMotion", "profile",
                              "Preset scales camera motion",
                              "On: each preset multiplies pitch, roll and idle sway by its own "
                              "amount. Off: those sliders apply as written to every preset.");
    schema.node(std::move(scaleMotion));

    struct PresetControls {
        const char* mode;
        const char* strengthKey;
        const char* motionKey;
        const char* name;
        const char* strengthId;
        const char* motionId;
        const char* strengthDefault;
        const char* motionDefault;
    };
    const PresetControls presets[] = {
            {"default", "headBobStrengthDefault", "cameraMotionDefault", "Default",
             "headBobStrengthDefault", "cameraMotionDefault", "1.00", "1.00"},
            {"bodycam", "headBobStrengthBodycam", "cameraMotionBodycam", "Bodycam",
             "headBobStrengthBodycam", "cameraMotionBodycam", "1.15", "1.30"},
            {"comfort", "headBobStrengthComfort", "cameraMotionComfort", "Comfort",
             "headBobStrengthComfort", "cameraMotionComfort", "0.70", "0.50"},
            {"custom", "headBobStrengthCustom", "cameraMotionCustom", "Custom",
             "headBobStrengthCustom", "cameraMotionCustom", "1.00", "1.00"},
    };

    schema.node(section("sec_strength", "profile", "Strength"));
    for (const auto& entry : presets) {
        auto strength = slider(entry.strengthId, entry.strengthKey, "profile",
                               std::string(entry.name) + " strength",
                               std::string("How strong the bob is while ") + entry.name +
                                       " is active, on top of Master strength.",
                               "0.0", "3.0", "0.05", "x");
        visibleOnlyFor(strength, entry.mode);
        greyOutUnless(strength, "headBobMode", entry.mode);
        schema.node(std::move(strength));
    }

    schema.node(section("sec_motion", "profile", "Camera motion"));
    for (const auto& entry : presets) {
        auto motion = slider(entry.motionId, entry.motionKey, "profile",
                             std::string(entry.name) + " camera motion",
                             std::string("Pitch, roll and idle sway multiplier while ") + entry.name +
                                     " is active (1.00 keeps the Camera Motion values as written).",
                             "0.0", "3.0", "0.05", "x");
        visibleOnlyFor(motion, entry.mode);
        greyOutUnless(motion, "headBobMode", entry.mode);
        greyOutWithout(motion, "presetScalesCameraMotion");
        schema.node(std::move(motion));
    }

    // --- Head bob -------------------------------------------------------
    schema.node(section("sec_bob", "headbob", "Step bob"));

    auto headBob = toggle("enableHeadBob", "enableHeadBob", "headbob", "Head bob",
                          "Vertical dip on each footfall plus a side-to-side yaw.");
    schema.node(std::move(headBob));

    auto master = slider("headBobStrength", "headBobStrength", "headbob", "Master strength",
                         "Multiplies the strength of whichever preset is active.",
                         "0.0", "3.0", "0.05", "x");
    greyOutWithout(master, "enableHeadBob");
    schema.node(std::move(master));

    schema.node(section("sec_custom", "headbob", "Custom tuning"));
    schema.node(info("info_custom", "headbob", "Custom preset only",
                     "These three sliders are read by the Custom preset. Default, Bodycam and "
                     "Comfort keep their own baked-in rhythm, so they stay consistent no matter "
                     "what you set here - switch the preset to Custom to feel the difference. "
                     "The Camera Motion sliders below apply to every preset, scaled by it."));

    auto stepRate = slider("headBobStepRate", "headBobStepRate", "headbob", "Step rate",
                           "How fast the step cycle runs while you walk.",
                           "0.25", "2.5", "0.05", "x");
    greyOutWithout(stepRate, "enableHeadBob");
    greyOutUnless(stepRate, "headBobMode", "custom");
    schema.node(std::move(stepRate));

    auto smoothHz = slider("headBobSmoothHz", "headBobSmoothHz", "headbob", "Spring frequency",
                           "Lower is looser and floatier, higher is snappier.",
                           "0.5", "6.0", "0.05", "Hz");
    greyOutWithout(smoothHz, "enableHeadBob");
    greyOutUnless(smoothHz, "headBobMode", "custom");
    schema.node(std::move(smoothHz));

    auto damping = slider("headBobDamping", "headBobDamping", "headbob", "Spring damping",
                          "Below 1.0 overshoots once, above 1.0 settles flat.",
                          "0.2", "3.0", "0.05", "");
    greyOutWithout(damping, "enableHeadBob");
    greyOutUnless(damping, "headBobMode", "custom");
    schema.node(std::move(damping));

    // --- Camera motion --------------------------------------------------
    schema.node(section("sec_pitch", "camera", "Pitch"));

    auto pitch = toggle("enablePitch", "enablePitch", "camera", "Pitch reaction",
                        "Lean the view when you accelerate, brake, or fall.");
    schema.node(std::move(pitch));

    for (const auto& entry : std::vector<ConfigNodeV2>{
                 slider("forwardPitchIntensity", "forwardPitchIntensity", "camera",
                        "Forward pitch intensity", "Nose-dive on forward acceleration.",
                        "0.0", "3.0", "0.05", "x"),
                 slider("forwardPitchSmoothing", "forwardPitchSmoothing", "camera",
                        "Forward pitch smoothing", "How quickly forward pitch settles.",
                        "0.1", "5.0", "0.05", "x"),
                 slider("verticalPitchIntensity", "verticalPitchIntensity", "camera",
                        "Vertical pitch intensity", "Pitch away from your vertical speed.",
                        "0.0", "3.0", "0.05", "x"),
                 slider("verticalPitchSmoothing", "verticalPitchSmoothing", "camera",
                        "Vertical pitch smoothing", "How quickly vertical pitch settles.",
                        "0.1", "5.0", "0.05", "x")}) {
        auto node = entry;
        greyOutWithout(node, "enablePitch");
        schema.node(std::move(node));
    }

    schema.node(section("sec_roll", "camera", "Roll"));

    auto roll = toggle("enableRoll", "enableRoll", "camera", "Roll reaction",
                       "Bank the view when you turn or strafe.");
    schema.node(std::move(roll));

    for (const auto& entry : std::vector<ConfigNodeV2>{
                 slider("turningRollIntensity", "turningRollIntensity", "camera",
                        "Turning roll intensity", "Banking strength while turning.",
                        "0.0", "3.0", "0.05", "x"),
                 slider("turningRollAccumulation", "turningRollAccumulation", "camera",
                        "Turning roll accumulation", "How much a held turn keeps building.",
                        "0.0", "3.0", "0.05", "x"),
                 slider("turningRollSmoothing", "turningRollSmoothing", "camera",
                        "Turning roll smoothing", "How quickly the bank returns to level.",
                        "0.1", "5.0", "0.05", "x"),
                 slider("strafingRollIntensity", "strafingRollIntensity", "camera",
                        "Strafing roll intensity", "Banking strength while strafing.",
                        "0.0", "3.0", "0.05", "x")}) {
        auto node = entry;
        greyOutWithout(node, "enableRoll");
        schema.node(std::move(node));
    }

    schema.node(section("sec_sway", "camera", "Idle sway"));

    auto sway = toggle("enableSway", "enableSway", "camera", "Idle sway",
                       "Slow hand-held drift that fades in after standing still.");
    schema.node(std::move(sway));

    for (const auto& entry : std::vector<ConfigNodeV2>{
                 slider("swayIntensity", "swayIntensity", "camera", "Sway intensity",
                        "Amount of drift at full fade-in.", "0.0", "3.0", "0.05", "x"),
                 slider("swayFrequency", "swayFrequency", "camera", "Sway frequency",
                        "Speed of the drift.", "0.1", "5.0", "0.05", "x"),
                 slider("swayFadeInDelay", "swayFadeInDelay", "camera", "Fade in delay",
                        "Seconds of standing still before the drift starts.",
                        "0.0", "15.0", "0.5", "s"),
                 slider("swayFadeInLength", "swayFadeInLength", "camera", "Fade in length",
                        "Seconds the drift takes to reach full strength.",
                        "0.1", "15.0", "0.1", "s"),
                 slider("swayFadeOutLength", "swayFadeOutLength", "camera", "Fade out length",
                        "Seconds the drift takes to disappear once you move.",
                        "0.1", "15.0", "0.1", "s")}) {
        auto node = entry;
        greyOutWithout(node, "enableSway");
        schema.node(std::move(node));
    }

    // --- Advanced -------------------------------------------------------
    auto overall = slider("transitionSmoothing", "transitionSmoothing", "advanced",
                          "Overall smoothness", "Global gain on every smoothing time above.",
                          "0.2", "4.0", "0.05", "x");
    schema.node(std::move(overall));

    schema.node(info("info_config", "advanced", "Config file",
                     "Everything here is stored in the mod's config.json, so the file and this "
                     "menu always agree."));

    return schema.toJson();
}

bool applySchemaV2() {
    // Preloaders without the V2 API leave the weak symbol unresolved: fall back
    // silently to the flat V1 entry list registered above.
    if (&pl::modmenu::setConfigSchemaJson == nullptr) return false;
    return pl::modmenu::setConfigSchemaJson(std::string(CameraOverhaulModule::moduleId),
                                            buildSchemaJson());
}

} // namespace

void registerWithLauncher() {
    auto& mod = CameraOverhaulModule::get();
    const bool hasSchemaApi = (&pl::modmenu::setConfigSchemaJson != nullptr);

    pl::modmenu::ModuleBuilder builder{std::string(CameraOverhaulModule::moduleId),
                                       std::string(CameraOverhaulModule::name)};

    builder.description("Step head-bob + cinematic pitch/roll/sway (1.26.62)")
           .defaultEnabled(mod.enabled())
           .hideInHudEditor(true)
           .onToggle(onToggle)
           .onConfigChanged(onConfigChanged);

    using pl::modmenu::ConfigType;

    builder.config("enablePitch", "Pitch", ConfigType::Toggle, b(mod.m_enablePitch));
    builder.config("forwardPitchIntensity", "Forward Pitch Intensity", ConfigType::SliderFloat,
                   f(mod.m_forwardPitchIntensity), "0.0", "3.0", "enablePitch");
    builder.config("forwardPitchSmoothing", "Forward Pitch Smoothing", ConfigType::SliderFloat,
                   f(mod.m_forwardPitchSmoothing), "0.1", "5.0", "enablePitch");
    builder.config("verticalPitchIntensity", "Vertical Pitch Intensity", ConfigType::SliderFloat,
                   f(mod.m_verticalPitchIntensity), "0.0", "3.0", "enablePitch");
    builder.config("verticalPitchSmoothing", "Vertical Pitch Smoothing", ConfigType::SliderFloat,
                   f(mod.m_verticalPitchSmoothing), "0.1", "5.0", "enablePitch");

    builder.config("enableRoll", "Roll", ConfigType::Toggle, b(mod.m_enableRoll));
    builder.config("turningRollIntensity", "Turning Roll Intensity", ConfigType::SliderFloat,
                   f(mod.m_turningRollIntensity), "0.0", "3.0", "enableRoll");
    builder.config("turningRollAccumulation", "Turning Roll Accumulation", ConfigType::SliderFloat,
                   f(mod.m_turningRollAccumulation), "0.0", "3.0", "enableRoll");
    builder.config("turningRollSmoothing", "Turning Roll Smoothing", ConfigType::SliderFloat,
                   f(mod.m_turningRollSmoothing), "0.1", "5.0", "enableRoll");
    builder.config("strafingRollIntensity", "Strafing Roll Intensity", ConfigType::SliderFloat,
                   f(mod.m_strafingRollIntensity), "0.0", "3.0", "enableRoll");

    builder.config("enableSway", "Idle Sway", ConfigType::Toggle, b(mod.m_enableSway));
    builder.config("swayIntensity", "Sway Intensity", ConfigType::SliderFloat,
                   f(mod.m_swayIntensity), "0.0", "3.0", "enableSway");
    builder.config("swayFrequency", "Sway Frequency", ConfigType::SliderFloat,
                   f(mod.m_swayFrequency), "0.1", "5.0", "enableSway");
    builder.config("swayFadeInDelay", "Sway Fade In Delay", ConfigType::SliderFloat,
                   f(mod.m_swayFadeInDelay), "0.0", "15.0", "enableSway");
    builder.config("swayFadeInLength", "Sway Fade In Length", ConfigType::SliderFloat,
                   f(mod.m_swayFadeInLength), "0.1", "15.0", "enableSway");
    builder.config("swayFadeOutLength", "Sway Fade Out Length", ConfigType::SliderFloat,
                   f(mod.m_swayFadeOutLength), "0.1", "15.0", "enableSway");

    builder.config("transitionSmoothing", "Overall Smoothness", ConfigType::SliderFloat,
                   f(mod.m_transitionSmoothing), "0.2", "4.0");

    builder.config("enableHeadBob", "Head Bob", ConfigType::Toggle, b(mod.m_enableHeadBob));
    // pl::modmenu::ModuleBuilder::config takes at most 7 arguments in every
    // released preloader, so the inline option list cannot be expressed here;
    // the preset choice is driven by the V2 schema (or config.json headBobMode).
    // Only preloaders without the V2 schema need the flat preset entry: its radio
    // sends free text, which is what used to poison config.json and reset
    // everything on the next launch.
    if (!hasSchemaApi) {
        builder.config("headBobMode", "Head Bob Mode", ConfigType::Radio,
                       modeName(mod.m_headBobModeKind));
    }
    builder.config("headBobStrength", "Head Bob Strength", ConfigType::SliderFloat,
                   f(mod.m_headBobStrength), "0.0", "3.0", "enableHeadBob");
    builder.config("headBobStepRate", "Step Rate (Custom preset)", ConfigType::SliderFloat,
                   f(mod.m_headBobStepRate), "0.25", "2.5", "enableHeadBob");
    builder.config("headBobSmoothHz", "Spring Frequency (Custom preset)", ConfigType::SliderFloat,
                   f(mod.m_headBobSmoothHz), "0.5", "6.0", "enableHeadBob");
    builder.config("headBobDamping", "Spring Damping (Custom preset)", ConfigType::SliderFloat,
                   f(mod.m_headBobDamping), "0.2", "3.0", "enableHeadBob");

    builder.config("headBobStrengthDefault", "Default Preset Strength", ConfigType::SliderFloat,
                   f(mod.m_headBobStrengthDefault), "0.0", "3.0", "enableHeadBob");
    builder.config("headBobStrengthBodycam", "Bodycam Preset Strength", ConfigType::SliderFloat,
                   f(mod.m_headBobStrengthBodycam), "0.0", "3.0", "enableHeadBob");
    builder.config("headBobStrengthComfort", "Comfort Preset Strength", ConfigType::SliderFloat,
                   f(mod.m_headBobStrengthComfort), "0.0", "3.0", "enableHeadBob");
    builder.config("headBobStrengthCustom", "Custom Preset Strength", ConfigType::SliderFloat,
                   f(mod.m_headBobStrengthCustom), "0.0", "3.0", "enableHeadBob");

    builder.config("presetScalesCameraMotion", "Preset Scales Camera Motion", ConfigType::Toggle,
                   b(mod.m_presetScalesCameraMotion));
    builder.config("cameraMotionDefault", "Default Preset Camera Motion", ConfigType::SliderFloat,
                   f(mod.m_cameraMotionDefault), "0.0", "3.0");
    builder.config("cameraMotionBodycam", "Bodycam Preset Camera Motion", ConfigType::SliderFloat,
                   f(mod.m_cameraMotionBodycam), "0.0", "3.0");
    builder.config("cameraMotionComfort", "Comfort Preset Camera Motion", ConfigType::SliderFloat,
                   f(mod.m_cameraMotionComfort), "0.0", "3.0");
    builder.config("cameraMotionCustom", "Custom Preset Camera Motion", ConfigType::SliderFloat,
                   f(mod.m_cameraMotionCustom), "0.0", "3.0");

    if (!builder.registerModule()) return;

    // Modern card/slider layout when the preloader speaks V2, flat list otherwise.
    (void)applySchemaV2();
}
