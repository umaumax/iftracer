# iftracer

instrument-functions tracer

## how to use
link iftracer library and build target application with `-finstrument-functions`

if you use `cmake`, just add below script to `CMakeLists.txt`
``` cmake
add_subdirectory(iftracer)
set(CMAKE_CXX_FLAGS "-std=c++11 -lpthread -ggdb3 -finstrument-functions -finstrument-functions-exclude-file-list=bits,include/c++ ${CMAKE_CXX_FLAGS}")
target_link_libraries(${PROJECT_NAME} iftracer)
```

depending on the situation, you can add also `-finstrument-functions-exclude-function-list=__mangled_func_name` option

### how to run example
#### cmake
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

#### make
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
  * `msync`のみを発行して、適宜、ファイルへ出力する仕様へ変更したため、初回に大容量のメモリを確保する方法がオススメ
    * first touchが行われるまで実際のメモリ確保が行われないことを利用している
  * 短時間のトレースならば、この値を大きく設定することで、トレース時の負荷を終了時にまとめられる
    * `msync`の間隔を環境変数から制御できるようにして、ファイルへflushするサイズを変更することでこれが実現できる
* `IFTRACER_EXTEND_BUFFER=8`: 各スレッドの拡張バッファサイズ(4KB単位)(デフォルト: 4KB*8=32KB)
  * トレースの途中で内部処理に時間がかかってしまうので、なるべく小さな単位を指定する
* `IFTRACER_FLUSH_BUFFER=16`: 各スレッドのフラッシュ(`munmap`)するバッファサイズのしきい値(4KB単位)(デフォルト: 4KB*16=64KB)
* `IFTRACER_OUTPUT_DIRECTORY=./`: トレースログの出力先のディレクトリ
* `IFTRACER_OUTPUT_FILE_PREFIX=iftracer.out.`: トレースログの出力ファイルのprefix

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
