#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <new>
#include <thread>

namespace Rat {
namespace detail {

#if defined(__cpp_lib_hardware_interference_size)
constexpr std::size_t kCacheLine = std::hardware_destructive_interference_size;
#else
constexpr std::size_t kCacheLine = 64;
#endif

// The maximum number of dispatch queues can exist at the same time
constexpr std::size_t kMaxNumQueues = 64;

using NumExecutorsType = uint32_t;
const NumExecutorsType kNumExecutors = std::thread::hardware_concurrency();

using UID = uint32_t;
using QIndex = uint32_t;

struct ID {
  UID uid;
  QIndex qidx;

  bool operator==(const ID& other) const noexcept { return uid == other.uid; }
};

}  // namespace detail
}  // namespace Rat

template <>
struct std::hash<Rat::detail::ID> {
  size_t operator()(const Rat::detail::ID& id) const noexcept {
    return std::hash<uint32_t>{}(id.uid);
  }
};

namespace Rat {
namespace detail {

template <typename Executor>
class DQBase;
class DQExecutor;

}  // namespace detail

using Func = std::function<void()>;
using DispatchQueue = detail::DQBase<detail::DQExecutor>;

}  // namespace Rat
