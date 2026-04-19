#pragma once

#include <atomic>
#include <bit>
#include <cstddef>
#include <new>
#include <optional>
#include <type_traits>
#include <utility>

#include "utils.h"

// Bounded MPMC (Multiple Producer, Multiple Consumer) queue.
// Based on Dmitry Vyukov's bounded MPMC queue using a ring buffer.
//
// - Multiple threads can call enqueue() / emplace() concurrently.
// - Multiple threads can call dequeue() concurrently.
// - Capacity is rounded up to the nearest power of two.
// - enqueue()/dequeue() spin-wait if the queue is full/empty.
//

namespace Rat {
namespace detail {

template <typename T>
  requires(std::is_nothrow_destructible_v<T> &&
           std::is_nothrow_move_constructible_v<T>)
class MPMCQueue {
  struct alignas(kCacheLine) Cell {
    std::atomic<std::size_t> sequence;
    alignas(T) std::byte data[sizeof(T)];

    T* data_ptr() noexcept { return std::launder(reinterpret_cast<T*>(data)); }
  };

  const std::size_t capacity_;
  const std::size_t mask_;
  Cell* buffer_;

  // Producer and consumer cursors on separate cache lines to avoid false
  // sharing.
  alignas(kCacheLine) std::atomic<std::size_t> head_{0};
  alignas(kCacheLine) std::atomic<std::size_t> tail_{0};

  template <typename... Args>
    requires std::is_nothrow_constructible_v<T, Args...>
  void emplace_at(std::size_t pos, Args&&... args) noexcept {
    Cell& cell = buffer_[pos & mask_];
    while (cell.sequence.load(std::memory_order_acquire) != pos);
    new (cell.data) T(std::forward<Args>(args)...);
    cell.sequence.store(pos + 1, std::memory_order_release);
  }

  template <typename... Args>
  void emplace_impl(Args&&... args) noexcept {
    std::size_t pos = head_.fetch_add(1, std::memory_order_acq_rel);
    emplace_at(pos, std::forward<Args>(args)...);
  }

  T dequeue_at(std::size_t pos) noexcept {
    Cell& cell = buffer_[pos & mask_];
    while (cell.sequence.load(std::memory_order_acquire) != pos + 1);
    T val = std::move(*cell.data_ptr());
    cell.data_ptr()->~T();
    cell.sequence.store(pos + capacity_, std::memory_order_release);
    return val;
  }

 public:
  explicit MPMCQueue(std::size_t capacity) noexcept
      : capacity_(std::bit_ceil(capacity)),
        mask_(capacity_ - 1),
        buffer_(new Cell[capacity_]) {
    for (std::size_t i = 0; i < capacity_; ++i)
      buffer_[i].sequence.store(i, std::memory_order_relaxed);
  }

  ~MPMCQueue() noexcept {
    // Precondition: no threads are accessing the queue.
    for (std::size_t i = tail_.load(std::memory_order_relaxed);
         i < head_.load(std::memory_order_relaxed); ++i)
      buffer_[i & mask_].data_ptr()->~T();
    delete[] buffer_;
  }

  MPMCQueue(const MPMCQueue&) = delete;
  MPMCQueue& operator=(const MPMCQueue&) = delete;
  MPMCQueue(MPMCQueue&&) = delete;
  MPMCQueue& operator=(MPMCQueue&&) = delete;

  // --- Producer side (thread-safe, lock-free) ---

  void enqueue(const T& val) noexcept { emplace_impl(val); }
  void enqueue(T&& val) noexcept { emplace_impl(std::move(val)); }

  template <typename... Args>
  void emplace(Args&&... args) noexcept {
    emplace_impl(std::forward<Args>(args)...);
  }

  [[nodiscard]] bool try_enqueue(const T& val) noexcept {
    return try_emplace(val);
  }
  [[nodiscard]] bool try_enqueue(T&& val) noexcept {
    return try_emplace(std::move(val));
  }

  template <typename... Args>
  [[nodiscard]] bool try_emplace(Args&&... args) noexcept {
    std::size_t head = head_.load(std::memory_order_acquire);
    std::size_t tail = tail_.load(std::memory_order_acquire);

    while (head - tail < capacity_) {
      if (head_.compare_exchange_weak(head, head + 1, std::memory_order_release,
                                      std::memory_order_acquire)) {
        emplace_at(head, std::forward<Args>(args)...);
        return true;
      } else {
        tail = tail_.load(std::memory_order_acquire);
      }
    }
    return false;
  }

  // --- Consumer side (thread-safe, lock-free) ---

  [[nodiscard]] T dequeue() noexcept {
    std::size_t pos = tail_.fetch_add(1, std::memory_order_acq_rel);
    return dequeue_at(pos);
  }

  [[nodiscard]] std::optional<T> try_dequeue() noexcept {
    std::size_t tail = tail_.load(std::memory_order_acquire);
    std::size_t head = head_.load(std::memory_order_acquire);

    while (tail < head) {
      if (tail_.compare_exchange_weak(tail, tail + 1, std::memory_order_release,
                                      std::memory_order_acquire)) {
        return dequeue_at(tail);
      } else {
        head = head_.load(std::memory_order_acquire);
      }
    }
    return std::nullopt;
  }
};

}  // namespace detail
}  // namespace Rat