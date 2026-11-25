// Compiles the real SUT.cpp against the stub header. The header ends with
// "#define SUT SUTStub", which would turn SUT::Calc into SUTStub::Calc, so it
// is undone here; the renaming is re-enabled so that SUT.cpp defines
// SUT::CalcStub / SUT::ProcessStub. SUT.cpp itself is not modified.
#include "SUTStub.h"

#undef SUT
#define Calc CalcStub
#define Process ProcessStub
#include "SUT.cpp"
#undef Calc
#undef Process
