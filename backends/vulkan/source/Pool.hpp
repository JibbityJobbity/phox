#ifndef PHOX_VK_POOL_HPP
#define PHOX_VK_POOL_HPP

#include <cassert>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

namespace phox::vulkan {

// Slot pool whose handles carry a generation. Destroying a slot bumps its generation, so a stale
// handle never resolves again, even after the slot is reused.
template <class Handle, class T> class Pool {
  public:
    Handle create(T value) {
        uint32_t index;
        if (!m_free.empty()) {
            index = m_free.back();
            m_free.pop_back();
        } else {
            index = static_cast<uint32_t>(m_slots.size());
            m_slots.emplace_back();
        }
        Slot &slot = m_slots[index];
        slot.value = std::move(value);
        slot.alive = true;
        return Handle{index, slot.generation};
    }

    bool contains(Handle handle) const {
        return handle.valid() && handle.index < m_slots.size() && m_slots[handle.index].alive &&
               m_slots[handle.index].generation == handle.generation;
    }

    // Debug builds abort on a stale or invalid handle; release builds throw instead of invoking UB.
    T &get(Handle handle) {
        check(handle);
        return m_slots[handle.index].value;
    }
    const T &get(Handle handle) const {
        check(handle);
        return m_slots[handle.index].value;
    }

    // Moves the value out and retires the slot. Invalid handle: returns false. Stale handle: assert in
    // debug, false in release.
    bool take(Handle handle, T &out) {
        if (!handle.valid()) {
            return false;
        }
        if (!contains(handle)) {
            assert(false && "destroy() called with a stale handle");
            return false;
        }
        Slot &slot = m_slots[handle.index];
        out = std::move(slot.value);
        slot.value = T{};
        slot.alive = false;
        ++slot.generation;
        m_free.push_back(handle.index);
        return true;
    }

    template <class F> void forEachAlive(F &&fn) {
        for (Slot &slot : m_slots) {
            if (slot.alive) {
                fn(slot.value);
            }
        }
    }

  private:
    struct Slot {
        T value{};
        uint32_t generation = 0;
        bool alive = false;
    };

    void check(Handle handle) const {
        if (!contains(handle)) {
            assert(false && "stale or invalid handle");
            throw std::logic_error("stale or invalid handle");
        }
    }

    std::vector<Slot> m_slots;
    std::vector<uint32_t> m_free;
};

} // namespace phox::vulkan

#endif // PHOX_VK_POOL_HPP
