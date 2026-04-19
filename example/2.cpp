#include <atomic>
#include <iostream>

#include "dispatch_queue/dispatch_queue.h"

int main() {
  auto q = Rat::CreateQueue(false);
  auto g = Rat::CreateGroup();

  const int n = 100;
  const int m = 1000;
  const int sum1 = 79999;

  std::atomic<int> cnt1 = 0;
  int cnt2 = 0;

  for (int i = 0; i < n; ++i) {
    q->Async(
        [&] {
          for (int j = 0; j < m; ++j) {
            if (j % 5) {
              q->Async([&] { cnt1.fetch_add(1, std::memory_order_relaxed); },
                       g);
            } else if (j % 10) {
              q->Async([&] { ++cnt2; }, g, true);
            } else {
              q->Sync([&] { ++cnt2; });
            }
          }
        },
        g);
    for (int k = 0; k < m / 10; ++k) {
      q->Sync([&] { ++cnt2; }, true);
    }
  }

  g->Wait();
  std::cout << "cnt1: " << cnt1.load(std::memory_order_relaxed)
            << ", cnt2: " << cnt2 << "\n";
}