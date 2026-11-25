[English](README.md) | [日本語](README_ja.md) | Deutsch | [Français](README_fr.md) | [Italiano](README_it.md) | [한국어](README_ko.md) | [简体中文](README_zh.md)

# Techniken für den Entwurf von C++-Stubs

Know-how zum Stub-Entwurf, um in C++ eine Unit-Test-Abdeckung von 100 % zu erreichen.
Dieses Repository ist ein konkretes Beispiel der Technik, angewandt auf ein SUT,
das [CppDatasetUtil](https://github.com/fangface-hub/CppDatasetUtil) verwendet.

- Keine Änderungen am zu testenden Code (SUT)
- Kein `virtual` nötig
- Der Testcode schreibt immer nur den Namen `SUT`; nur der Stub kennt die echte Klasse
- Jede Methode lässt sich einzeln zwischen echt und gestubbt umschalten

## Zu testender Code

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
`Calc` hat drei Pfade (Schlüssel fehlt / Ganzzahl / keine Ganzzahl) und `Process`
zwei (normal / Ausnahme). `Process` ruft intern `Calc` auf.

## Test-Stub

Ein einziger Header, `test/SUTStub.h`, erledigt alles. Die Kommentare im Code
erklären jeden Schritt; die wichtigsten Punkte stehen unten.

**test/SUTStub.h**

```cpp
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
```
- **Umbenennen.** `#define Calc CalcStub` / `#define Process ProcessStub`
  benennen die Methoden des SUT um, bevor `SUT.h` eingebunden wird; die
  ursprünglichen Namen werden so für die Stub-Klasse frei. Binden Sie alle
  Header, die nicht umbenannt werden dürfen (Standard- und Bibliotheks-Header),
  **vor** diesen Makros ein, damit gleichnamige Member in der Bibliothek nicht
  beschädigt werden.
- **Das SUT mit demselben Header kompilieren.** `SUT.cpp` wird mit
  `-DSUT_UNDER_TEST --include SUTStub.h` kompiliert; nur das Umbenennen wirkt
  und es definiert `SUT::CalcStub` / `SUT::ProcessStub`. Auch der Aufruf
  `Calc(data)` in `Process` wird zu `CalcStub(data)`, sodass **Selbstaufrufe des
  SUT immer den echten Code erreichen**.
- **Stub-Klasse.** `SUTStub` erbt von `SUT` und definiert die ursprünglichen
  Namen `Calc` / `Process`. Ist `useReal` wahr, wird das echte
  `SUT::CalcStub` usw. aufgerufen, sonst der konfigurierte Stub-Wert
  zurückgegeben.
- **Rückgabetypen.** Für weitere Typen fügen Sie `StubValue` (der Union) ein
  Member und `StubTag` einen Wert hinzu. Eine einzige `configMap` verwaltet
  Methoden mit unterschiedlichen Rückgabetypen.
- **`#define SUT SUTStub`** am Ende ist der Kern. **Es ersetzt im echten Code
  `SUT` durch `SUTStub`, sodass nur `SUTStub` das echte `SUT` kennt.** Der
  Testcode schreibt einfach `SUT sut;` und erhält den Stub.

## Testcode

**test/test_sut.cpp** (Auszug; er enthält kein `#include "SUTStub.h"`)

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
# SUT: only the renaming part of the stub header is active; only the SUT is instrumented for coverage
$(CXX) -std=c++17 $(CPPFLAGS) --coverage -DSUT_UNDER_TEST --include SUTStub.h -c src/SUT.cpp -o build/SUT.o

# Test: force-include the stub (the test itself and the library are not measured)
$(CXX) -std=c++17 $(CPPFLAGS) --coverage --include SUTStub.h \
    test/test_sut.cpp build/SUT.o libcppdatasetutil.a -o build/test_sut
```
Den tatsächlichen Build finden Sie im [Makefile](Makefile).

```sh
git submodule update --init
make test       # build and run
make coverage   # gcov; fails unless line coverage of SUT.cpp is 100%
```
Benötigt werden g++ (C++17), make und gcov. CppDatasetUtil liegt als Submodul
in `third_party/CppDatasetUtil`.

## Verhalten interner Aufrufe

| Aufruf | Ergebnis |
| :-- | :-- |
| `stub.Calc(five)`, Calc auf echt gesetzt | `SUT::CalcStub(five)` → 10 |
| `stub.Calc(five)`, Calc auf Stub gesetzt | der Stub-Wert |
| `stub.Process(five)`, Process auf Stub gesetzt | der Stub-Wert (100) |
| `stub.Process(five)`, Process auf echt gesetzt | `SUT::ProcessStub(five)` → intern `CalcStub` (echt) → 11 |

- Selbstaufrufe des SUT (`Process` → `Calc`) laufen immer über den echten Code
  (`CalcStub`)
- Gestubbt werden nur Aufrufe, die direkt aus dem Testcode kommen
- Überhaupt kein `virtual`

## Tipps für 100 % Abdeckung

- **Decken Sie jeden Pfad mit Tests ab, die die echte Methode aufrufen.** Ein
  Stub ersetzt nur den Einstiegspunkt, den der Test sieht. Die internen Zweige
  des SUT decken Sie ab, indem Sie die echte Methode mit unterschiedlichen
  Eingaben aufrufen. Hier werden die drei Pfade von `Calc` mit drei `Dataset`s
  und echtem `Calc` abgedeckt, der Ausnahmezweig von `Process` mit echtem
  `Process` und einem nicht ganzzahligen `x`.
- **Messen Sie nur das SUT.** Fügen Sie `--coverage` nur beim Kompilieren von
  `SUT.cpp` und beim Linken hinzu. Testcode oder Bibliothek verwässern die Zahl
  (`make coverage` beurteilt nur `SUT.cpp`).
- **Bewerten Sie die Zeilenabdeckung.** Die Zweigabdeckung von gcov enthält vom
  Compiler erzeugte Ausnahme- und `std::string`-Interna, die Tests nicht
  erreichen können. Hier beträgt die Zweigabdeckung 68,75 %, obwohl jeder
  `Dataset`-Zweig abgedeckt ist; `make coverage` gibt sie nur zur Information
  aus.
- **Um `private`-Methoden direkt zu testen**, umschließen Sie das
  `#include "SUT.h"` in `SUTStub.h` mit `#define private public` /
  `#undef private` (nachdem die Standard-Header eingebunden wurden). Dieses SUT
  hat keine privaten Member, daher wird es hier nicht verwendet.

## Dateien

| Datei | Rolle |
| :-- | :-- |
| `src/SUT.h`, `src/SUT.cpp` | Zu testender Code |
| `test/SUTStub.h` | Stub-Klasse und `#define SUT SUTStub` |
| `test/test_sut.cpp` | Testcode |
| `Makefile` | Build und Abdeckungsprüfung |
| `third_party/CppDatasetUtil` | Submodul |
