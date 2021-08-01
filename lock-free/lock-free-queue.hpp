#ifndef LOCK_FREE_QUEUE_HPP_INCLUDED
#define LOCK_FREE_QUEUE_HPP_INCLUDED

#include <cstddef>

#include "mpsc.hpp"
#include "spmc.hpp"

namespace iftracer {
template <typename T>
class LockFreeMPSCQueue {
 public:
  using Node = Node<T>;

  LockFreeMPSCQueue<T>() {}
  LockFreeMPSCQueue<T>(int buffer_size) { Init(buffer_size); }
  void Init(std::size_t buffer_size) {
    for (std::size_t i = 0; i < buffer_size; i++) {
      Node node;
      nodes_.emplace_back(node);
    }
    for (auto& node : nodes_) {
      buffer_queue_.push(node);
    }
  }

  bool try_pop(T* v) {
    Node* x  = nullptr;
    bool ret = work_queue_.try_pop(&x);
    if (ret && x != nullptr) {
      *v = x->value_;
      buffer_queue_.push(*x);
      return true;
    }
    return false;
  }
  bool try_push(const T& v) {
    Node* x  = nullptr;
    bool ret = buffer_queue_.try_pop(&x);
    if (ret && x != nullptr) {
      x->value_ = v;
      work_queue_.push(x);
      return true;
    }
    return false;
  }

 private:
  SPMCQueue<T> buffer_queue_;
  MPSCQueue<T> work_queue_;
  std::vector<Node> nodes_;
};
}  // namespace iftracer

#endif  // LOCK_FREE_QUEUE_HPP_INCLUDED
