[English](README.md) | 日本語 | [Deutsch](README_de.md) | [Français](README_fr.md) | [Italiano](README_it.md) | [한국어](README_ko.md) | [简体中文](README_zh.md)

# C++ スタブ設計のテクニック

単体テストのカバレッジを 100% にするためのスタブ設計のノウハウです。
このリポジトリは、そのテクニックを [CppDatasetUtil](https://github.com/fangface-hub/CppDatasetUtil) を使う SUT に適用した具体例です。

- テスト対象(SUT)のコードを一切変更しない
- `virtual` は一切不要
- テストコードは `SUT` という名前だけを書く。実クラスを知っているのはスタブだけ
- メソッドごとに「本物」と「スタブ」を切り替えられる

## テスト対象

**src/SUT.h**

```cpp
#pragma once
#include "cppdatasetutil.h"

class SUT {
 public:
  // 2 * data["x"]; 0 if "x" is absent. Throws std::invalid_argument if "x" is
  // not an integer.
  int Calc(const Dataset& data);
  // Calc(data) + 1; -1 if Calc throws. Calls Calc internally.
  int Process(const Dataset& data);
};
```

**src/SUT.cpp**

```cpp
#include "SUT.h"

#include <stdexcept>

int SUT::Calc(const Dataset& data) {
  if (!data.contains("x")) {
    return 0;
  }
  const auto* x = cppdatasetutil::get_if<std::int64_t>(&data["x"].value());
  if (!x) {
    throw std::invalid_argument("x must be an integer");
  }
  return static_cast<int>(*x) * 2;
}

int SUT::Process(const Dataset& data) {
  try {
    return Calc(data) + 1;  // internal call
  } catch (const std::invalid_argument&) {
    return -1;
  }
}
```
`Calc` は 3 経路(キー無し / 整数 / 整数以外)、`Process` は 2 経路(正常 / 例外)を持ちます。
`Process` は内部で `Calc` を呼びます。

## テストスタブ

ヘッダは `test/SUTStub.h` 1 つだけです。各手順はコード中のコメントに書いてあります。押さえるべき点は以下のとおりです。

**test/SUTStub.h**

```cpp
#pragma once
// Stub for SUT, compiled into every test build with --include SUTStub.h.
// The production build never uses this header.
//
//  - Calc / Process are renamed CalcStub / ProcessStub before SUT.h is read.
//    test/SUTStub.cpp compiles the real SUT.cpp with the same renaming, so a
//    call such as Calc(data) inside Process is renamed too and the SUT's
//    self-calls always reach real code.
//  - SUTStub derives from SUT and defines the original names.
//  - The final #define turns "SUT" into SUTStub, so the test only ever writes
//    "SUT" and SUTStub is the only code that knows the real class.
#include <cstdint>
#include <map>
#include <stdexcept>

// Include everything that must not be renamed BEFORE the macros below.
#include "cppdatasetutil.h"

// Rename the SUT's methods, freeing the original names for the stub class.
#define Calc CalcStub
#define Process ProcessStub
// Drop a leftover "#define SUT SUTStub" if this header is seen again.
#ifdef SUT
#undef SUT
#endif
#include "SUT.h"

#undef Calc
#undef Process

class SUTStub : public SUT {
 public:
  enum class Method { Calc, Process };

  // One slot per return type; add members (and StubTag values) as needed.
  union StubValue {
    int i;
    StubValue() {}
    ~StubValue() {}
  };

  enum class StubTag { None, Int };

  struct StubConfig {
    bool useReal = false;
    StubTag tag = StubTag::None;
    StubValue value;
  };

  std::map<Method, StubConfig> configMap;

  void SetUseReal(Method m, bool use) { configMap[m].useReal = use; }

  void SetStubReturn(Method m, int v) {
    configMap[m].value.i = v;
    configMap[m].tag = StubTag::Int;
  }

  int Calc(const Dataset& data) {
    auto& c = configMap[Method::Calc];
    return c.useReal ? SUT::CalcStub(data) : c.value.i;
  }
  int Process(const Dataset& data) {
    auto& c = configMap[Method::Process];
    return c.useReal ? SUT::ProcessStub(data) : c.value.i;
  }
};

// From here on, "SUT" means the stub.
#define SUT SUTStub
```
- **付け替え**: `SUT.h` をインクルードする前に `#define Calc CalcStub` / `#define Process ProcessStub` で SUT のメソッド名を付け替え、元の名前をスタブクラスで使えるようにします。
  付け替えたくない標準ヘッダやライブラリのヘッダは、これらのマクロの**前**にインクルードします。ライブラリ内の同名メソッドを壊さないためです。
- **本物の SUT も同じヘッダでコンパイル**: 実ビルドは `SUTStub.h` を一切使いません。テストビルドでは `test/SUTStub.cpp` がこれをインクルードし、末尾の `#define SUT SUTStub` を `#undef` して付け替えを再度有効にしたうえで、無修正の `SUT.cpp` をインクルードします。これにより `SUT::CalcStub` / `SUT::ProcessStub` が定義されます。
  `Process` 内の `Calc(data)` も `CalcStub(data)` に置き換わるため、**SUT 内部の自己呼び出しは常に本物を通ります**。
- **スタブクラス**: `SUTStub` は `SUT` を継承し、元の名前 `Calc` / `Process` を定義します。
  `useReal` が真なら `SUT::CalcStub` などの本物を、偽なら設定済みのスタブ値を返します。
- **戻り値の型**: 増やすときは `StubValue`(union)にメンバーを、`StubTag` に値を足します。
  メソッドごとに戻り値型が違っても、`configMap` 1 つで管理できます。
- 末尾の **`#define SUT SUTStub`** が要点です。**実コード上の `SUT` を `SUTStub` に置き換え、`SUT` を知っているのは `SUTStub` だけにします。**
  テストコードは `SUT sut;` と書くだけで、実体はスタブになります。

## テストコード

**test/test_sut.cpp**(抜粋。`#include "SUTStub.h"` は書かない)

```cpp
int main() {
  SUT stub;
  using Method = SUT::Method;

  Dataset empty;                // Calc: "x" absent
  Dataset five;
  five["x"] = 5;                // Calc: integer
  Dataset text;
  text["x"] = "five";           // Calc: not an integer

  // Real Calc covers all three paths.
  stub.SetUseReal(Method::Calc, true);
  CHECK(stub.Calc(empty) == 0);
  CHECK(stub.Calc(five) == 10);
  CHECK_THROWS(stub.Calc(text));

  // Stubbed Process.
  stub.SetStubReturn(Method::Process, 100);
  CHECK(stub.Process(five) == 100);

  // Real Process; its internal call reaches the real Calc (CalcStub).
  stub.SetUseReal(Method::Process, true);
  CHECK(stub.Process(five) == 11);
  CHECK(stub.Process(text) == -1);  // exception branch

  // Stubbed Calc is used when called from the test.
  stub.SetUseReal(Method::Calc, false);
  stub.SetStubReturn(Method::Calc, 42);
  CHECK(stub.Calc(five) == 42);
  CHECK(stub.Process(five) == 11);  // still the real Calc inside Process
}
```

## ビルド

```makefile
# SUT: SUTStub.cpp がスタブヘッダ付きで本物の SUT.cpp をコンパイルする。計測対象はこのオブジェクトだけ
$(CXX) -std=c++17 $(CPPFLAGS) --coverage --include SUTStub.h -c test/SUTStub.cpp -o build/SUTStub.o

# テスト: テスト自体とライブラリは計測しない
$(CXX) -std=c++17 $(CPPFLAGS) --include SUTStub.h -c test/test_sut.cpp -o build/test_sut.o
$(CXX) --coverage build/test_sut.o build/SUTStub.o libcppdatasetutil.a -o build/test_sut
```

実際の `Makefile` は [Makefile](Makefile) を参照してください。

```sh
git submodule update --init
make test       # build and run
make coverage   # gcov; fails unless line coverage of SUT.cpp is 100%
```

g++(C++17)、make、gcov が必要です。CppDatasetUtil は `third_party/CppDatasetUtil` にサブモジュールとして置いてあります。

## 内部呼び出しの挙動

| 呼び出し | 結果 |
| :-- | :-- |
| `stub.Calc(five)`、Calc を本物に設定 | `SUT::CalcStub(five)` → 10 |
| `stub.Calc(five)`、Calc をスタブに設定 | スタブ値 |
| `stub.Process(five)`、Process をスタブに設定 | スタブ値(100) |
| `stub.Process(five)`、Process を本物に設定 | `SUT::ProcessStub(five)` → 内部で `CalcStub`(本物)→ 11 |

- SUT 内部の自己呼び出し(`Process` → `Calc`)は常に本物(`CalcStub`)を通る
- スタブ化されるのはテストコードからの直接呼び出しのみ
- `virtual` は一切不要

## カバレッジ 100% を達成するコツ

- **本物を呼ぶテストで全経路を通す**: スタブはテストから見える入口を差し替えるだけです。SUT 内部の分岐は、本物のメソッドに入力を変えて呼ぶことで網羅します。
  この例では `Calc` の 3 経路は `Calc` を本物にして 3 通りの `Dataset` で、`Process` の例外分岐は `Process` を本物にして整数でない `x` を渡して通します。
- **計測対象を SUT だけにする**: `--coverage` は `SUT.cpp` のコンパイルとリンクにだけ付けます。テストコードやライブラリを混ぜると数値が薄まります(`make coverage` は `SUT.cpp` だけを判定します)。
- **判定は行カバレッジ**: gcov の分岐カバレッジには、コンパイラが生成する例外処理や `std::string` 内部の分岐が含まれ、テストからは通せないものがあります。この例では分岐は 68.75% ですが、`Dataset` の分岐は全て通しており、`make coverage` は参考値として出力するだけです。
- **`private` のメソッドを直接テストしたいとき**: `SUTStub.h` の `#include "SUT.h"` の前後を `#define private public` / `#undef private` で挟みます(標準ヘッダを先にインクルードしておくこと)。この例の SUT には `private` が無いので使っていません。

## ファイル構成

| ファイル | 役割 |
| :-- | :-- |
| `src/SUT.h`, `src/SUT.cpp` | テスト対象 |
| `test/SUTStub.h` | 付け替え、スタブクラス、`#define SUT SUTStub` |
| `test/SUTStub.cpp` | スタブヘッダを使って本物の `SUT.cpp` をコンパイル |
| `test/test_sut.cpp` | テストコード |
| `Makefile` | ビルドとカバレッジ判定 |
| `third_party/CppDatasetUtil` | サブモジュール |

---

文書バージョン: 1.0.1
