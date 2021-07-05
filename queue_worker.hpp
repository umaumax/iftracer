#ifndef QUEUE_WORKER_HPP_INCLUDED
#define QUEUE_WORKER_HPP_INCLUDED

#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <utility>
#include <vector>

namespace iftracer {
template <class T>
class QueueWorker {
 public:
  QueueWorker(std::size_t number, std::function<void(T)> task) {
    for (std::size_t i = 0; i < number; ++i) {
      workers_.emplace_back([this] { Spawn(); });
    }
    task_ = task;
  }

  virtual ~QueueWorker() {
    if (!stop_flag_) Join();
  }

  void Post(T item) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      items_.push(item);
    }

    condition_variable_.notify_one();
  }

  void Join() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      stop_flag_ = true;
    }
    condition_variable_.notify_all();
    for (auto& worker : workers_) {
      if (worker.joinable()) {
        worker.join();
      }
    }
  }

 private:
  void Spawn() {
    while (true) {
      T item;
      {
        std::unique_lock<std::mutex> lock(mutex_);
        condition_variable_.wait(
            lock, [this] { return !items_.empty() || stop_flag_; });
        if (stop_flag_ && items_.empty()) return;
        item = std::move(items_.front());
        items_.pop();
      }
      task_(item);
    }
  }

  std::vector<std::thread> workers_;
  std::queue<T> items_;
  std::function<void(T)> task_;

  std::mutex mutex_;
  std::condition_variable condition_variable_;
  bool stop_flag_ = false;
};
}  // namespace iftracer

#endif  // QUEUE_WORKER_HPP_INCLUDED
