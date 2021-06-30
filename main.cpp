#include <stdio.h>

#include <chrono>
#include <iostream>
#include <thread>

#include "iftracer.hpp"

class Animal {
 public:
  Animal() {}
  ~Animal() {}
};

void piyo() {
  auto scope_logger = iftracer::ScopeLogger();
  scope_logger.Enter("piyo function called!");
  printf("piyo\n");
  scope_logger.Exit();
}
void hoge() {
  printf("hoge\n");
  piyo();
}
void fuga() {
  auto scope_logger = iftracer::ScopeLogger("fuga function called!");
  printf("fuga\n");
  scope_logger.Exit();
}

Animal global_animal;

int main() {
  std::cout << "Hello world!" << std::endl;
  char buf[256] = {0};
  snprintf(buf, sizeof(buf), "snprintf sample");
  std::cout << "snprintf:" << buf << std::endl;

  hoge();
  fuga();

  for (int i = 0; i < 10; i++) {
    std::thread th([&] {
      hoge();
      fuga();
      hoge();
      piyo();
      fuga();
    });
    for (int i = 0; i < 10; i++) {
      printf("hoge:%p\n", hoge);
      printf("fuga:%p\n", fuga);
      printf("piyo:%p\n", piyo);
      printf("main:%p\n", main);
      hoge();
      fuga();
    }
    th.join();
    hoge();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    hoge();
  }
  return 0;
}
