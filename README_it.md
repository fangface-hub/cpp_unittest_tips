[English](README.md) | [日本語](README_ja.md) | [Deutsch](README_de.md) | [Français](README_fr.md) | Italiano | [한국어](README_ko.md) | [简体中文](README_zh.md)

# Tecniche di progettazione di stub in C++

Know-how sulla progettazione di stub per raggiungere il 100% di copertura dei test unitari in C++.
Questo repository è un esempio concreto della tecnica, applicata a un SUT che
usa [CppDatasetUtil](https://github.com/fangface-hub/CppDatasetUtil).

- Nessuna modifica al codice sotto test (SUT)
- Nessun `virtual` necessario
- Il codice di test scrive sempre e solo il nome `SUT`; solo lo stub conosce la classe reale
- Ogni metodo può essere commutato singolarmente tra reale e stub

## Codice sotto test

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
`Calc` ha tre percorsi (chiave assente / intero / non intero) e `Process` ne
ha due (normale / eccezione). `Process` chiama `Calc` internamente.

## Stub di test

Un solo header, `test/SUTStub.h`, fa tutto. I commenti nel codice spiegano ogni
passaggio; i punti da ricordare sono qui sotto.

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
- **Rinominare.** `#define Calc CalcStub` / `#define Process ProcessStub`
  rinominano i metodi del SUT prima dell'inclusione di `SUT.h`, liberando i nomi
  originali per la classe stub. Includete **prima** di queste macro tutti gli
  header che non devono essere rinominati (header standard e di librerie), per
  non rompere i membri con lo stesso nome all'interno della libreria.
- **Compilare il SUT con lo stesso header.** `SUT.cpp` viene compilato con
  `-DSUT_UNDER_TEST --include SUTStub.h`; ha effetto solo la ridenominazione e
  vengono definiti `SUT::CalcStub` / `SUT::ProcessStub`. Anche la chiamata
  `Calc(data)` dentro `Process` diventa `CalcStub(data)`, perciò **le
  auto-chiamate del SUT raggiungono sempre il codice reale**.
- **Classe stub.** `SUTStub` deriva da `SUT` e definisce i nomi originali
  `Calc` / `Process`. Se `useReal` è vero chiama il reale `SUT::CalcStub` ecc.;
  altrimenti restituisce il valore di stub configurato.
- **Tipi di ritorno.** Per supportarne altri, aggiungete un membro a
  `StubValue` (l'union) e un valore a `StubTag`. Una sola `configMap` gestisce
  metodi con tipi di ritorno diversi.
- Il **`#define SUT SUTStub`** finale è il punto chiave. **Sostituisce `SUT`
  con `SUTStub` nel codice reale, quindi solo `SUTStub` conosce il vero
  `SUT`.** Il codice di test scrive semplicemente `SUT sut;` e ottiene lo stub.

## Codice di test

**test/test_sut.cpp** (estratto; non contiene `#include "SUTStub.h"`)

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
Per la build effettiva vedere il [Makefile](Makefile).

```sh
git submodule update --init
make test       # build and run
make coverage   # gcov; fails unless line coverage of SUT.cpp is 100%
```
Richiede g++ (C++17), make e gcov. CppDatasetUtil è un sottomodulo in
`third_party/CppDatasetUtil`.

## Comportamento delle chiamate interne

| Chiamata | Risultato |
| :-- | :-- |
| `stub.Calc(five)`, Calc impostato su reale | `SUT::CalcStub(five)` → 10 |
| `stub.Calc(five)`, Calc impostato su stub | il valore dello stub |
| `stub.Process(five)`, Process impostato su stub | il valore dello stub (100) |
| `stub.Process(five)`, Process impostato su reale | `SUT::ProcessStub(five)` → internamente `CalcStub` (reale) → 11 |

- Le auto-chiamate del SUT (`Process` → `Calc`) passano sempre dal codice reale
  (`CalcStub`)
- Vengono sostituite con stub solo le chiamate fatte direttamente dal codice di
  test
- Nessun `virtual`

## Consigli per raggiungere il 100% di copertura

- **Coprite ogni percorso con test che chiamano il metodo reale.** Uno stub
  sostituisce solo il punto di ingresso visto dal test. Coprite i rami interni
  del SUT chiamando il metodo reale con input diversi. Qui i tre percorsi di
  `Calc` sono coperti con tre `Dataset` e `Calc` reale, e il ramo di eccezione
  di `Process` con `Process` reale e una `x` non intera.
- **Misurate solo il SUT.** Aggiungete `--coverage` solo nella compilazione di
  `SUT.cpp` e nel link. Includere il codice di test o la libreria diluisce il
  valore (`make coverage` valuta solo `SUT.cpp`).
- **Giudicate in base alla copertura delle righe.** La copertura dei rami di
  gcov include parti interne generate dal compilatore (eccezioni, `std::string`)
  che i test non possono raggiungere. Qui la copertura dei rami è 68,75%
  sebbene ogni ramo di `Dataset` sia coperto; `make coverage` la stampa solo a
  titolo informativo.
- **Per testare direttamente metodi `private`**, racchiudete il
  `#include "SUT.h"` in `SUTStub.h` tra `#define private public` /
  `#undef private` (dopo aver incluso gli header standard). Questo SUT non ha
  membri privati, quindi qui non viene usato.

## File

| File | Ruolo |
| :-- | :-- |
| `src/SUT.h`, `src/SUT.cpp` | Codice sotto test |
| `test/SUTStub.h` | Classe stub e `#define SUT SUTStub` |
| `test/test_sut.cpp` | Codice di test |
| `Makefile` | Build e controllo della copertura |
| `third_party/CppDatasetUtil` | Sottomodulo |
