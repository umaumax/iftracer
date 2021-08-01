#ifndef MPSC_HPP_INCLUDED
#define MPSC_HPP_INCLUDED

#include <atomic>
#include <cassert>
#include <chrono>
#include <thread>

#include "node.hpp"

namespace iftracer {
#ifdef __x86_64__
#define pause(t) (asm volatile("pause"));
#else
#define pause(t) (std::this_thread::sleep_for(std::chrono::nanoseconds((t))));
#endif

template <typename T>
class MPSCQueue {
 public:
  using Node = Node<T>;

 private:
  std::atomic<Node*> tail;
  Node stub;

  Node* head;

  void insert(Node& first, Node& last) {
    last.next  = nullptr;
    Node* prev = tail.exchange(&last, std::memory_order_relaxed);
    prev->next = &first;
  }
  void insert(Node* first, Node* last) {
    last->next = nullptr;
    Node* prev = tail.exchange(last, std::memory_order_relaxed);
    prev->next = first;
  }

 public:
  MPSCQueue() : tail(&stub), head(&stub) {}

  void push(Node& elem) { insert(elem, elem); }
  void push(Node& first, Node& last) { insert(first, last); }
  void push(Node* elem) { insert(elem, elem); }
  void push(Node* first, Node& last) { insert(first, last); }

  // non-blocking
  bool try_pop(Node** v) {
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
    Node* l = head;
    head    = head->next;
    *v      = l;
    return true;
  }

  // pop operates in chunk of elements and re-inserts stub after each chunk
  bool pop(Node** v) {
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
    Node* l = head;
    head    = head->next;
    *v      = l;
    return true;
  }
};
}  // namespace iftracer

#endif  // MPSC_HPP_INCLUDED
