#pragma once

#include <pl/Mod.hpp>

#include <atomic>
#include <cstdint>

#include "mod/Config.h"
#include "mod/HeadBob.h"
#include "util/Memory.h"

class RealisticHeadBobMod {
public:
    static RealisticHeadBobMod& instance();

    bool load(pl::mod::ModContext& context);
    bool enable(pl::mod::ModContext& context);
    bool disable(pl::mod::ModContext& context);
    bool unload(pl::mod::ModContext& context);

private:
    pl::log::Logger& logger() const { return mContext->logger(); }

    bool hookCameraBlend();
    bool hookPlayerTick();
    void unhookAll();

    // Called from the game thread (once per actor tick, 20 Hz).
    void onActorTick(void* actor);

    // Called from the render thread, after the original camera blend tick.
    void applyToCamera(void* component);

    static void cameraBlendDetour(void* component, void* blendState, float factor);
    static void tickDetour(void* actor);

    pl::mod::ModContext* mContext = nullptr;
    headbob::State       mState{};

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
    std::atomic<float> mPosSpeed{0.f};      // speed derived from position deltas
    std::atomic<float> mDeltaSpeed{0.f};    // speed derived from the engine's own tick delta
    std::atomic<bool>  mDeltaMode{true};
    std::atomic<unsigned long long> mTicks{0};
    std::atomic<unsigned long long> mFrames{0};

    headbob::memory::Vec3 mLastPos{};
    bool   mHasLastPos    = false;
    int    mBeatCount     = 0;
    double mLastTickTime  = 0.0;
    double mLastFrameTime = 0.0;
    float  mGameTime      = 0.f;
    float  mSmoothSpeed   = 0.f;
    float  mSmoothVy      = 0.f;
    int    mDeltaDeadTicks = 0;
    int    mTickBeats      = 0;
    double mLastTickBeat   = 0.0;
    double mLastBeat      = 0.0;
};
