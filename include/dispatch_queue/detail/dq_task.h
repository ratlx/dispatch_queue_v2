#pragma once

#include <iostream>
#include <string_view>

#include "utils.h"

namespace Rat {
namespace detail {

inline void FuncExecute(const Func& func) noexcept {
  auto PrintExceptInfo = [](std::string_view info) noexcept {
    std::cerr << "Unhandled exception : " << info << std::endl;
  };

  try {
    func();
  } catch (const std::exception& ex) {
    PrintExceptInfo(ex.what());
  } catch (...) {
    PrintExceptInfo("unknown error");
  }
}

class DQTask {
 public:
  DQTask(Func func, ID id, bool is_barrier, bool is_sync) noexcept
      : func_(std::move(func)),
        id_(id),
        is_barrier_(is_barrier),
        is_sync_(is_sync) {}

  DQTask(DQTask&& other) noexcept
      : func_(std::move(other.func_)),
        id_(other.id_),
        is_barrier_(other.is_barrier_),
        is_sync_(other.is_sync_) {}

  DQTask& operator=(DQTask&&) = delete;

  void Execute() const noexcept { FuncExecute(func_); }

  [[nodiscard]] ID GetID() const noexcept { return id_; }

  [[nodiscard]] bool IsBarrier() const noexcept { return is_barrier_; }

  [[nodiscard]] bool IsSync() const noexcept { return is_sync_; }

 private:
  Func func_;
  ID id_;
  bool is_barrier_;
  bool is_sync_;
};

}  // namespace detail
}  // namespace Rat