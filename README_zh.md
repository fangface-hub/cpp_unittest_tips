[English](README.md) | [日本語](README_ja.md) | [Deutsch](README_de.md) | [Français](README_fr.md) | [Italiano](README_it.md) | [한국어](README_ko.md) | 简体中文

# C++ 桩（Stub）设计技巧

在 C++ 中让单元测试覆盖率达到 100% 的桩设计经验。
本仓库是该技巧的具体示例，应用于使用 [CppDatasetUtil](https://github.com/fangface-hub/CppDatasetUtil) 的 SUT。

- 完全不修改被测代码（SUT）
- 不需要 `virtual`
- 测试代码只写 `SUT` 这个名字，只有桩知道真正的类
- 可以按方法逐个在真实实现和桩之间切换

## 被测代码

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
`Calc` 有三条路径（键不存在 / 整数 / 非整数），`Process` 有两条路径（正常 /
异常）。`Process` 在内部调用 `Calc`。

## 测试桩

只需一个头文件 `test/SUTStub.h` 即可完成全部工作。每一步都在代码注释中说明，
需要记住的要点如下。

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
- **重命名**：在包含 `SUT.h` 之前，用 `#define Calc CalcStub` /
  `#define Process ProcessStub` 重命名 SUT 的方法，从而把原来的名字留给桩类使用。
  不能被重命名的头文件（标准头文件和库头文件）要在这些宏**之前**包含，以免宏
  破坏库中的同名成员。
- **真实的 SUT 也用同一个头文件编译**：正式构建完全不使用 `SUTStub.h`。
  在测试构建中，`test/SUTStub.cpp` 包含它，撤销末尾的 `#define SUT SUTStub`，
  重新启用重命名，再包含未经修改的 `SUT.cpp`，从而定义
  `SUT::CalcStub` / `SUT::ProcessStub`。`Process` 内部的 `Calc(data)` 调用也会
  变成 `CalcStub(data)`，所以 **SUT 内部的自我调用始终走真实代码**。
- **桩类**：`SUTStub` 继承 `SUT`，并定义原来的名字 `Calc` / `Process`。
  `useReal` 为真时调用真实的 `SUT::CalcStub` 等，否则返回已设置的桩值。
- **返回类型**：若要支持更多类型，在 `StubValue`（union）中添加成员，并在
  `StubTag` 中添加取值。返回类型不同的方法也可以用同一个 `configMap` 管理。
- 末尾的 **`#define SUT SUTStub`** 是关键。**它把真实代码中的 `SUT` 替换为
  `SUTStub`，使得只有 `SUTStub` 知道真正的 `SUT`。** 测试代码只需写
  `SUT sut;` 即可得到桩。

## 测试代码

**test/test_sut.cpp**（节选，不写 `#include "SUTStub.h"`）

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
## 构建

```makefile
# SUT: SUTStub.cpp compiles the real SUT.cpp with the stub header; only this object is instrumented
$(CXX) -std=c++17 $(CPPFLAGS) --coverage --include SUTStub.h -c test/SUTStub.cpp -o build/SUTStub.o

# Test: the test itself and the library are not measured
$(CXX) -std=c++17 $(CPPFLAGS) --include SUTStub.h -c test/test_sut.cpp -o build/test_sut.o
$(CXX) --coverage build/test_sut.o build/SUTStub.o libcppdatasetutil.a -o build/test_sut
```
实际的构建请参见 [Makefile](Makefile)。

```sh
git submodule update --init
make test       # build and run
make coverage   # gcov; fails unless line coverage of SUT.cpp is 100%
```
需要 g++（C++17）、make 和 gcov。CppDatasetUtil 作为子模块位于
`third_party/CppDatasetUtil`。

## 内部调用的行为

| 调用 | 结果 |
| :-- | :-- |
| `stub.Calc(five)`，Calc 设为真实 | `SUT::CalcStub(five)` → 10 |
| `stub.Calc(five)`，Calc 设为桩 | 桩值 |
| `stub.Process(five)`，Process 设为桩 | 桩值（100） |
| `stub.Process(five)`，Process 设为真实 | `SUT::ProcessStub(five)` → 内部调用 `CalcStub`（真实）→ 11 |

- SUT 内部的自我调用（`Process` → `Calc`）始终走真实代码（`CalcStub`）
- 只有从测试代码直接发起的调用才会被桩替换
- 完全不需要 `virtual`

## 达到 100% 覆盖率的要点

- **用调用真实方法的测试覆盖所有路径。** 桩只替换测试所见的入口。SUT 内部的
  分支要通过用不同输入调用真实方法来覆盖。本例中，`Calc` 的三条路径通过真实
  `Calc` 加三个 `Dataset` 覆盖，`Process` 的异常分支通过真实 `Process` 加非整数
  `x` 覆盖。
- **只测量 SUT。** `--coverage` 只加在 `SUT.cpp` 的编译和链接上。混入测试代码或
  库会稀释数值（`make coverage` 只判定 `SUT.cpp`）。
- **以行覆盖率作为判定标准。** gcov 的分支覆盖率包含编译器生成的异常处理和
  `std::string` 内部分支，测试无法到达。本例的分支覆盖率为 68.75%，但
  `Dataset` 的分支已全部覆盖；`make coverage` 仅将其作为参考输出。
- **要直接测试 `private` 方法时**，在 `SUTStub.h` 中用
  `#define private public` / `#undef private` 包住 `#include "SUT.h"`（需先包含
  标准头文件）。本例的 SUT 没有私有成员，因此没有使用。

## 文件结构

| 文件 | 作用 |
| :-- | :-- |
| `src/SUT.h`, `src/SUT.cpp` | 被测代码 |
| `test/SUTStub.h` | 重命名、桩类和 `#define SUT SUTStub` |
| `test/SUTStub.cpp` | 用桩头文件编译真实的 `SUT.cpp` |
| `test/test_sut.cpp` | 测试代码 |
| `Makefile` | 构建和覆盖率判定 |
| `third_party/CppDatasetUtil` | 子模块 |

---

文档版本: 1.0.1
