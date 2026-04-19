#pragma once

#include <atomic>
#include <cstddef>
#include <new>
#include <optional>
#include <type_traits>
#include <utility>

#include "utils.h"

// Classic Michael-Scott MPSC (Multiple Producer, Single Consumer) queue.
// Based on a lock-free singly-linked list with sentinel node.
//
// - Multiple threads can call enqueue() concurrently.
// - Only one thread may call try_dequeue() / try_peek().
// - try_peek() returns a pointer to the front element without removing it.
//   The returned pointer is valid until the next call to try_dequeue().
//

namespace Rat {
namespace detail {

template <typename T>
  requires(std::is_nothrow_destructible_v<T> &&
           std::is_nothrow_move_constructible_v<T>)
class MPSCQueue {
  struct Node {
    alignas(T) std::byte data[sizeof(T)];
    std::atomic<Node*> next{nullptr};

    T* data_ptr() noexcept { return std::launder(reinterpret_cast<T*>(data)); }

    Node() noexcept = default;  // sentinel

    template <typename... Args>
      requires std::is_nothrow_constructible_v<T, Args...>
    explicit Node(std::in_place_t, Args&&... args) noexcept {
      new (data) T(std::forward<Args>(args)...);
    }
  };

  alignas(kCacheLine) std::atomic<Node*> head_;  // producers push here
  alignas(kCacheLine) Node* tail_;  // consumer reads here (single-threaded)

  // Helper: allocate a node and atomically push it to head_.
  template <typename... Args>
  void emplace_impl(Args&&... args) noexcept {
    Node* node = new Node(std::in_place, std::forward<Args>(args)...);
    Node* prev = head_.exchange(node, std::memory_order_acq_rel);
    prev->next.store(node, std::memory_order_release);
  }

 public:
  MPSCQueue() noexcept
      : head_(new Node()), tail_(head_.load(std::memory_order_relaxed)) {}

  ~MPSCQueue() noexcept {
    while (tail_->next.load(std::memory_order_acquire)) pop();
    delete tail_;  // delete the sentinel
  }

  MPSCQueue(const MPSCQueue&) = delete;
  MPSCQueue& operator=(const MPSCQueue&) = delete;
  MPSCQueue(MPSCQueue&&) = delete;
  MPSCQueue& operator=(MPSCQueue&&) = delete;

  // --- Producer side (thread-safe, lock-free) ---

  void enqueue(const T& val) noexcept { emplace_impl(val); }
  void enqueue(T&& val) noexcept { emplace_impl(std::move(val)); }

  template <typename... Args>
  void emplace(Args&&... args) noexcept {
    emplace_impl(std::forward<Args>(args)...);
  }

  // --- Consumer side (single-threaded only) ---

  // Non-blocking dequeue.  Returns std::nullopt if the queue is empty.
  [[nodiscard]] std::optional<T> try_dequeue() noexcept {
    Node* next = tail_->next.load(std::memory_order_acquire);
    if (!next) return std::nullopt;

    T val = std::move(*next->data_ptr());
    next->data_ptr()->~T();
    delete tail_;
    tail_ = next;
    return val;
  }

  // Non-blocking pop.  Discards the front element.  No-op if empty.
  void pop() noexcept {
    Node* next = tail_->next.load(std::memory_order_acquire);
    if (!next) return;

    next->data_ptr()->~T();
    delete tail_;
    tail_ = next;
  }

  // Non-blocking peek.  Returns pointer to the front element, or nullptr.
  // Valid until the next call to try_dequeue() or pop()
  [[nodiscard]] T* try_peek() noexcept {
    Node* next = tail_->next.load(std::memory_order_acquire);
    return next ? next->data_ptr() : nullptr;
  }

  // Check if queue is empty (consumer side only).
  [[nodiscard]] bool empty() const noexcept {
    return tail_->next.load(std::memory_order_acquire) == nullptr;
  }
};

}  // namespace detail
}  // namespace Rat