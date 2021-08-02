#ifndef LOCK_FREE_QUEUE_WORKER_HPP_INCLUDED
#define LOCK_FREE_QUEUE_WORKER_HPP_INCLUDED

#include <atomic>
#include <functional>
#include <thread>

#include "lock-free/lock-free-queue.hpp"

namespace iftracer {
template <class T>
class LockFreeQueueWorker {
 public:
  LockFreeQueueWorker(std::size_t buffer_number, std::function<void(T)> task)
      : task_(task) {
    queue_.Init(buffer_number);
  }

  virtual ~LockFreeQueueWorker() {
    if (!stop_flag_.load()) Join();
  }

  void Post(T item) {
    while (true) {
      bool ret = queue_.try_push(item);
      if (ret) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::nanoseconds(1));
    }
  }

  void Join() {
    stop_flag_.store(true);
    if (worker_.joinable()) {
      worker_.join();
    }
  }

 private:
  void Spawn() {
    worker_ = std::thread([&]() {
      while (stop_flag_.load()) {
        T x;
        bool ret = queue_.try_pop(&x);
        if (ret) {
          task_(x);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
    });
  }

  LockFreeMPSCQueue<T> queue_;
  std::thread worker_;
  std::function<void(T)> task_;
  std::atomic<bool> stop_flag_{false};
};
}  // namespace iftracer

#endif  // LOCK_FREE_QUEUE_WORKER_HPP_INCLUDED
