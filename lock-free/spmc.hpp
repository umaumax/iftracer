#ifndef SPMC_HPP_INCLUDED
#define SPMC_HPP_INCLUDED

#include <atomic>
#include <cassert>
#include <chrono>

#include "node.hpp"

namespace iftracer {
#ifdef __x86_64__
#define pause(t) (asm volatile("pause"));
#else
#define pause(t) (std::this_thread::sleep_for(std::chrono::nanoseconds((t))));
#endif

template <typename T>
class SPMCQueue {
 public:
  using Node = Node<T>;

 private:
  std::atomic<Node*> head;
  Node stub;

  Node* tail;

  void insert(Node& first, Node& last) {
    last.next = nullptr;
    if (head == nullptr) {
      tail       = &last;
      Node* prev = head.exchange(&first, std::memory_order_relaxed);
      assert(prev == nullptr);
      return;
    }
    tail->next = &first;
    tail       = &last;
  }

 public:
  SPMCQueue() : head(nullptr), tail(nullptr) {}

  void push(Node& elem) { insert(elem, elem); }
  void push(Node& first, Node& last) { insert(first, last); }

  // non-blocking
  bool try_pop(Node** v) {
    while (head != nullptr) {
      Node* prev = head.exchange(nullptr, std::memory_order_relaxed);
      if (prev == nullptr) {
        continue;
      }
      Node* dummy_prev = head.exchange(prev->next, std::memory_order_relaxed);
      assert(dummy_prev == nullptr);
      *v = prev;
      return true;
    }
    return false;
  }
};
}  // namespace iftracer

#endif  // SPMC_HPP_INCLUDED
