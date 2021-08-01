#include "lock-free-queue.hpp"

#include <iostream>
#include <string>
#include <thread>
#include <vector>

using namespace iftracer;

int main(int argc, char* argv[]) {
  LockFreeSPSCQueue<int> queue(10);

  std::thread th([&]() {
    for (int i = 0; i < 10; i++) {
      int x    = 0;
      bool ret = queue.try_pop(&x);
      if (ret) {
        std::cout << "done work:" << x << std::endl;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  });

  std::vector<std::thread> handlers;
  for (int i = 0; i < 5; i++) {
    handlers.emplace_back(std::thread([&, i]() {
      std::this_thread::sleep_for(std::chrono::milliseconds(200 + i * 100));
      int x    = 100 + i;
      bool ret = queue.try_push(x);
      if (ret) {
        std::cout << "set work queue:" << x << std::endl;
      }
    }));
  }
  for (auto& handler : handlers) {
    handler.join();
  }
  th.join();
  return 0;
}
