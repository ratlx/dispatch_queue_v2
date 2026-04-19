#pragma once

#include <memory>
#include <semaphore>
#include <stdexcept>

#include "dq_manager.h"
#include "dq_task.h"
#include "mpsc_queue.h"
#include "utils.h"

namespace Rat {
namespace detail {

template <typename Executor>
class DQBase : public std::enable_shared_from_this<DQBase<Executor>> {
 public:
  DQBase(bool is_serial) : is_serial_(is_serial) {
    auto res = detail::DQManager::Instance().Register();
    if (!res.success) {
      throw std::runtime_error(
          "Number of created dispatch queues exceeds maximum (" +
          std::to_string(kMaxNumQueues) + ")");
    }

    id_ = res.value;
    cb_ = &DQManager::Instance().GetControlBlock(id_);
  }

  ~DQBase() { DQManager::Instance().Deregister(id_); }

  void Sync(Func func, bool is_barrier = false) {
    is_barrier |= is_serial_;
    if (!is_barrier) {
      FuncExecute(func);
      return;
    }

    auto sem = std::binary_semaphore(0);
    auto sync_task = DQTask([&sem]() { sem.release(); }, id_, true, true);
    Submit(std::move(sync_task));
    FuncExecute([&func, &sem] {
      sem.acquire();
      func();
    });
    Notify();
  }

  void Async(Func func, bool is_barrier = false) {
    is_barrier |= is_serial_;
    auto async_task = DQTask(std::move(func), id_, is_barrier, false);
    Submit(std::move(async_task));
  }

  [[nodiscard]] bool IsSerial() const noexcept { return is_serial_; }

 private:
  friend class DQExecutor;

  DQControlBlock& GetControlBlock() const noexcept {
    return DQManager::Instance().GetControlBlock(id_);
  }

  void Submit(DQTask&& task) noexcept {
    task_queue_.enqueue(std::move(task));
    GetControlBlock().drainer.Collect();

    if (GetControlBlock().drainer.TrySwitchOn().success) {
      Notify();
    }
  }

  void Notify() noexcept {
    while (true) {
      if (auto task_ptr = task_queue_.try_peek()) {
        if (task_ptr->IsSync()) {
          auto task = task_queue_.try_dequeue();
          GetControlBlock().drainer.Discharge();
          task->Execute();
        } else {
          if (GetControlBlock().weak_ptr.expired()) {
            GetControlBlock().weak_ptr = this->weak_from_this();
          }
          Executor::Instance().CallDrainer(id_);
        }
        break;
      } else if (GetControlBlock().drainer.TrySwitchOff().success) {
        break;
      }
    }
  }

  MPSCQueue<DQTask> task_queue_;
  // only for debugging
  const DQControlBlock* cb_;

  ID id_;
  bool is_serial_;
};

}  // namespace detail
}  // namespace Rat