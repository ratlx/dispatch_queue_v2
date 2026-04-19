#pragma once

#include <atomic>
#include <cstdint>
#include <memory>

namespace Rat {
class DispatchGroup : public std::enable_shared_from_this<DispatchGroup> {
 public:
  DispatchGroup() noexcept = default;

  DispatchGroup(const DispatchGroup&) = delete;
  DispatchGroup& operator=(const DispatchGroup&) = delete;

  DispatchGroup(DispatchGroup&&) = delete;
  DispatchGroup& operator=(DispatchGroup&&) = delete;

  void Enter() noexcept {
    auto state = state_.load(std::memory_order_relaxed);
    while (true) {
      auto new_state = state + 1;
      if (GetNumTasks(state) == 0) {
        new_state += 1ULL << kRoundBitShift;
      }

      if (state_.compare_exchange_weak(state, new_state,
                                       std::memory_order_relaxed)) {
        return;
      }
    }
  }

  void Leave() noexcept {
    if (GetNumTasks(state_.fetch_sub(1, std::memory_order_release)) == 1) {
      state_.notify_all();
    }
  }

  void Wait() noexcept {
    auto state = state_.load(std::memory_order_acquire);
    const auto round = GetRound(state);

    while (GetNumTasks(state) > 0 && round == GetRound(state)) {
      state_.wait(state, std::memory_order_acquire);
      state = state_.load(std::memory_order_acquire);
    }
  }

 private:
  using State = uint64_t;
  static constexpr uint8_t kRoundBitShift = sizeof(State) * 4;
  static constexpr State kRoundMask = static_cast<State>(UINT32_MAX)
                                      << kRoundBitShift;
  static constexpr State kNumTasksMask = ~kRoundMask;

  static State GetNumTasks(State state) noexcept {
    return state & kNumTasksMask;
  }
  static State GetRound(State state) noexcept {
    return (state & kRoundMask) >> kRoundBitShift;
  }

  std::atomic<State> state_{0};
};

inline std::shared_ptr<DispatchGroup> CreateGroup() noexcept {
  return std::make_shared<DispatchGroup>();
}
}  // namespace Rat