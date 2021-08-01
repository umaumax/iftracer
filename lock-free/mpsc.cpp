#include "mpsc.hpp"

#include <iostream>

using namespace iftracer;

int main(int argc, char* argv[]) {
  MPSCQueue<int> q;

  Node<int> p1(123);
  Node<int> p2(246);
  std::thread th([&]() {
    q.push(p1);
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    q.push(p2);
  });
  for (int i = 0; i < 10; i++) {
    Node<int>* x = nullptr;
    bool ret     = q.try_pop(&x);
    // bool ret = q.pop(&x);
    std::cout << "ret=" << ret << std::endl;
    std::cout << "x=" << x << std::endl;
    if (x != nullptr) {
      std::cout << "value=" << x->value_ << std::endl;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  th.join();
  return 0;
}
