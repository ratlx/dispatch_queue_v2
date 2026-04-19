#pragma once

#include <memory>
#include <thread>
#include <vector>

#include "dq_base.h"
#include "dq_manager.h"
#include "dq_task.h"
#include "mpmc_queue.h"
#include "utils.h"

namespace Rat {
namespace detail {

class DQExecutor {
 public:
  DQExecutor(const DQExecutor&) = delete;
  DQExecutor& operator=(const DQExecutor&) = delete;

  DQExecutor(DQExecutor&&) = delete;
  DQExecutor& operator=(DQExecutor&&) = delete;

  static DQExecutor& Instance() noexcept {
    static DQExecutor inst;
    return inst;
  }

  void CallDrainer(ID id) noexcept {
    id_queue_.enqueue(id);
    sem_.release();
  }

 private:
  DQExecutor() noexcept {
    executors_.reserve(kNumExecutors);

    for (int i = 0; i < kNumExecutors; ++i) {
      executors_.emplace_back(std::bind(&DQExecutor::ExecutorRun, this));
    }
  }

  ~DQExecutor() noexcept {
    is_stop_ = true;
    sem_.release(kNumExecutors);

    for (auto& executor : executors_) {
      executor.join();
    }
  }

  void ExecutorRun() noexcept {
    while (true) {
      sem_.acquire();
      if (is_stop_) {
        return;
      }

      if (auto id = id_queue_.try_dequeue()) {
        GetExecutorState(*id).DrainerEnter();
        DrainerRun(*id);
      } else {
        auto task = task_queue_.dequeue();
        task.Execute();

        if (GetExecutorState(task.GetID()).TryBecomeDrainer().success) {
          DrainerRun(task.GetID());
        }
      }
    }
  }

  void DrainerRun(ID id) noexcept {
    auto& drainer_state = DQManager::Instance().GetControlBlock(id).drainer;
    auto& executor_state = GetExecutorState(id);

    if (auto queue = GetQueue(id)) {
      while (true) {
        if (auto task_ptr = queue->task_queue_.try_peek()) {
          if (task_ptr->IsBarrier()) {
            auto state = executor_state.SetBarrier();
            if (ExecutorState::GetNumExecutors(state) == 0 ||
                !executor_state.TryDrainerLeave().success) {
              auto task = queue->task_queue_.try_dequeue();
              drainer_state.Discharge();

              if (task->IsSync()) {
                // notify the sync task and give the control to thread that runs
                // the task, reset the state first
                executor_state.ClearBarrier();
                executor_state.DrainerLeave();
                task->Execute();
                return;
              } else {
                task->Execute();
                executor_state.ClearBarrier();
              }
            } else {
              // TryDrainerLeave success
              return;
            }
          } else {
            auto task = queue->task_queue_.try_dequeue();
            drainer_state.Discharge();

            executor_state.ExecutorEnter();
            task_queue_.enqueue(std::move(*task));
            sem_.release();
          }
        } else {
          // queue is empty (no task)
          // the modification order should be executor_state -> drainer_state
          auto state = executor_state.DrainerLeave();
          if (ExecutorState::GetNumExecutors(state) == 0) {
            if (drainer_state.TrySwitchOff().success) {
              return;
            } else {
              // fail to switch off, means new tasks are in
              // set back the state
              executor_state.ExecutorEnter();
            }
            // try to be drainer again if executor exists
          } else if (executor_state.TryDrainerEnter().success) {
            continue;
          } else {
            return;
          }
        }

        // leave if the number of executo  rs reach kNumExecutors
        if (executor_state.TryDrainerLeave().success) {
          return;
        }
      }
    } else {
      // the queue has been destructed
      auto state = executor_state.DrainerLeave();
      // last executor clears the queue
      if (ExecutorState::GetNumExecutors(state) == 0) {
        drainer_state.SwitchOff();
        DQManager::Instance().Deregister(id);
      }
      return;
    }
  }

  static std::shared_ptr<DispatchQueue> GetQueue(ID id) noexcept {
    return DQManager::Instance().GetControlBlock(id).weak_ptr.lock();
  }

  static ExecutorState& GetExecutorState(ID id) noexcept {
    return DQManager::Instance().GetControlBlock(id).executor;
  }

  static DrainerState& GetDrainerState(ID id) noexcept {
    return DQManager::Instance().GetControlBlock(id).drainer;
  }

  MPMCQueue<DQTask> task_queue_{kNumExecutors * kMaxNumQueues};
  MPMCQueue<ID> id_queue_{kNumExecutors};
  std::vector<std::thread> executors_;
  std::counting_semaphore<> sem_{0};
  bool is_stop_{false};
};

}  // namespace detail
}  // namespace Rat