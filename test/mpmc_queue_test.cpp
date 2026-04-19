#include "dispatch_queue/detail/mpmc_queue.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <numeric>
#include <thread>
#include <vector>

using Rat::detail::MPMCQueue;

const uint32_t num_threads = std::thread::hardware_concurrency();

// --- Basic single-threaded tests ---

TEST(MPMCQueue, EnqueueDequeue) {
  MPMCQueue<int> q(4);
  q.enqueue(42);
  EXPECT_EQ(q.dequeue(), 42);
}

TEST(MPMCQueue, TryEnqueueTryDequeue) {
  MPMCQueue<int> q(4);
  EXPECT_TRUE(q.try_enqueue(1));
  auto val = q.try_dequeue();
  ASSERT_TRUE(val.has_value());
  EXPECT_EQ(*val, 1);
}

TEST(MPMCQueue, TryDequeueEmpty) {
  MPMCQueue<int> q(4);
  EXPECT_EQ(q.try_dequeue(), std::nullopt);
}

TEST(MPMCQueue, TryEnqueueFull) {
  MPMCQueue<int> q(4);
  for (int i = 0; i < 4; ++i) ASSERT_TRUE(q.try_enqueue(i));
  EXPECT_FALSE(q.try_enqueue(99));
}

TEST(MPMCQueue, FIFOOrder) {
  MPMCQueue<int> q(64);
  for (int i = 0; i < 50; ++i) q.enqueue(i);
  for (int i = 0; i < 50; ++i) EXPECT_EQ(q.dequeue(), i);
}

TEST(MPMCQueue, FIFOOrderTry) {
  MPMCQueue<int> q(64);
  for (int i = 0; i < 50; ++i) ASSERT_TRUE(q.try_enqueue(i));
  for (int i = 0; i < 50; ++i) {
    auto val = q.try_dequeue();
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, i);
  }
}

TEST(MPMCQueue, WrapAround) {
  MPMCQueue<int> q(4);
  for (int round = 0; round < 100; ++round) {
    for (int i = 0; i < 4; ++i) q.enqueue(round * 4 + i);
    for (int i = 0; i < 4; ++i) EXPECT_EQ(q.dequeue(), round * 4 + i);
  }
}

TEST(MPMCQueue, MoveEnqueue) {
  MPMCQueue<std::unique_ptr<int>> q(4);
  q.enqueue(std::make_unique<int>(99));
  auto val = q.dequeue();
  ASSERT_NE(val, nullptr);
  EXPECT_EQ(*val, 99);
}

TEST(MPMCQueue, Emplace) {
  MPMCQueue<std::pair<int, int>> q(4);
  q.emplace(1, 2);
  auto val = q.dequeue();
  EXPECT_EQ(val.first, 1);
  EXPECT_EQ(val.second, 2);
}

TEST(MPMCQueue, TryEmplace) {
  MPMCQueue<std::pair<int, int>> q(4);
  EXPECT_TRUE(q.try_emplace(3, 4));
  auto val = q.try_dequeue();
  ASSERT_TRUE(val.has_value());
  EXPECT_EQ(val->first, 3);
  EXPECT_EQ(val->second, 4);
}

// Verify linearizability: no loss, no duplication, per-consumer per-producer
// monotonic.
template <typename EnqueueFn, typename DequeueFn>
void linearizability_test(uint32_t capacity, uint32_t num_producers,
                          uint32_t num_consumers, uint32_t items_per_producer,
                          EnqueueFn enqueue_fn, DequeueFn dequeue_fn) {
  MPMCQueue<int> q(capacity);
  const auto total = num_producers * items_per_producer;

  std::vector<std::thread> prod_threads;
  for (int t = 0; t < num_producers; ++t) {
    prod_threads.emplace_back([&, t]() {
      for (int i = 0; i < items_per_producer; ++i)
        enqueue_fn(q, t * items_per_producer + i);
    });
  }

  // Each consumer records into its own vector (no lock needed).
  std::vector<std::vector<int>> consumed(num_consumers);

  std::vector<std::thread> cons_threads;
  auto per_consumer = total / num_consumers;
  for (int t = 0; t < num_consumers; ++t) {
    cons_threads.emplace_back([&, t]() {
      int count = (t == num_consumers - 1)
                      ? total - per_consumer * (num_consumers - 1)
                      : per_consumer;
      for (int i = 0; i < count; ++i) consumed[t].push_back(dequeue_fn(q));
    });
  }

  for (auto& t : prod_threads) t.join();
  for (auto& t : cons_threads) t.join();

  // 1. No loss, no duplication: merge and check sorted consumed == [0, total).
  std::vector<int> all;
  all.reserve(total);
  for (auto& v : consumed) all.insert(all.end(), v.begin(), v.end());
  std::sort(all.begin(), all.end());
  std::vector<int> expected(total);
  std::iota(expected.begin(), expected.end(), 0);
  ASSERT_EQ(all, expected);

  // 2. Per-consumer, per-producer monotonic: within each consumer's local
  // order,
  //    values from the same producer must be strictly increasing.
  for (int c = 0; c < num_consumers; ++c) {
    std::vector<int> last_seq(num_producers, -1);
    for (int val : consumed[c]) {
      int producer = val / items_per_producer;
      int seq = val % items_per_producer;
      EXPECT_GT(seq, last_seq[producer])
          << "consumer " << c << " saw producer " << producer << " seq "
          << last_seq[producer] << " then " << seq;
      last_seq[producer] = seq;
    }
  }
}

TEST(MPMCQueue, MultiProducerMultiConsumer) {
  linearizability_test(
      1024, num_threads / 2, num_threads / 2, 10000,
      [](MPMCQueue<int>& q, int v) { q.enqueue(v); },
      [](MPMCQueue<int>& q) -> int { return q.dequeue(); });
}

TEST(MPMCQueue, MultiProducerMultiConsumerTry) {
  linearizability_test(
      1024, num_threads / 2, num_threads / 2, 10000,
      [](MPMCQueue<int>& q, int v) { while (!q.try_enqueue(v)); },
      [](MPMCQueue<int>& q) -> int {
        while (true) {
          if (auto v = q.try_dequeue()) {
            return *v;
          }
        }
      });
}