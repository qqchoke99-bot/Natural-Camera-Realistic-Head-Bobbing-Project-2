#include <cameraoverhaul/Log.hpp>

#include <cstdio>

#if defined(__ANDROID__)
#include <android/log.h>
#endif

namespace cameraoverhaul {
namespace {

constexpr const char* kTag = "CameraOverhaul";

void emit(int priority, std::string_view message) {
    if (message.empty()) return;
#if defined(__ANDROID__)
    __android_log_print(priority, kTag, "%.*s", static_cast<int>(message.size()), message.data());
#else
    (void)priority;
    std::fprintf(stderr, "%s: %.*s\n", kTag, static_cast<int>(message.size()), message.data());
#endif
}

}

void logInfo(std::string_view message) {
#if defined(__ANDROID__)
    emit(ANDROID_LOG_INFO, message);
#else
    emit(4, message);
#endif
}

void logWarn(std::string_view message) {
#if defined(__ANDROID__)
    emit(ANDROID_LOG_WARN, message);
#else
    emit(5, message);
#endif
}

}
