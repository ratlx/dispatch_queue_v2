#include "dispatch_queue/detail/mpsc_queue.h"

#include <gtest/gtest.h>

#include <atomic>
#include <thread>
#include <vector>

using Rat::detail::MPSCQueue;

TEST(MPSCQueue, EnqueueDequeue) {
  MPSCQueue<int> q;
  q.enqueue(42);
  auto val = q.try_dequeue();
  ASSERT_TRUE(val.has_value());
  EXPECT_EQ(*val, 42);
}

TEST(MPSCQueue, DequeueEmpty) {
  MPSCQueue<int> q;
  EXPECT_FALSE(q.try_dequeue().has_value());
}

TEST(MPSCQueue, Empty) {
  MPSCQueue<int> q;
  EXPECT_TRUE(q.empty());
  q.enqueue(1);
  EXPECT_FALSE(q.empty());
}

TEST(MPSCQueue, FIFOOrder) {
  MPSCQueue<int> q;
  for (int i = 0; i < 100; ++i) q.enqueue(i);
  for (int i = 0; i < 100; ++i) {
    auto val = q.try_dequeue();
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, i);
  }
  EXPECT_TRUE(q.empty());
}

TEST(MPSCQueue, MoveEnqueue) {
  MPSCQueue<std::unique_ptr<int>> q;
  q.enqueue(std::make_unique<int>(99));
  auto val = q.try_dequeue();
  ASSERT_TRUE(val.has_value());
  EXPECT_EQ(**val, 99);
}

TEST(MPSCQueue, Emplace) {
  MPSCQueue<std::pair<int, int>> q;
  q.emplace(1, 2);
  auto val = q.try_dequeue();
  ASSERT_TRUE(val.has_value());
  EXPECT_EQ(val->first, 1);
  EXPECT_EQ(val->second, 2);
}

TEST(MPSCQueue, Peek) {
  MPSCQueue<int> q;
  q.enqueue(10);
  int* p = q.try_peek();
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(*p, 10);
  // peek does not remove
  EXPECT_NE(q.try_peek(), nullptr);
  (void)q.try_dequeue();
  EXPECT_EQ(q.try_peek(), nullptr);
}

TEST(MPSCQueue, Pop) {
  MPSCQueue<int> q;
  q.enqueue(1);
  q.enqueue(2);
  q.pop();
  auto val = q.try_dequeue();
  ASSERT_TRUE(val.has_value());
  EXPECT_EQ(*val, 2);
}

TEST(MPSCQueue, MultiProducer) {
  MPSCQueue<int> q;
  constexpr int kProducers = 4;
  constexpr int kItems = 10000;
  std::atomic<int> total_enqueued{0};

  std::vector<std::thread> producers;
  for (int t = 0; t < kProducers; ++t) {
    producers.emplace_back([&, t]() {
      for (int i = 0; i < kItems; ++i) {
        q.enqueue(t * kItems + i);
        total_enqueued.fetch_add(1, std::memory_order_relaxed);
      }
    });
  }

  for (auto& t : producers) t.join();

  int count = 0;
  while (q.try_dequeue().has_value()) ++count;
  EXPECT_EQ(count, kProducers * kItems);
}