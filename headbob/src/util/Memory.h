#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>

namespace headbob::memory {

struct Vec2 {
    float x;  // pitch, degrees
    float y;  // yaw, degrees
};

struct Vec3 {
    float x;
    float y;
    float z;
};

template <class T>
inline T& field(void* object, std::size_t offset) {
    return *reinterpret_cast<T*>(reinterpret_cast<std::uintptr_t>(object) + offset);
}

template <class T>
inline const T& field(const void* object, std::size_t offset) {
    return *reinterpret_cast<const T*>(reinterpret_cast<std::uintptr_t>(object) + offset);
}

// Fault-free probe: is this buffer inside mapped pages? mincore() answers
// without touching the memory, so a pointer read from a struct offset that
// moved between game builds is rejected here instead of raising SIGSEGV.
inline bool readable(const void* address, std::size_t length) {
    if (!address || length == 0) return false;
    const auto value = reinterpret_cast<std::uintptr_t>(address);
    if (value < 0x1000) return false;
    const long pageSizeValue = sysconf(_SC_PAGESIZE);
    if (pageSizeValue <= 0) return false;
    const auto pageSize = static_cast<std::uintptr_t>(pageSizeValue);
    const auto firstPage = value & ~(pageSize - 1);
    const auto lastPage = (value + length - 1) & ~(pageSize - 1);
    unsigned char present = 0;
    if (mincore(reinterpret_cast<void*>(firstPage), pageSize, &present) != 0) return false;
    if (lastPage != firstPage && mincore(reinterpret_cast<void*>(lastPage), pageSize, &present) != 0) return false;
    return true;
}

}
