#pragma once
// Stub for SUT. This single header is used in two ways (see the Makefile):
//
//  1. Compiling src/SUT.cpp:  -DSUT_UNDER_TEST --include SUTStub.h
//     Only the renaming below takes effect, so SUT.cpp defines
//     SUT::CalcStub / SUT::ProcessStub. A call such as Calc(data) inside
//     Process is renamed too, so the SUT's self-calls always reach real code.
//
//  2. Compiling the test:     --include SUTStub.h
//     The test only ever writes "SUT"; the final #define turns it into
//     SUTStub, so SUTStub is the only code that knows the real class.
#include <cstdint>
#include <map>
#include <stdexcept>

// Include everything that must not be renamed BEFORE the macros below.
#include "cppdatasetutil.h"

// Rename the SUT's methods, freeing the original names for the stub class.
#define Calc CalcStub
#define Process ProcessStub
#include "SUT.h"

#ifndef SUT_UNDER_TEST

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

#endif  // SUT_UNDER_TEST
