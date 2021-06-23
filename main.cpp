#include <stdio.h>
#include <chrono>
#include <thread>

class Animal {
  public:
    Animal() {}
    ~Animal() {}
};

void piyo() { printf("piyo\n"); }
void hoge() {
  printf("hoge\n");
  piyo();
}
void fuga() { printf("fuga\n"); }

Animal global_animal;

int main() {
  for (int i = 0; i < 1; i++) {
    std::thread th([&] {
      hoge();
      fuga();
      hoge();
      piyo();
      fuga();
    });
    printf("hoge:%p\n", hoge);
    printf("fuga:%p\n", fuga);
    printf("piyo:%p\n", piyo);
    printf("main:%p\n", main);
    hoge();
    fuga();

    th.join();

    hoge();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    hoge();
  }
  return 0;
}
