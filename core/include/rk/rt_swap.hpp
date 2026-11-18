// Raumklang core: hand over heap objects to the audio thread without locks and
// without ever freeing memory on the audio thread.
#pragma once

#include <atomic>
#include <memory>

namespace rk {

/// Single producer (control thread) / single consumer (audio thread) object handover.
///
/// * Control thread: publish(), collectGarbage(), hasPending().
/// * Audio thread:   acquire(), current(), hasPending().
///
/// The audio thread only swaps pointers. Old objects are parked in a "retired" slot
/// and deleted later by the control thread. While the retired slot is occupied the audio
/// thread keeps the current object, so nothing is ever leaked or freed in realtime.
template <typename T>
class RtSwap {
public:
    RtSwap() = default;
    RtSwap(const RtSwap&) = delete;
    RtSwap& operator=(const RtSwap&) = delete;

    ~RtSwap() {
        delete pending_.exchange(nullptr);
        delete retired_.exchange(nullptr);
        delete current_;
    }

    /// Control thread. Replaces any not-yet-acquired object.
    void publish(std::unique_ptr<T> next) {
        collectGarbage();
        T* old = pending_.exchange(next.release(), std::memory_order_acq_rel);
        delete old; // never seen by the audio thread
    }

    /// Control thread. Frees the object the audio thread has finished with.
    void collectGarbage() { delete retired_.exchange(nullptr, std::memory_order_acq_rel); }

    /// Either thread.
    bool hasPending() const noexcept { return pending_.load(std::memory_order_acquire) != nullptr; }

    /// Audio thread. Takes the pending object if possible. Returns true if it switched.
    bool acquire() noexcept {
        if (retired_.load(std::memory_order_acquire) != nullptr) return false;
        T* next = pending_.exchange(nullptr, std::memory_order_acq_rel);
        if (next == nullptr) return false;
        retired_.store(current_, std::memory_order_release);
        current_ = next;
        return true;
    }

    /// Audio thread (or any thread while audio is stopped).
    T* current() const noexcept { return current_; }

    /// Only while the audio thread is not running: installs an object directly.
    void resetUnsafe(std::unique_ptr<T> obj) {
        delete pending_.exchange(nullptr);
        delete retired_.exchange(nullptr);
        delete current_;
        current_ = obj.release();
    }

private:
    std::atomic<T*> pending_{nullptr};
    std::atomic<T*> retired_{nullptr};
    T* current_ = nullptr;
};

} // namespace rk
