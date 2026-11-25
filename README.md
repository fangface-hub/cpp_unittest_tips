English | [日本語](README_ja.md)

# C++ Stub Design Techniques

Know-how on stub design for reaching 100% unit-test coverage in C++.
This repository is a concrete example of the technique, applied to a SUT that
uses [CppDatasetUtil](https://github.com/fangface-hub/CppDatasetUtil).

- No changes to the code under test (SUT)
- No `virtual` needed
- Test code only ever writes the name `SUT`; only the stub knows the real class
- Each method can be switched individually between real and stubbed

## Code under test

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

`Calc` has three paths (key absent / integer / not an integer) and `Process`
has two (normal / exception). `Process` calls `Calc` internally.

## Test stub

The mechanism is split across two headers.

### 1. Renaming the methods (SUTRename.h)

**test/SUTRename.h**

```cpp
#pragma once
#include <cstdint>
#include <stdexcept>

#include "cppdatasetutil.h"

#define Calc CalcStub
#define Process ProcessStub
#include "SUT.h"
```

`Calc` / `Process` are renamed to `CalcStub` / `ProcessStub`, which frees the
original names for the stub class.

- Include every header that must not be renamed (standard and library
  headers) **before** the macros, so they cannot break same-named members
  inside the library.
- `SUT.cpp` is compiled with `--include SUTRename.h`, so it defines
  `SUT::CalcStub` / `SUT::ProcessStub`. The `Calc(data)` call inside `Process`
  also becomes `CalcStub(data)`, so **the SUT's self-calls always reach the
  real code**.

### 2. The stub class (SUTStub.h)

**test/SUTStub.h**

```cpp
#pragma once
#include <map>

#include "SUTRename.h"

#undef Calc
#undef Process

class SUTStub : public SUT {
 public:
  enum class Method { Calc, Process };

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

// Test code is compiled with --include SUTStub.h and only writes "SUT".
#define SUT SUTStub
```

- `SUTStub` derives from `SUT` and defines the original names `Calc` /
  `Process`. If `useReal` is true it calls the real `SUT::CalcStub` etc.;
  otherwise it returns the configured stub value.
- To support more return types, add a member to `StubValue` (the union) and a
  value to `StubTag`. One `configMap` manages methods with different return
  types.
- The final `#define SUT SUTStub` is the key. **It replaces `SUT` with
  `SUTStub` in the real code, so only `SUTStub` knows the real `SUT`.** Test
  code just writes `SUT sut;` and gets the stub.

## Test code

**test/test_sut.cpp** (excerpt; it does not `#include "SUTStub.h"`)

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

## Build

```makefile
# SUT: force-include the rename header; only the SUT is instrumented for coverage
$(CXX) -std=c++17 $(CPPFLAGS) --coverage --include SUTRename.h -c src/SUT.cpp -o build/SUT.o

# Test: force-include the stub (the test itself and the library are not measured)
$(CXX) -std=c++17 $(CPPFLAGS) --coverage --include SUTStub.h \
    test/test_sut.cpp build/SUT.o libcppdatasetutil.a -o build/test_sut
```

See the [Makefile](Makefile) for the real build.

```sh
git submodule update --init
make test       # build and run
make coverage   # gcov; fails unless line coverage of SUT.cpp is 100%
```

Requires g++ (C++17), make, and gcov. CppDatasetUtil is a submodule in
`third_party/CppDatasetUtil`.

## Behavior of internal calls

| Call | Result |
| :-- | :-- |
| `stub.Calc(five)`, Calc set to real | `SUT::CalcStub(five)` → 10 |
| `stub.Calc(five)`, Calc set to stub | the stub value |
| `stub.Process(five)`, Process set to stub | the stub value (100) |
| `stub.Process(five)`, Process set to real | `SUT::ProcessStub(five)` → internally `CalcStub` (real) → 11 |

- The SUT's self-calls (`Process` → `Calc`) always go through the real code
  (`CalcStub`)
- Only calls made directly from the test code are stubbed
- No `virtual` at all

## Tips for reaching 100% coverage

- **Cover every path with tests that call the real method.** A stub only
  replaces the entry point the test sees. Cover the SUT's internal branches by
  calling the real method with different inputs. Here, the three paths of
  `Calc` are covered with three `Dataset`s and real `Calc`, and the exception
  branch of `Process` with real `Process` and a non-integer `x`.
- **Measure only the SUT.** Add `--coverage` only when compiling `SUT.cpp` and
  when linking. Including the test code or the library dilutes the number
  (`make coverage` judges `SUT.cpp` only).
- **Gate on line coverage.** gcov's branch coverage includes compiler-generated
  exception and `std::string` internals that tests cannot reach. Here branch
  coverage is 68.75% although every `Dataset` branch is covered;
  `make coverage` prints it for reference only.
- **To test `private` methods directly**, wrap the `#include "SUT.h"` in
  `SUTRename.h` with `#define private public` / `#undef private` (after the
  standard headers are included). This SUT has no private members, so it is
  not used here.

## Files

| File | Role |
| :-- | :-- |
| `src/SUT.h`, `src/SUT.cpp` | Code under test |
| `test/SUTRename.h` | Method renaming |
| `test/SUTStub.h` | Stub class and `#define SUT SUTStub` |
| `test/test_sut.cpp` | Test code |
| `Makefile` | Build and coverage gate |
| `third_party/CppDatasetUtil` | Submodule |
