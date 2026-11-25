#pragma once
// Force-included when compiling SUT.cpp and included by SUTStub.h, so both see
// the same class: Calc/Process are renamed CalcStub/ProcessStub, freeing the
// original names for the stub. Headers that must not be renamed come first.
#include <cstdint>
#include <stdexcept>

#include "cppdatasetutil.h"

#define Calc CalcStub
#define Process ProcessStub
#include "SUT.h"
