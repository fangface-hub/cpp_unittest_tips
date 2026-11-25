# cpp_unittest_tips

## Sample: 100% unit-test coverage with CppDatasetUtil

`src/SUT.*` is a minimal SUT that reads a `Dataset` from
[CppDatasetUtil](https://github.com/fangface-hub/CppDatasetUtil) (git submodule
in `third_party/`). `Calc` branches on whether `"x"` exists and is an integer;
`Process` calls `Calc` internally and catches its exception. It is tested with
the union-based stub technique: no `virtual`, no changes to the SUT.

| File | Role |
| :-- | :-- |
| `test/SUTRename.h` | Renames `Calc`/`Process` to `CalcStub`/`ProcessStub`. Force-included when compiling `SUT.cpp` and included by the stub. |
| `test/SUTStub.h` | `SUTStub` derives from `SUT`; each method returns the real result (`SetUseReal`) or a canned value (`SetStubReturn`, stored in a `union`). It ends with `#define SUT SUTStub`. |
| `test/test_sut.cpp` | Compiled with `--include SUTStub.h`; it only writes `SUT`, so the stub is the only code that knows the real class. |

Calls inside the SUT (`Process` -> `Calc`) always reach the real code; only
calls from the test are stubbed.

```sh
git submodule update --init
make test       # build and run
make coverage   # gcov; fails unless line coverage of SUT.cpp is 100%
```

Only the SUT is instrumented (`--coverage`). Requires g++ (C++17), make, gcov.
