#include <stdio.h>

#include <chrono>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

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

#include <iostream>

void help(std::string app_name) {
  std::cout << "usage: " << app_name << " <pattern>" << std::endl
            << "    pattern: hello_world, threads" << std::endl;
}

int main(int argc, const char* argv[]) {
  if (argc == 1) {
    help(std::string(argv[0]));
    return 1;
  }
  std::string pattern(argv[1]);

  if (pattern == "hello_world") {
    std::cout << "Hello world!" << std::endl;
    char buf[256] = {0};
    snprintf(buf, sizeof(buf), "snprintf sample");
    std::cout << "snprintf:" << buf << std::endl;

    hoge();
    fuga();

    iftracer::AsyncLogger async_logger;
    async_logger.Enter("thread loop start");
    for (int i = 0; i < 10; i++) {
      iftracer::AsyncLogger async_logger;
      async_logger.Enter("for loop");
      std::thread th([&] {
        iftracer::InstantLogger("thread start");
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
      async_logger.Exit("for loop");
    }
    async_logger.Exit();
  } else if (pattern == "threads") {
    std::vector<std::thread> threads;

    int cnt = 0;
    std::mutex m;
    for (int i = 0; i < 10; i++) {
      threads.emplace_back(std::thread([&] {
        for (int i = 0; i < 100; i++) {
          std::lock_guard<std::mutex> lock(m);
          cnt++;
          uint64_t timestamp =
              std::chrono::duration_cast<std::chrono::microseconds>(
                  std::chrono::system_clock::now().time_since_epoch())
                  .count();
          auto scope_logger = iftracer::ScopeLogger(std::to_string(cnt) + ":" +
                                                    std::to_string(timestamp));
          std::this_thread::sleep_for(std::chrono::nanoseconds(500));
          std::this_thread::yield();
        }
      }));
    }
    {
      std::lock_guard<std::mutex> lock(m);
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    for (auto& th : threads) {
      th.join();
    }
  } else {
    help(std::string(argv[0]));
    return 1;
  }
  return 0;
}
