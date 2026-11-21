#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>
#include <utility>

namespace cameraoverhaul::sdk {

inline bool makeWritableExecutable(void* address, std::size_t size) {
    if (!address || size == 0) return false;
    const long pageSizeValue = sysconf(_SC_PAGESIZE);
    if (pageSizeValue <= 0) return false;
    const auto pageSize = static_cast<std::uintptr_t>(pageSizeValue);
    const auto start = reinterpret_cast<std::uintptr_t>(address);
    const auto pageStart = start & ~(pageSize - 1);
    const auto pageEnd = (start + size + pageSize - 1) & ~(pageSize - 1);
    const auto protectSize = static_cast<std::size_t>(pageEnd - pageStart);
    return mprotect(reinterpret_cast<void*>(pageStart), protectSize, PROT_READ | PROT_WRITE | PROT_EXEC) == 0;
}

inline bool patchMemory(void* address, const void* data, std::size_t size) {
    if (!address || !data || size == 0) return false;
    const long pageSizeValue = sysconf(_SC_PAGESIZE);
    if (pageSizeValue <= 0) return false;
    const auto pageSize = static_cast<std::uintptr_t>(pageSizeValue);
    const auto start = reinterpret_cast<std::uintptr_t>(address);
    const auto pageStart = start & ~(pageSize - 1);
    const auto pageEnd = (start + size + pageSize - 1) & ~(pageSize - 1);
    const auto protectSize = static_cast<std::size_t>(pageEnd - pageStart);
    if (!makeWritableExecutable(address, size)) return false;
    std::memcpy(address, data, size);
    __builtin___clear_cache(static_cast<char*>(address), static_cast<char*>(address) + size);
    mprotect(reinterpret_cast<void*>(pageStart), protectSize, PROT_READ | PROT_EXEC);
    return true;
}

// Fault-free probe: is this buffer inside mapped pages? mincore() answers
// without touching the memory, so a pointer read from a stale struct offset
// (a float bit pattern, a bool, ...) is rejected here instead of raising
// SIGSEGV inside the game.
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

template <class T>
T& field(void* object, std::size_t offset) {
    return *reinterpret_cast<T*>(reinterpret_cast<std::uintptr_t>(object) + offset);
}

template <class T>
const T& field(const void* object, std::size_t offset) {
    return *reinterpret_cast<const T*>(reinterpret_cast<std::uintptr_t>(object) + offset);
}

template <class Return, class... Args>
Return virtualCall(void* instance, std::size_t index, Args&&... args) {
    auto table = *reinterpret_cast<void***>(instance);
    auto function = reinterpret_cast<Return(*)(void*, Args...)>(table[index]);
    return function(instance, std::forward<Args>(args)...);
}

}
