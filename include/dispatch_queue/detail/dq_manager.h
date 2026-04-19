#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <queue>
#include <mutex>
#include <unordered_set>

#include "utils.h"

namespace Rat {
namespace detail {

template <typename Value>
struct Result {
  Value value;
  bool success;
};

class DrainerState {
 public:
  using State = int32_t;
  using Result = Result<State>;

  DrainerState() noexcept = default;

  ~DrainerState() noexcept = default;

  State Collect() noexcept {
    return state_.fetch_add(2, std::memory_order_release);
  }

  State Discharge() noexcept {
    return state_.fetch_sub(2, std::memory_order_release);
  }

  Result TrySwitchOn() noexcept {
    auto state = state_.load(std::memory_order_acquire);
    while (!IsSwitchOn(state) && GetNumTasks(state) != 0) {
      if (state_.compare_exchange_weak(state, state | kSwitchBit,
                                       std::memory_order_relaxed,
                                       std::memory_order_acquire)) {
        return {state, true};
      }
    }
    return {state, false};
  }

  Result TrySwitchOff() noexcept {
    auto state = state_.load(std::memory_order_acquire);
    while (GetNumTasks(state) == 0) {
      if (state_.compare_exchange_weak(state, state & ~kSwitchBit,
                                       std::memory_order_release,
                                       std::memory_order_acquire)) {
        return {state, true};
      }
    }
    return {state, false};
  }

  State SwitchOff() noexcept {
    return state_.fetch_and(~kSwitchBit, std::memory_order_acq_rel);
  }

  bool IsSwitchOn() const noexcept {
    auto state = state_.load(std::memory_order_acquire);
    return IsSwitchOn(state);
  }

  static bool IsSwitchOn(State state) noexcept { return state & kSwitchBit; }
  static State GetNumTasks(State state) noexcept {
    return (state & kNumTasksMask) >> 1;
  }

 private:
  static constexpr State kSwitchBit = 1;
  static constexpr State kNumTasksMask = ~kSwitchBit;

  std::atomic<State> state_{0};
};

class ExecutorState {
 public:
  using State = NumExecutorsType;
  using Result = Result<State>;

  ExecutorState() noexcept = default;

  ~ExecutorState() noexcept = default;

  State DrainerEnter() noexcept {
    return state_.fetch_or(kDrainerMask, std::memory_order_acquire);
  }

  State DrainerLeave() noexcept {
    return state_.fetch_and(~kDrainerMask, std::memory_order_acq_rel);
  }

  Result TryDrainerLeave() noexcept {
    auto state = state_.load(std::memory_order_acquire);
    while (GetNumExecutors(state) >= kNumExecutors ||
           (IsBarrierExist(state) && GetNumExecutors(state) > 0)) {
      State new_state = state & ~kDrainerMask;
      if (state_.compare_exchange_weak(state, new_state,
                                       std::memory_order_release,
                                       std::memory_order_acquire)) {
        return {state, true};
      }
    }
    return {state, false};
  }

  State SetBarrier() noexcept {
    auto state = state_.fetch_or(kBarrierMask, std::memory_order_acquire);
    return state;
  }

  State ClearBarrier() noexcept {
    return state_.fetch_and(~kBarrierMask, std::memory_order_release);
  }

  State ExecutorEnter() noexcept {
    return state_.fetch_add(1, std::memory_order_acquire);
  }

  Result TryBecomeDrainer() noexcept {
    auto state = state_.load(std::memory_order_acquire);
    while (true) {
      bool satisfy = !IsDrainerExist(state) &&
                     !(IsBarrierExist(state) && GetNumExecutors(state) != 1);
      State new_state = state - 1;
      if (satisfy) {
        new_state |= kDrainerMask;
      }
      if (state_.compare_exchange_weak(state, new_state,
                                       std::memory_order_relaxed,
                                       std::memory_order_acquire)) {
        return {state, satisfy};
      }
    }
  }

  NumExecutorsType GetNumExecutors() const noexcept {
    auto state = state_.load(std::memory_order_acquire);
    return GetNumExecutors(state);
  }

  static bool IsDrainerExist(State state) noexcept {
    return state & kDrainerMask;
  }
  static bool IsBarrierExist(State state) noexcept {
    return state & kBarrierMask;
  }
  static NumExecutorsType GetNumExecutors(State state) noexcept {
    return state & kNumExecutorsMask;
  }

 private:
  static constexpr uint8_t kDrainerShift = sizeof(State) * 8 - 1;
  static constexpr uint8_t kBarrierShift = kDrainerShift - 1;
  static constexpr State kDrainerMask = 1 << kDrainerShift;
  static constexpr State kBarrierMask = 1 << kBarrierShift;
  static constexpr State kNumExecutorsMask =
      static_cast<State>(~kDrainerMask & ~kBarrierMask);

  std::atomic<State> state_{0};
};

struct DQControlBlock {
  alignas(kCacheLine) DrainerState drainer;
  alignas(kCacheLine) ExecutorState executor;
  std::weak_ptr<DispatchQueue> weak_ptr;
};

// Thread-safe registry for dispatch queues.
// Uses a fixed-size array with an atomic index allocator.
class DQManager {
  std::array<DQControlBlock, kMaxNumQueues> cb_array_{};
  std::mutex mtx_{};
  std::unordered_set<ID> id_set_{};
  std::queue<QIndex> free_qid_list_{};
  std::atomic<UID> next_{0};

  DQManager() noexcept = default;
  ~DQManager() noexcept = default;

 public:
  using Result = Result<ID>;

  DQManager(const DQManager&) = delete;
  DQManager& operator=(const DQManager&) = delete;

  static DQManager& Instance() noexcept {
    static DQManager inst;
    return inst;
  }

  Result Register() noexcept {
    UID uid = next_.fetch_add(1, std::memory_order_relaxed);
    QIndex qidx = uid;
    std::lock_guard lock{mtx_};

    if (uid >= kMaxNumQueues) {
      if (free_qid_list_.empty()) {
        return {{uid, qidx}, false};
      } else {
        qidx = free_qid_list_.front();
        free_qid_list_.pop();
      }
    }

    id_set_.emplace(uid, qidx);
    return {{uid, qidx}, true};
  }

  bool Deregister(const ID& id) noexcept {
    std::lock_guard lock{mtx_};

    auto it = id_set_.find(id);
    if (it != id_set_.end()) {
      // drainer is still on, can't deregister now
      if (cb_array_[id.qidx].drainer.IsSwitchOn()) {
        return false;
      }

      cb_array_[id.qidx].weak_ptr.reset();
      free_qid_list_.push(id.qidx);
      id_set_.erase(it);
      return true;
    }
    return false;
  }

  // Look up queue control block by id.
  DQControlBlock& GetControlBlock(const ID& id) noexcept {
    return cb_array_[id.qidx];
  }
};

}  // namespace detail
}  // namespace Rat