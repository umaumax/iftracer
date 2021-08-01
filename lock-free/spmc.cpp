#include "spmc.hpp"

#include <iostream>
#include <thread>
#include <vector>

using namespace iftracer;

int main(int argc, char* argv[]) {
  SPMCQueue<int> q;
  Node<int> p1(123);
  Node<int> p2(246);
  q.push(p1);
  q.push(p2);
  std::vector<std::thread> handlers;
  for (int i = 0; i < 10; i++) {
    handlers.emplace_back(std::thread([&, i]() {
      std::this_thread::sleep_for(std::chrono::milliseconds(i * 10));

      Node<int>* x = nullptr;
      bool ret     = q.try_pop(&x);
      std::cout << "ret=" << ret << std::endl;
      std::cout << "x=" << x << std::endl;
      if (x != nullptr) {
        std::cout << "value=" << x->value_ << std::endl;
      }
    }));
  }
  for (auto& handler : handlers) {
    handler.join();
  }
  return 0;
}
