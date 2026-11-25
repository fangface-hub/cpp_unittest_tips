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
