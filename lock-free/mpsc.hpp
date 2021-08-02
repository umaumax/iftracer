#ifndef MPSC_HPP_INCLUDED
#define MPSC_HPP_INCLUDED

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
class MPSCQueue {
 public:
  using NodeT = Node<T>;

 private:
  std::atomic<NodeT*> tail{nullptr};
  NodeT stub;

  NodeT* head;

  void insert(NodeT& first, NodeT& last) {
    last.next   = nullptr;
    NodeT* prev = tail.exchange(&last, std::memory_order_relaxed);
    prev->next  = &first;
  }
  void insert(NodeT* first, NodeT* last) {
    last->next  = nullptr;
    NodeT* prev = tail.exchange(last, std::memory_order_relaxed);
    prev->next  = first;
  }

 public:
  MPSCQueue() : tail(&stub), head(&stub) {}

  void push(NodeT& elem) { insert(elem, elem); }
  void push(NodeT& first, NodeT& last) { insert(first, last); }
  void push(NodeT* elem) { insert(elem, elem); }
  void push(NodeT* first, NodeT& last) { insert(first, last); }

  // non-blocking
  bool try_pop(NodeT** v) {
    if (head == &stub) {
      if (tail == &stub) {
        return false;
      }
      // move stub (head to last)
      head = stub.next;
      insert(stub, stub);
    }
    if (head->next == nullptr) {
      return false;
    }
    NodeT* l = head;
    head     = head->next;
    *v       = l;
    return true;
  }

  // pop operates in chunk of elements and re-inserts stub after each chunk
  bool pop(NodeT** v) {
    if (head == &stub) {  // current chunk empty
      while (stub.next == nullptr) {
        pause(1000);
      }
      head = stub.next;    // remove stub
      insert(stub, stub);  // re-insert stub at end
    }
    // wait for producer in insert()
    while (head->next == nullptr) {
      pause(1000);
    }
    // retrieve and return first element
    NodeT* l = head;
    head     = head->next;
    *v       = l;
    return true;
  }
};
}  // namespace iftracer

#endif  // MPSC_HPP_INCLUDED
