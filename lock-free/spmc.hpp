#ifndef SPMC_HPP_INCLUDED
#define SPMC_HPP_INCLUDED

#include <atomic>
#include <cassert>

#include "node.hpp"

namespace iftracer {
#ifdef __x86_64__
#define pause(t) \
  { asm volatile("pause"); }
#else
#include <chrono>
#include <thread>
#define pause(t) \
  { std::this_thread::sleep_for(std::chrono::nanoseconds((t))); }
#endif

template <typename T>
class SPMCQueue {
 public:
  using NodeT = Node<T>;

 private:
  std::atomic<NodeT*> head{nullptr};
  NodeT stub;

  NodeT* tail;

  void insert(NodeT& first, NodeT& last) {
    last.next = nullptr;
    if (head == nullptr) {
      tail        = &last;
      NodeT* prev = head.exchange(&first, std::memory_order_relaxed);
      assert(prev == nullptr);
      return;
    }
    tail->next = &first;
    tail       = &last;
  }

 public:
  SPMCQueue() : head(nullptr), tail(nullptr) {}

  void push(NodeT& elem) { insert(elem, elem); }
  void push(NodeT& first, NodeT& last) { insert(first, last); }

  // non-blocking
  bool try_pop(NodeT** v) {
    while (head != nullptr) {
      NodeT* prev = head.exchange(nullptr, std::memory_order_relaxed);
      if (prev == nullptr) {
        continue;
      }
      NodeT* dummy_prev = head.exchange(prev->next, std::memory_order_relaxed);
      assert(dummy_prev == nullptr);
      *v = prev;
      return true;
    }
    return false;
  }
};
}  // namespace iftracer

#endif  // SPMC_HPP_INCLUDED
