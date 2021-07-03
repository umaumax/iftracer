# iftracer

instrument-functions tracer

## how to build with your app
link iftracer library and build target application with `-finstrument-functions`

### cmake
if you use `cmake`, just add below script to `CMakeLists.txt` and run `git clone https://github.com/umaumax/iftracer`

``` cmake
add_subdirectory(iftracer)
set(IFTRACER_COMPILE_FLAGS "-std=c++11 -lpthread -g1 -DIFTRACER_ENABLE_API -finstrument-functions -finstrument-functions-exclude-file-list=bits,include/c++")
set_property(TARGET ${PROJECT_NAME} APPEND PROPERTY COMPILE_FLAGS "${IFTRACER_COMPILE_FLAGS}")
target_link_libraries(${PROJECT_NAME} iftracer)

# if there are lots of cmake target, easily way is just add below code
include_directories(./iftracer)
```

### make
``` make
IFTRACER_APP_FLAGS := -DIFTRACER_ENABLE_API -finstrument-functions -finstrument-functions-exclude-file-list=bits,include/c++
ifneq ($(filter clang%,$(CXX)),)
	IFTRACER_APP_FLAGS := -DIFTRACER_ENABLE_API -finstrument-functions-after-inlining
endif
$(CURDIR)/iftracer/libiftracer.a:
	$(MAKE) -C ./iftracer

CXXFLAGS := $(CXXFLAGS) -I$(CURDIR)/iftracer -g1 $(IFTRACER_APP_FLAGS)
$(TARGET_APP): $(CURDIR)/iftracer/libiftracer.a
```

rewrite `$(TARGET_APP)` to your main application target

### NOTE
depending on the situation, you can add also `-finstrument-functions-exclude-function-list=__mangled_func_name` option

Option `-g1` is sufficient if you want to know the location of the file by objdump.

e.g. `-ggdb3` output file size is 12MB, but `-g1` is 3MB

`objdump` takes a lot of time if a target file size is big.
This is especially noticeable with cross objdump(e.g. arm-linux-gnueabihf-objdump).

## how to use API
``` cpp
#include "iftracer.hpp"

// recommendation way
void task() {
  auto scope_logger = iftracer::ScopeLogger("eating 🍣");
  // do something
}

void task(int x) {
  iftracer::ScopeLogger scope_logger;
  switch (x) {
  case 0:
    scope_logger.Enter("x is 0");
    break;
  case 1:
    scope_logger.Enter("x is 1");
    break;
  default:
    scope_logger.Enter("invalid x");
    break;
  }
}

void task(int x) {
  iftracer::ScopeLogger scope_logger;
  scope_logger.Enter();
  std::string scope_logger_text = "";
  switch (x) {
  case 0:
    scope_logger_text = "x is 0";
    break;
  case 1:
    scope_logger_text = "x is 1";
    break;
  default:
    scope_logger_text = "invalid x";
    break;
  }
  scope_logger.SetText(scope_logger_text);
}

void task() {
  auto scope_logger = iftracer::ScopeLogger("playing 🎮");
  // do something
  scope_logger.Exit();
}

// bad examplel
void task() {
  auto scope_logger1 = iftracer::ScopeLogger();
  auto scope_logger2 = iftracer::ScopeLogger();
  scope_logger1.Enter("doing task1");
  // do something
  scope_logger2.Enter("doing task2");
  // do something
  // order must be like a stack!!
  scope_logger1.Exit(); // reverse order!!
  scope_logger2.Exit(); // reverse order!!;
}

// bad examplel
// unintentionally causes two destructor calls
void task() {
  std::vector<iftracer::ScopeLogger> scope_loggers;
  scope_loggers.emplace_back(iftracer::ScopeLogger("hoge function called!"));
}

```

### bad examples
下記では、意図せずにデストラクタが呼ばれるので、`iftracer::ScopeLogger`をコピー不可能にすることで根本的な対策とした
``` cpp
iftracer::ScopeLogger scope_logger;
scope_logger = std::move(iftracer::ScopeLogger("hoge function called!"));

iftracer::ScopeLogger scope_logger;
scope_logger = iftracer::ScopeLogger("hoge function called!");
```

## how to run example
### cmake
``` bash
mkdir build
cd build
cmake .. -DIFTRACER_EXAMPLE=1
make
./iftracer_main

../conv.sh ./iftracer_main
```

for Max OS X
``` bash
# g++-11
CXX=g++-11 cmake .. -DIFTRACER_EXAMPLE=1

# for dwarf info
dsymutil iftracer_main
```

### make
``` bash
make
make run
./conv.sh ./iftracer_main
```

for Max OS X
``` bash
# g++-11
make CXX=g++-11

# for dwarf info
dsymutil iftracer_main
```

## for detail
### environment variables
* `IFTRACER_INIT_BUFFER=4096`: 各スレッドの初期バッファサイズ(4KB単位)(デフォルト: 4KB*4096=16MB)
  * `munmap`のみを定期的に発行して、適宜、ファイルへ出力する仕様へ変更したため、初回に大容量のメモリを確保する方法がオススメ
    * first touchが行われるまで実際のメモリ確保が行われないことを利用している
    * `msync`を発行してしまうと、`fsync`相当のディスクへの書き込みが強制的に発生してしまう
  * 短時間のトレースならば、この値を大きく設定することで、トレース時の負荷を終了時にまとめられる
* `IFTRACER_EXTEND_BUFFER=8`: 各スレッドの拡張バッファサイズ(4KB単位)(デフォルト: 4KB*8=32KB)
  * トレースの途中で内部処理に時間がかかってしまうので、なるべく小さな単位を指定する
  * `IFTRACER_INIT_BUFFER`にて、大容量を確保する形式とすれば、この値はほとんど利用されない
* `IFTRACER_FLUSH_BUFFER=64`: 各スレッドのメモリ解放(`munmap`)するバッファサイズのしきい値(4KB単位)(デフォルト: 4KB*64=256KB)
  * `munmap`後は、OS任せで非同期にファイルへ書き込まれる
  * 1回あたり、`0.1ms`~`0.5ms`ほどの処理時間である
* `IFTRACER_OUTPUT_DIRECTORY=./`: トレースログの出力先のディレクトリ
* `IFTRACER_OUTPUT_FILE_PREFIX=iftracer.out.`: トレースログの出力ファイルのprefix
* `IFTRACER_ASYNC_MUNMAP=0`: 対象プロセス上に`munmap`を実行するスレッドを別途作成し、そこで実行するかどうか(0以外の数値を設定するとスレッドが起動する)
  * 1回あたり、`0.03ms`~`1.5msと`なり、普通に`munmap`を呼ぶ場合と比較して、揺れ幅が増えているので、スレッドを立ち上げる副作用もあり、あまりおすすめしない
  * 有効にしない限り、スレッドは立ち上がらない

## data format
| event_flag | description                            | binary content                                                   |
|------------|----------------------------------------|------------------------------------------------------------------|
| `0x0`      | enter function(12B/8B)                 | `timestamp_diff(4B)` -> `function address(8B/4B)`                |
| `0x1`      | internal enter(4B), external enter(4B) | `timestamp_diff(4B)`                                             |
| `0x2`      | exit function(4B), internal exit(4B)   | `timestamp_diff(4B)`                                             |
| `0x3`      | external exit(12B~/8B~)                | `timestamp_diff(4B)` -> `text_size(4B)` -> `text(4B aligned)` |

* internal enter/exit (used in iftracer itself)
* external enter/exit (used as API)

### timestamp_diff
32bit
| MSB |  LSB |
|:----|---:|
|31-30|0|
|event_flag|timestamp data|

* microsecond単位
* 1つ前のtimestampを基準にして差分(>=0)+1を利用する
  * メインスレッドの一番最初の値を基準とする
  * timestamp dataの値が0であると、区別がつかないかつ壊れたファイル読み込み時の0値と区別がつかないため、オフセットとして1ずらしている
* range: 0 us ~ 2^30-1-1 us (1073.741822 sec)

### function address
アーキテクチャ依存で、32bit or 64bitとなる

### text_size
4B alignedされる前のtextのサイズ(32bit)

### text
サイズは4B alignedとなり、終端のNULL文字は不要で、パディング値は任意

## 個別に関数をフィルタする例
``` bash
nm ./a.out | cut -c20- | grep -e duration -e clock | sed 's/@@.*$//' > exclude-function-list.txt
echo -finstrument-functions-exclude-function-list=$(cat exclude-function-list.txt | tr '\n' ',')
```

## ヘッダサーチパス
``` bash
$ g++ -E -v - </dev/null |& grep -E '^ /[^ ]+$'
 /usr/lib/gcc/x86_64-linux-gnu/5/include
 /usr/local/include
 /usr/lib/gcc/x86_64-linux-gnu/5/include-fixed
 /usr/include/x86_64-linux-gnu
 /usr/include
```

* `-finstrument-functions-exclude-file-list=bits`: これを最低限使用しないとSEGVが発生するケースがある
* `-finstrument-functions-exclude-file-list=bits,include/c++`: トレース量の削減のために、多くの標準ライブラリを無効化する

## メモ
下記をビルドオプションを変更して、分割ビルドする仕組み
``` cpp
void __cyg_profile_func_enter(void* func_address, void* call_site);
void __cyg_profile_func_exit(void* func_address, void* call_site);
```
ただし、上記関数内で利用する標準ライブラリ関数のテンプレートやインライン最適化された関数をアプリケーション側でトレースするような設定とすると、無限ループとなるので、スタックオーバーフローとなることに注意

* ファイルスコープの変数が優先的に初期化され、さらにそれがファイルスコープなTLSを呼び出す場合に、一連のTLSの初期化が始まることを利用して、極力`thread_local Logger logger;`の初期化のタイミングを早めている

* `call_site`は呼び出し元のPC(プログラムカウンター)なので、呼び出し元の関数のアドレスとピッタリとはならない

* 関数リストのみで除外しようとしても、SEGVが発生するので、ファイルオプションを利用して、標準ライブラリ群を除外
  * 主な原因は最適化されたテンプレート関数などで、予め生成した実行ファイルを`nm`してもその情報を得られないため

* ASLRの無効化を明示的に行わなくてもトレースできている(Ubuntu)
  * MacではASLRが必要
* macでは正しいtidがうまく取得できていない

`-finstrument-functions`でフックが埋め込まれないアドレスは存在するし、おそらく、上位2bitは0利用しなくとも問題ないと考えられるので、関数のアドレスの上位2bitに情報を埋め込む

ファイルの書き出し速度

* e.g. 遅いマシンの例で、16KBの書き出しに、4ms~10msほど
  * 一定量までの書き出しは一定時間に抑えられるのでは?
  * 1B~8KB: 2msよこばい
  * 16KB: 3ms
  * 24KB: 3.75ms
  * 32KB: 4.5ms
  * 48KB: 6ms
  * 64KB: 7.5ms
  * 線形で増え、おおよそ3ms+(size-16)/8*0.75(ただし、たまに、外れ値があり、1~15msほど余計に時間がかかる)
* 通常のマシンでは、60us~300us(120KB)の間を線形で、外れ値は1ms~4.5msや22msなど

## tips
### What to do if you run the program before erasing the previous trace data.
``` bash
$ ls -t iftracer.out.* | xargs stat -c '%-24n:%y'
iftracer.out.20272      :2021-07-01 20:52:17.379974198 +0900
iftracer.out.20367      :2021-07-01 20:52:17.367974066 +0900
iftracer.out.20365      :2021-07-01 20:52:17.355973934 +0900
iftracer.out.20363      :2021-07-01 20:52:17.347973845 +0900
iftracer.out.20358      :2021-07-01 20:52:17.335973713 +0900
iftracer.out.20345      :2021-07-01 20:52:17.327973625 +0900
iftracer.out.20341      :2021-07-01 20:52:17.315973493 +0900
iftracer.out.20320      :2021-07-01 20:52:17.307973406 +0900
iftracer.out.20313      :2021-07-01 20:52:17.295973274 +0900
iftracer.out.20295      :2021-07-01 20:52:17.283973142 +0900
iftracer.out.20273      :2021-07-01 20:52:17.275973053 +0900
iftracer.out.4975       :2021-07-01 20:51:41.271576776 +0900
iftracer.out.5138       :2021-07-01 20:51:41.259576644 +0900
iftracer.out.5112       :2021-07-01 20:51:41.255576599 +0900
iftracer.out.5099       :2021-07-01 20:51:41.243576467 +0900
iftracer.out.5091       :2021-07-01 20:51:41.231576335 +0900
iftracer.out.5063       :2021-07-01 20:51:41.223576246 +0900
iftracer.out.5056       :2021-07-01 20:51:41.211576115 +0900
iftracer.out.5026       :2021-07-01 20:51:41.203576026 +0900
iftracer.out.5022       :2021-07-01 20:51:41.195575938 +0900
iftracer.out.5006       :2021-07-01 20:51:41.179575762 +0900
iftracer.out.4976       :2021-07-01 20:51:41.167575630 +0900
```

force to remove tracer logs
``` bash
rm -rf iftracer.out.*; ./target-app
```
