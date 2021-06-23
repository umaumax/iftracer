# iftracer

instrument-functions tracer

## how to use
add to `CMakeLists.txt`
``` cmake
add_subdirectory(iftracer)
set(CMAKE_CXX_FLAGS "-std=c++11 -lpthread -ggdb3 -finstrument-functions -finstrument-functions-exclude-file-list=chrono,include ${CMAKE_CXX_FLAGS}")
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

make
``` bash
make
make test
./conv.sh ./iftracer_main
```

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

## メモ
下記をビルドオプションを変更して、分割ビルドする仕組み
``` cpp
void __cyg_profile_func_enter(void* func_address, void* call_site);
void __cyg_profile_func_exit(void* func_address, void* call_site);
```
ただし、上記関数内で利用する`wWuU`なシンボルの関数をアプリケーション側でトレースするような設定とすると、無限ループとなるので、スタックオーバーフローとなることに注意

* `call_site`は呼び出し元のPC(プログラムカウンター)なので、呼び出し元の関数のアドレスとピッタリとはならない

* 関数リストのみで除外しようとしても、SEGVが発生するので、ファイルオプションを利用して、標準ライブラリ群を除外
  * 主な原因は最適化されたテンプレート関数などで、予め生成した実行ファイルを`nm`してもその情報を得られないため

* ASLRの無効化を明示的に行わなくてもトレースできている
