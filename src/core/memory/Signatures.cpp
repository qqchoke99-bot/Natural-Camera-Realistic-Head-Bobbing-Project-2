#include <cameraoverhaul/memory/Signatures.hpp>

#include <cameraoverhaul/Log.hpp>
#include <cameraoverhaul/memory/GameProfiles.hpp>

#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <vector>

#include <pl/memory/Signature.hpp>

namespace cameraoverhaul::memory {
namespace {

std::array<std::uintptr_t, SignatureCount> addresses{};

constexpr std::array<std::string_view, SignatureCount> kSignatureNames{{
    "NormalTick",
    "CameraBlendSystemTick",
}};

std::string_view signatureName(SignatureId id) {
    const auto index = static_cast<std::size_t>(id);
    return index < kSignatureNames.size() ? kSignatureNames[index] : "unknown";
}

std::string hexAddress(std::uintptr_t address) {
    constexpr char kDigits[] = "0123456789ABCDEF";
    if (address == 0) return "0x0";
    std::string text;
    while (address != 0) {
        text.insert(text.begin(), kDigits[address & 0xF]);
        address >>= 4;
    }
    return "0x" + text;
}

}

// Resolves the signatures of the one profile this build targets. There is no
// cross-version fallback on purpose: running a package against a game build it
// was not built for is what the version pin exists to prevent.
bool resolveAll(std::string_view libraryName) {
    const std::string library(libraryName);
    const auto& profile = targetProfile();

    std::vector<std::string> patterns;
    for (const auto& signature : profile.signatures) {
        for (const auto& candidate : signature.candidates) {
            if (!candidate.empty()) patterns.emplace_back(candidate);
        }
    }
    std::sort(patterns.begin(), patterns.end());
    patterns.erase(std::unique(patterns.begin(), patterns.end()), patterns.end());

    addresses.fill(0);
    if (patterns.empty()) {
        logWarn("signatures: no patterns configured for game profile " + std::string(profile.version));
        return false;
    }

    const auto resolved = pl::memory::resolveSignatures(patterns, library.c_str());

    const auto lookup = [&resolved](std::string_view candidate) -> std::uintptr_t {
        if (candidate.empty()) return 0;
        const auto it = resolved.find(std::string(candidate));
        return it == resolved.end() ? 0 : it->second;
    };

    std::string missing;
    for (std::size_t slot = 0; slot < SignatureCount; ++slot) {
        const auto id = static_cast<SignatureId>(slot);
        for (const auto& candidate : profile.signatures[slot].candidates) {
            const std::uintptr_t address = lookup(candidate);
            if (address != 0) {
                addresses[slot] = address;
                logInfo("signatures: " + std::string(signatureName(id)) + " at " + hexAddress(address));
                break;
            }
        }
        if (addresses[slot] == 0) {
            if (!missing.empty()) missing += ", ";
            missing += signatureName(id);
        }
    }

    if (!missing.empty()) {
        logWarn("signatures: game profile " + std::string(profile.version) +
                " does not fit this " + library + " (missing " + missing +
                "); this package is built for a different Minecraft build");
        return false;
    }

    logInfo("signatures: matched game profile " + std::string(profile.version));
    return true;
}

std::uintptr_t resolve(SignatureId id) {
    const auto index = static_cast<std::size_t>(id);
    return index < addresses.size() ? addresses[index] : 0;
}

void clear() {
    addresses.fill(0);
}

}
