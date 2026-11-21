#pragma once

#include <cameraoverhaul/events/Event.hpp>
#include <cameraoverhaul/Export.hpp>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <utility>
#include <vector>

namespace cameraoverhaul::events {

using Subscription = std::uint64_t;

/**
 * Publish-side hot path: pumped once per player tick (20 Hz) and once per frame
 * (60-120 Hz, from the eglSwapBuffers detour).
 *
 * Publishing is now lock-free and allocation-free: each event type has an atomic
 * pointer to an immutable listener list, so a publish is one acquire load plus
 * N std::function calls. Lists are rebuilt copy-on-write on subscribe/unsubscribe
 * and the replaced ones are kept alive, so a publisher already walking an old
 * list never sees freed memory. Subscribing happens at init, so the retained
 * lists are bounded by config reloads, not by frames.
 */
class EventBus {
public:
    using RawCallback = std::function<void(void*)>;

    EventBus() noexcept {
        for (auto& slot : mSlots) slot.store(nullptr, std::memory_order_relaxed);
    }

    EventBus(const EventBus&) = delete;
    EventBus& operator=(const EventBus&) = delete;

    Subscription subscribeRaw(EventType type, RawCallback callback, EventPriority priority = EventPriority::Normal) {
        if (!callback) return 0;
        const std::size_t index = indexOf(type);
        if (index == kNoSlot) return 0;

        std::unique_lock lock(mMutex);
        const auto id = mNextId++;

        auto list = std::make_shared<std::vector<Entry>>();
        if (const auto* current = mSlots[index].load(std::memory_order_relaxed)) {
            *list = *current;
        }
        list->push_back({id, priority, std::move(callback)});
        std::stable_sort(list->begin(), list->end(), [](const Entry& left, const Entry& right) {
            return static_cast<std::int32_t>(left.priority) > static_cast<std::int32_t>(right.priority);
        });

        mKeepAlive.push_back(list);
        mSlots[index].store(list.get(), std::memory_order_release);
        return id;
    }

    template <class Event, class Callback>
    Subscription subscribe(Callback&& callback, EventPriority priority = EventPriority::Normal) {
        return subscribeRaw(Event::type, [handler = std::forward<Callback>(callback)](void* payload) mutable {
            handler(*static_cast<Event*>(payload));
        }, priority);
    }

    void unsubscribe(Subscription id) {
        if (id == 0) return;
        std::unique_lock lock(mMutex);
        for (std::size_t index = 0; index < kSlotCount; ++index) {
            const auto* current = mSlots[index].load(std::memory_order_relaxed);
            if (!current) continue;
            const bool present = std::any_of(current->begin(), current->end(),
                                             [id](const Entry& entry) { return entry.id == id; });
            if (!present) continue;

            auto next = std::make_shared<std::vector<Entry>>();
            next->reserve(current->size() > 0 ? current->size() - 1 : 0);
            for (const auto& entry : *current) {
                if (entry.id != id) next->push_back(entry);
            }
            if (next->empty()) {
                mSlots[index].store(nullptr, std::memory_order_release);
                mKeepAlive.push_back(std::move(next));
            } else {
                mKeepAlive.push_back(next);
                mSlots[index].store(next.get(), std::memory_order_release);
            }
        }
    }

    void publishRaw(EventType type, void* payload) {
        const std::size_t index = indexOf(type);
        if (index == kNoSlot) return;
        const auto* listeners = mSlots[index].load(std::memory_order_acquire);
        if (!listeners) return;
        for (const auto& entry : *listeners) entry.callback(payload);
    }

    template <class Event>
    void publish(Event& event) {
        publishRaw(Event::type, &event);
    }

    void clear() {
        std::unique_lock lock(mMutex);
        for (auto& slot : mSlots) slot.store(nullptr, std::memory_order_release);
    }

private:
    static constexpr std::size_t kSlotCount = 8;
    static constexpr std::size_t kNoSlot = static_cast<std::size_t>(-1);

    static constexpr std::size_t indexOf(EventType type) noexcept {
        const auto index = static_cast<std::size_t>(type);
        return index < kSlotCount ? index : kNoSlot;
    }

    struct Entry {
        Subscription id;
        EventPriority priority;
        RawCallback callback;
    };

    std::shared_mutex mMutex;
    std::array<std::atomic<const std::vector<Entry>*>, kSlotCount> mSlots{};
    std::vector<std::shared_ptr<const std::vector<Entry>>> mKeepAlive;
    std::uint64_t mNextId = 1;
};

CAMERAOVERHAUL_API EventBus& bus();

}
