#include <atomic>
#include <format>
#include <iostream>
#include <semaphore>

#include "dispatch_queue/dispatch_queue.h"

int main() {
  auto q = Rat::CreateQueue(false);

  const int n = 100;
  const int m = 1000;
  const int sum1 = 79999;

  auto sem = std::binary_semaphore(0);
  std::atomic<int> cnt1 = 0;
  int cnt2 = 0;

  for (int i = 0; i < n; ++i) {
    q->Async([&] {
      for (int j = 0; j < m; ++j) {
        if (j % 5) {
          q->Async([&] {
            if (cnt1.fetch_add(1, std::memory_order_relaxed) == sum1) {
              sem.release();
            }
          });
        } else if (j % 10) {
          q->Async([&] { ++cnt2; }, true);
        }
      }
    });
    for (int k = 0; k < m / 10; ++k) {
      q->Sync([&] { ++cnt2; }, true);
    }
  }

  sem.acquire();
  std::cout << std::format("cnt1: {}, cnt2: {}\n",
                           cnt1.load(std::memory_order_relaxed), cnt2);
}