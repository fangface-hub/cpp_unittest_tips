[English](README.md) | [日本語](README_ja.md) | [Deutsch](README_de.md) | Français | [Italiano](README_it.md) | [한국어](README_ko.md) | [简体中文](README_zh.md)

# Techniques de conception de stubs en C++

Savoir-faire de conception de stubs pour atteindre 100 % de couverture de tests unitaires en C++.
Ce dépôt est un exemple concret de la technique, appliquée à un SUT qui utilise
[CppDatasetUtil](https://github.com/fangface-hub/CppDatasetUtil).

- Aucune modification du code testé (SUT)
- Aucun `virtual` nécessaire
- Le code de test n'écrit que le nom `SUT` ; seul le stub connaît la vraie classe
- Chaque méthode peut être basculée individuellement entre réelle et stubbée

## Code testé

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
`Calc` a trois chemins (clé absente / entier / non entier) et `Process` en a
deux (normal / exception). `Process` appelle `Calc` en interne.

## Stub de test

Un seul en-tête, `test/SUTStub.h`, fait tout. Les commentaires du code
expliquent chaque étape ; les points à retenir sont ci-dessous.

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
- **Renommer.** `#define Calc CalcStub` / `#define Process ProcessStub`
  renomment les méthodes du SUT avant l'inclusion de `SUT.h`, ce qui libère les
  noms d'origine pour la classe stub. Incluez tous les en-têtes qui ne doivent
  pas être renommés (en-têtes standard et de bibliothèques) **avant** ces
  macros, afin de ne pas casser les membres homonymes de la bibliothèque.
- **Compiler le vrai SUT avec le même en-tête.** Le build de production
  n'utilise jamais `SUTStub.h`. Dans le build de test, `test/SUTStub.cpp`
  l'inclut, annule le `#define SUT SUTStub` final, réactive le renommage et
  inclut le `SUT.cpp` non modifié, qui définit ainsi `SUT::CalcStub` /
  `SUT::ProcessStub`. L'appel `Calc(data)` dans `Process` devient lui aussi
  `CalcStub(data)`, de sorte que **les auto-appels du SUT atteignent toujours
  le vrai code**.
- **Classe stub.** `SUTStub` dérive de `SUT` et définit les noms d'origine
  `Calc` / `Process`. Si `useReal` est vrai, elle appelle le vrai
  `SUT::CalcStub` etc. ; sinon elle renvoie la valeur de stub configurée.
- **Types de retour.** Pour en prendre en charge d'autres, ajoutez un membre à
  `StubValue` (l'union) et une valeur à `StubTag`. Une seule `configMap` gère
  des méthodes ayant des types de retour différents.
- **`#define SUT SUTStub`** à la fin est l'élément clé. **Il remplace `SUT` par
  `SUTStub` dans le vrai code, de sorte que seul `SUTStub` connaît le vrai
  `SUT`.** Le code de test écrit simplement `SUT sut;` et obtient le stub.

## Code de test

**test/test_sut.cpp** (extrait ; il n'inclut pas `#include "SUTStub.h"`)

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
## Compilation

```makefile
# SUT: SUTStub.cpp compiles the real SUT.cpp with the stub header; only this object is instrumented
$(CXX) -std=c++17 $(CPPFLAGS) --coverage --include SUTStub.h -c test/SUTStub.cpp -o build/SUTStub.o

# Test: the test itself and the library are not measured
$(CXX) -std=c++17 $(CPPFLAGS) --include SUTStub.h -c test/test_sut.cpp -o build/test_sut.o
$(CXX) --coverage build/test_sut.o build/SUTStub.o libcppdatasetutil.a -o build/test_sut
```
Consultez le [Makefile](Makefile) pour la compilation réelle.

```sh
git submodule update --init
make test       # build and run
make coverage   # gcov; fails unless line coverage of SUT.cpp is 100%
```
Nécessite g++ (C++17), make et gcov. CppDatasetUtil est un sous-module dans
`third_party/CppDatasetUtil`.

## Comportement des appels internes

| Appel | Résultat |
| :-- | :-- |
| `stub.Calc(five)`, Calc réglé sur réel | `SUT::CalcStub(five)` → 10 |
| `stub.Calc(five)`, Calc réglé sur stub | la valeur du stub |
| `stub.Process(five)`, Process réglé sur stub | la valeur du stub (100) |
| `stub.Process(five)`, Process réglé sur réel | `SUT::ProcessStub(five)` → en interne `CalcStub` (réel) → 11 |

- Les auto-appels du SUT (`Process` → `Calc`) passent toujours par le vrai code
  (`CalcStub`)
- Seuls les appels faits directement depuis le code de test sont stubbés
- Aucun `virtual`

## Conseils pour atteindre 100 % de couverture

- **Couvrez chaque chemin avec des tests qui appellent la vraie méthode.** Un
  stub ne remplace que le point d'entrée vu par le test. Couvrez les branches
  internes du SUT en appelant la vraie méthode avec des entrées différentes.
  Ici, les trois chemins de `Calc` sont couverts avec trois `Dataset` et le
  vrai `Calc`, et la branche d'exception de `Process` avec le vrai `Process` et
  un `x` non entier.
- **Ne mesurez que le SUT.** Ajoutez `--coverage` uniquement lors de la
  compilation de `SUT.cpp` et de l'édition de liens. Inclure le code de test ou
  la bibliothèque dilue le chiffre (`make coverage` ne juge que `SUT.cpp`).
- **Jugez sur la couverture de lignes.** La couverture de branches de gcov
  inclut des éléments internes générés par le compilateur (exceptions,
  `std::string`) que les tests ne peuvent pas atteindre. Ici la couverture de
  branches est de 68,75 % bien que chaque branche de `Dataset` soit couverte ;
  `make coverage` l'affiche à titre indicatif seulement.
- **Pour tester directement des méthodes `private`**, encadrez le
  `#include "SUT.h"` de `SUTStub.h` par `#define private public` /
  `#undef private` (après l'inclusion des en-têtes standard). Ce SUT n'a pas de
  membres privés ; ce n'est donc pas utilisé ici.

## Fichiers

| Fichier | Rôle |
| :-- | :-- |
| `src/SUT.h`, `src/SUT.cpp` | Code testé |
| `test/SUTStub.h` | Renommage, classe stub et `#define SUT SUTStub` |
| `test/SUTStub.cpp` | Compile le vrai `SUT.cpp` avec l'en-tête stub |
| `test/test_sut.cpp` | Code de test |
| `Makefile` | Compilation et contrôle de la couverture |
| `third_party/CppDatasetUtil` | Sous-module |
