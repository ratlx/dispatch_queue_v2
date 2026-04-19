#include <iostream>
#include <semaphore>

#include "dispatch_queue/dispatch_queue.h"

int main() {
  auto q = Rat::CreateQueue(true);
  int i = 0;
  std::cout << std::this_thread::get_id() << std::endl;
  q->Sync([&] {
    ++i;
    std::cout << std::this_thread::get_id() << std::endl;
  });
  std::cout << i << std::endl;

  auto s = std::binary_semaphore(0);
  q->Async([&] {
    ++i;
    std::cout << std::this_thread::get_id() << std::endl;
    s.release();
  });

  s.acquire();
  std::cout << i;
}