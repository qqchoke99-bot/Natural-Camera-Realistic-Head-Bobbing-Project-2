#pragma once

#include <pl/Mod.hpp>

#include "mod/Config.h"
#include "mod/HeadBob.h"

class RealisticHeadBobMod {
public:
    static RealisticHeadBobMod& instance();

    // Valid inside a lifecycle call: the preloader sets the current mod while
    // load/enable/disable/unload run.
    [[nodiscard]] ll::mod::NativeMod& getSelf() const { return *ll::mod::NativeMod::current(); }

    bool load();
    bool enable();
    bool disable();
    bool unload();

private:
    headbob::State mState{};

    using CameraBlendFn = void (*)(void* component, void* blendState, float factor);
    CameraBlendFn mOrigCameraBlend = nullptr;
    void*         mHookTarget      = nullptr;

    using NormalTickFn = void (*)(void* actor);
    NormalTickFn mOrigPlayerTick = nullptr;
    void*        mTickTarget     = nullptr;

    bool resolveAndHook();
    bool resolveAndHookPlayerTick();
    void unhookAll();

    // Same signature as CameraOverhaul CameraBlendSystemTick
    static void cameraBlendDetour(void* component, void* blendState, float factor);
    void applyToCamera(void* component, float factor);

    static void playerTickDetour(void* actor);
    void onPlayerTick(void* actor);

    // Player state published by the tick hook, read by the camera hook.
    float mSpeedXZ       = 0.f;
    float mVerticalSpeed = 0.f;
    bool  mOnGround      = true;

    float  mLastX = 0.f, mLastY = 0.f, mLastZ = 0.f;
    bool   mHasLastPos    = false;
    double mLastTickTime  = 0.0;
    double mLastFrameTime = 0.0;
    float  mSmoothSpeed   = 0.f;
    float  mSmoothVy      = 0.f;
};
