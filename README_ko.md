[English](README.md) | [日本語](README_ja.md) | [Deutsch](README_de.md) | [Français](README_fr.md) | [Italiano](README_it.md) | 한국어 | [简体中文](README_zh.md)

# C++ 스텁 설계 기법

C++에서 단위 테스트 커버리지 100%를 달성하기 위한 스텁 설계 노하우입니다.
이 저장소는 [CppDatasetUtil](https://github.com/fangface-hub/CppDatasetUtil)을 사용하는 SUT에 이 기법을 적용한 구체적인 예입니다.

- 테스트 대상 코드(SUT)를 전혀 수정하지 않음
- `virtual`이 필요 없음
- 테스트 코드는 `SUT`라는 이름만 사용하며, 실제 클래스를 아는 것은 스텁뿐임
- 메서드별로 실제 구현과 스텁을 전환할 수 있음

## 테스트 대상 코드

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
`Calc`에는 세 가지 경로(키 없음 / 정수 / 정수 아님)가 있고 `Process`에는 두
가지 경로(정상 / 예외)가 있습니다. `Process`는 내부에서 `Calc`를 호출합니다.

## 테스트 스텁

헤더 하나, `test/SUTStub.h`가 모든 것을 처리합니다. 각 단계는 코드의 주석에
설명되어 있으며, 기억해야 할 점은 아래와 같습니다.

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
- **이름 바꾸기**: `SUT.h`를 인클루드하기 전에 `#define Calc CalcStub` /
  `#define Process ProcessStub`으로 SUT의 메서드 이름을 바꿔, 원래 이름을 스텁
  클래스에서 사용할 수 있게 합니다. 이름을 바꾸면 안 되는 헤더(표준 헤더와
  라이브러리 헤더)는 이 매크로들보다 **앞에** 인클루드하여, 라이브러리 안의
  같은 이름의 멤버가 깨지지 않게 합니다.
- **실제 SUT도 같은 헤더로 컴파일**: 실제(제품) 빌드는 `SUTStub.h`를 전혀
  사용하지 않습니다. 테스트 빌드에서는 `test/SUTStub.cpp`가 이를 인클루드하고,
  마지막의 `#define SUT SUTStub`을 `#undef`한 뒤 이름 바꾸기를 다시 활성화하여
  수정하지 않은 `SUT.cpp`를 인클루드합니다. 그래서 `SUT::CalcStub` /
  `SUT::ProcessStub`이 정의됩니다. `Process` 안의 `Calc(data)` 호출도
  `CalcStub(data)`로 바뀌므로 **SUT 내부의 자기 호출은 항상 실제 코드를
  거칩니다**.
- **스텁 클래스**: `SUTStub`은 `SUT`를 상속하고 원래 이름 `Calc` / `Process`를
  정의합니다. `useReal`이 참이면 실제 `SUT::CalcStub` 등을 호출하고, 거짓이면
  설정된 스텁 값을 반환합니다.
- **반환 타입**: 늘리려면 `StubValue`(union)에 멤버를, `StubTag`에 값을
  추가합니다. 반환 타입이 서로 다른 메서드도 `configMap` 하나로 관리할 수
  있습니다.
- 마지막의 **`#define SUT SUTStub`**이 핵심입니다. **실제 코드의 `SUT`를
  `SUTStub`으로 치환하여, 실제 `SUT`를 아는 것은 `SUTStub`뿐이 됩니다.**
  테스트 코드는 `SUT sut;`라고만 쓰면 스텁을 얻습니다.

## 테스트 코드

**test/test_sut.cpp** (발췌, `#include "SUTStub.h"`는 쓰지 않음)

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
## 빌드

```makefile
# SUT: SUTStub.cpp compiles the real SUT.cpp with the stub header; only this object is instrumented
$(CXX) -std=c++17 $(CPPFLAGS) --coverage --include SUTStub.h -c test/SUTStub.cpp -o build/SUTStub.o

# Test: the test itself and the library are not measured
$(CXX) -std=c++17 $(CPPFLAGS) --include SUTStub.h -c test/test_sut.cpp -o build/test_sut.o
$(CXX) --coverage build/test_sut.o build/SUTStub.o libcppdatasetutil.a -o build/test_sut
```
실제 빌드는 [Makefile](Makefile)을 참조하세요.

```sh
git submodule update --init
make test       # build and run
make coverage   # gcov; fails unless line coverage of SUT.cpp is 100%
```
g++(C++17), make, gcov가 필요합니다. CppDatasetUtil은
`third_party/CppDatasetUtil`에 서브모듈로 들어 있습니다.

## 내부 호출의 동작

| 호출 | 결과 |
| :-- | :-- |
| `stub.Calc(five)`, Calc를 실제로 설정 | `SUT::CalcStub(five)` → 10 |
| `stub.Calc(five)`, Calc를 스텁으로 설정 | 스텁 값 |
| `stub.Process(five)`, Process를 스텁으로 설정 | 스텁 값(100) |
| `stub.Process(five)`, Process를 실제로 설정 | `SUT::ProcessStub(five)` → 내부에서 `CalcStub`(실제) → 11 |

- SUT 내부의 자기 호출(`Process` → `Calc`)은 항상 실제 코드(`CalcStub`)를
  거칩니다
- 스텁이 적용되는 것은 테스트 코드에서 직접 호출한 경우뿐입니다
- `virtual`은 전혀 필요 없습니다

## 커버리지 100%를 달성하는 요령

- **실제 메서드를 호출하는 테스트로 모든 경로를 지나가게 하세요.** 스텁은
  테스트에서 보이는 진입점만 바꿔 줍니다. SUT 내부의 분기는 실제 메서드를
  다양한 입력으로 호출하여 커버합니다. 이 예에서는 `Calc`의 세 경로를 실제
  `Calc`와 세 가지 `Dataset`으로, `Process`의 예외 분기를 실제 `Process`와
  정수가 아닌 `x`로 지나갑니다.
- **측정 대상을 SUT만으로 하세요.** `--coverage`는 `SUT.cpp`의 컴파일과
  링크에만 붙입니다. 테스트 코드나 라이브러리를 포함하면 수치가 희석됩니다
  (`make coverage`는 `SUT.cpp`만 판정합니다).
- **판정은 라인 커버리지로 하세요.** gcov의 분기 커버리지에는 컴파일러가
  생성한 예외 처리나 `std::string` 내부의 분기가 포함되어 있어 테스트로는 도달할
  수 없습니다. 이 예에서 분기 커버리지는 68.75%이지만 `Dataset`의 분기는 모두
  지나갔으며, `make coverage`는 참고용으로만 출력합니다.
- **`private` 메서드를 직접 테스트하려면** `SUTStub.h`의 `#include "SUT.h"`를
  `#define private public` / `#undef private`로 감싸세요(표준 헤더를 먼저
  인클루드한 뒤). 이 예의 SUT에는 private 멤버가 없으므로 사용하지 않았습니다.

## 파일 구성

| 파일 | 역할 |
| :-- | :-- |
| `src/SUT.h`, `src/SUT.cpp` | 테스트 대상 |
| `test/SUTStub.h` | 이름 바꾸기, 스텁 클래스, `#define SUT SUTStub` |
| `test/SUTStub.cpp` | 스텁 헤더로 실제 `SUT.cpp`를 컴파일 |
| `test/test_sut.cpp` | 테스트 코드 |
| `Makefile` | 빌드와 커버리지 판정 |
| `third_party/CppDatasetUtil` | 서브모듈 |
