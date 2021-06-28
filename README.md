# iftracer

instrument-functions tracer

## how to use
add to `CMakeLists.txt`
``` cmake
add_subdirectory(iftracer)
set(CMAKE_CXX_FLAGS "-std=c++11 -lpthread -ggdb3 -finstrument-functions -finstrument-functions-exclude-file-list=bits,include/c++ ${CMAKE_CXX_FLAGS}")
target_link_libraries(${PROJECT_NAME} iftracer)
```

### how to run example
cmake
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

make
``` bash
make
make test
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
* `IFTRACER_INIT_BUFFER=8`: 各スレッドの初期バッファサイズ(4KB単位)(デフォルト: 4KB*8=32KB)
  * 短時間のトレースならば、この値を大きく設定することで、トレース時の負荷を終了時にまとめられる
* `IFTRACER_EXTEND_BUFFE=8`: 各スレッドの拡張バッファサイズ(4KB単位)(デフォルト: 4KB*8=32KB)
  * トレースの途中で内部処理に時間がかかってしまうので、なるべく小さな単位を指定する

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
