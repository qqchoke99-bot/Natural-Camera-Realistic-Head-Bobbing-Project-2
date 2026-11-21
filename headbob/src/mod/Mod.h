#pragma once

#include <ll/api/mod/NativeMod.h>

#include <atomic>

#include "mod/Config.h"
#include "mod/HeadBob.h"
#include "util/Memory.h"

class RealisticHeadBobMod {
public:
    static RealisticHeadBobMod& instance();

    RealisticHeadBobMod();

    [[nodiscard]] ll::mod::NativeMod& getSelf() const { return mSelf; }

    bool load();
    bool enable();
    bool disable();
    bool unload();

private:
    bool hookCameraBlend();
    bool hookPlayerTick();
    void unhookAll();

    // Called from the game thread (once per actor tick, 20 Hz).
    void onActorTick(void* actor);

    // Called from the render thread, after the original camera blend tick.
    void applyToCamera(void* component);

    static void cameraBlendDetour(void* component, void* blendState, float factor);
    static void tickDetour(void* actor);

    ll::mod::NativeMod& mSelf;
    headbob::State      mState{};

    void* mCameraTarget = nullptr;
    void* mTickTarget   = nullptr;
    void (*mOrigCameraBlend)(void*, void*, float) = nullptr;
    void (*mOrigTick)(void*) = nullptr;

    // Published by the game thread, consumed by the render thread.
    std::atomic<float> mSpeed{0.f};
    std::atomic<float> mVerticalSpeed{0.f};
    std::atomic<bool>  mOnGround{true};
    std::atomic<bool>  mSprinting{false};
    std::atomic<bool>  mHasSample{false};

    headbob::memory::Vec3 mLastPos{};
    bool   mHasLastPos    = false;
    bool   mLoggedSample  = false;
    double mLastTickTime  = 0.0;
    double mLastFrameTime = 0.0;
    float  mGameTime      = 0.f;
    float  mSmoothSpeed   = 0.f;
    float  mSmoothVy      = 0.f;
};
