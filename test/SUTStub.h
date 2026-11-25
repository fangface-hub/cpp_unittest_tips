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
