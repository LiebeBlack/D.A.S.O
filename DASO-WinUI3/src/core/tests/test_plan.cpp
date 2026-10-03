// Pruebas del planificador de lotes y del resolutor por biseccion.
#include <algorithm>
#include <string>
#include <vector>

#include "daso/plan.hpp"
#include "test_harness.hpp"

using namespace daso;

namespace {

// Catalogo sintetico con almacenamiento propio.
//
// IMPORTANTE: PackageEntry guarda string_view, asi que los nombres tienen que
// vivir mas que el catalogo. Devolver solo el vector de entradas dejaria las
// vistas colgando de los std::string temporales; a -O0 pasaria y a -O2 no.
// Ver la nota equivalente en test_filter.cpp.
struct FakeCatalog {
  std::vector<std::string> storage;
  std::vector<PackageEntry> entries;

  operator std::span<const PackageEntry>() const noexcept { return entries; }
  const PackageEntry& operator[](std::size_t i) const noexcept { return entries[i]; }
  std::size_t size() const noexcept { return entries.size(); }
};

FakeCatalog MakeCatalog(std::vector<std::string> names) {
  FakeCatalog cat;
  cat.storage = std::move(names);
  cat.entries.reserve(cat.storage.size());
  for (const std::string& n : cat.storage) {
    cat.entries.push_back(PackageEntry{n, n, n, "d", "d", RiskTier::Safe, Category::Other});
  }
  return cat;
}

std::vector<std::string> MakeNames(std::size_t count, std::size_t pad = 0) {
  std::vector<std::string> names;
  names.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    names.push_back("com.test.p" + std::to_string(i) + std::string(pad, 'x'));
  }
  return names;
}

std::vector<PlannedItem> SelectAll(std::size_t n, PackageAction action = PackageAction::Disable) {
  std::vector<PlannedItem> items;
  items.reserve(n);
  for (std::size_t i = 0; i < n; ++i) items.push_back(PlannedItem{i, action});
  return items;
}

}  // namespace

DASO_TEST(BuildPlan_UsesTheDefaultBatchLimit) {
  const auto catalog = MakeCatalog(MakeNames(144));
  const auto wanted = SelectAll(144);
  const PlanReport r = BuildPlan(wanted, catalog, PlanLimits{});

  DASO_CHECK_EQ(r.batches.size(), std::size_t{3});   // 64 + 64 + 16
  DASO_CHECK_EQ(r.batches[0].Size(), std::size_t{64});
  DASO_CHECK_EQ(r.batches[2].Size(), std::size_t{16});
  DASO_CHECK_EQ(r.planned_packages, std::size_t{144});
  DASO_CHECK_EQ(r.blocked_vital, std::size_t{0});
  DASO_CHECK_EQ(r.duplicates_dropped, std::size_t{0});
}

DASO_TEST(BuildPlan_CollapsesOneAdbTrip) {
  // El punto del diseno: con presupuesto de longitud generoso, los 144
  // paquetes del catalogo caben en UNA sola llamada a adb. La version en Python
  // lanzaba un proceso por paquete.
  const auto catalog = MakeCatalog(MakeNames(144));
  const auto wanted = SelectAll(144);
  const PlanReport r = BuildPlan(wanted, catalog, PlanLimits{1000, 1'000'000});
  DASO_CHECK_EQ(r.batches.size(), std::size_t{1});
  DASO_CHECK_EQ(r.batches[0].Size(), std::size_t{144});
}

DASO_TEST(BuildPlan_DefaultCollapsesAdbTripsByAFactor) {
  // Con los limites por defecto: 3 viajes en vez de 144. Es la promesa de
  // rendimiento, medida sobre los 144 paquetes reales.
  const auto catalog = Catalog();
  std::vector<PlannedItem> wanted;
  for (std::size_t i = 0; i < catalog.size(); ++i) {
    wanted.push_back(PlannedItem{i, PackageAction::Disable});
  }
  const PlanReport r = BuildPlan(wanted, catalog, PlanLimits{});
  DASO_CHECK_EQ(r.planned_packages, catalog.size());
  DASO_CHECK(r.batches.size() <= 3);
  DASO_CHECK(r.batches.size() * 20 < catalog.size());
}

DASO_TEST(BuildPlan_SplitsOnPackageCount) {
  const auto catalog = MakeCatalog(MakeNames(100));
  const auto wanted = SelectAll(100);
  const PlanReport r = BuildPlan(wanted, catalog, PlanLimits{25, 1'000'000});
  DASO_CHECK_EQ(r.batches.size(), std::size_t{4});
  for (const auto& b : r.batches) DASO_CHECK_EQ(b.Size(), std::size_t{25});
}

DASO_TEST(BuildPlan_SplitsOnCommandLength) {
  // Nombres de 200 caracteres: el presupuesto de longitud debe mandar antes
  // que el de cantidad.
  const auto catalog = MakeCatalog(MakeNames(20, 200));
  const auto wanted = SelectAll(20);
  const PlanReport r = BuildPlan(wanted, catalog, PlanLimits{1000, 2000});
  DASO_CHECK(r.batches.size() > 1);
  for (const auto& b : r.batches) {
    DASO_CHECK(b.NameChars(catalog) <= 2000);
  }
}

DASO_TEST(BuildPlan_NeverMixesActionsInOneBatch) {
  // `pm disable-user` y `pm enable` son subcomandos distintos: mezclarlos
  // produciria un comando que no existe.
  const auto catalog = MakeCatalog(MakeNames(10));
  std::vector<PlannedItem> wanted;
  for (std::size_t i = 0; i < 10; ++i) {
    wanted.push_back(PlannedItem{i, i % 2 == 0 ? PackageAction::Disable
                                                : PackageAction::Enable});
  }
  const PlanReport r = BuildPlan(wanted, catalog, PlanLimits{1000, 1'000'000});
  DASO_CHECK_EQ(r.batches.size(), std::size_t{2});
  for (const auto& b : r.batches) {
    for (const auto& item : b.items) DASO_CHECK(item.action == b.action);
  }
}

DASO_TEST(BuildPlan_DropsDuplicates) {
  const auto catalog = MakeCatalog(MakeNames(5));
  std::vector<PlannedItem> wanted = SelectAll(5);
  wanted.push_back(PlannedItem{2, PackageAction::Disable});  // repetido
  wanted.push_back(PlannedItem{0, PackageAction::Disable});  // repetido

  const PlanReport r = BuildPlan(wanted, catalog, PlanLimits{});
  DASO_CHECK_EQ(r.duplicates_dropped, std::size_t{2});
  DASO_CHECK_EQ(r.planned_packages, std::size_t{5});
  DASO_CHECK_EQ(r.batches[0].Size(), std::size_t{5});
}

DASO_TEST(BuildPlan_BlocksVitalPackages) {
  // Este es el punto de la promesa "modo seguro": un paquete vital que llegue
  // por la lista del usuario queda fuera del plan y queda contabilizado.
  auto catalog = MakeCatalog({"com.test.ok", "com.android.systemui", "com.test.ok2"});
  const std::vector<PlannedItem> wanted = {
      PlannedItem{0, PackageAction::Disable},
      PlannedItem{1, PackageAction::Disable},
      PlannedItem{2, PackageAction::Disable},
  };
  const PlanReport r = BuildPlan(wanted, catalog, PlanLimits{});
  DASO_CHECK_EQ(r.blocked_vital, std::size_t{1});
  DASO_CHECK_EQ(r.planned_packages, std::size_t{2});
  for (const auto& b : r.batches) {
    for (const auto& item : b.items) {
      DASO_CHECK(catalog[item.index].name != "com.android.systemui");
    }
  }
}

DASO_TEST(BuildPlan_IgnoresOutOfRangeIndices) {
  const auto catalog = MakeCatalog(MakeNames(3));
  const std::vector<PlannedItem> wanted = {
      PlannedItem{0, PackageAction::Disable},
      PlannedItem{999, PackageAction::Disable},
  };
  const PlanReport r = BuildPlan(wanted, catalog, PlanLimits{});
  DASO_CHECK_EQ(r.planned_packages, std::size_t{1});
}

DASO_TEST(BuildPlan_EmptyRequest) {
  const auto catalog = MakeCatalog(MakeNames(5));
  const PlanReport r = BuildPlan({}, catalog, PlanLimits{});
  DASO_CHECK(r.batches.empty());
  DASO_CHECK_EQ(r.planned_packages, std::size_t{0});
}

DASO_TEST(BuildPlan_RealCatalogSafeMode) {
  // Escenario real: modo seguro selecciona todo lo RiskTier::Safe.
  const auto catalog = Catalog();
  std::vector<PlannedItem> wanted;
  for (std::size_t i = 0; i < catalog.size(); ++i) {
    if (catalog[i].tier == RiskTier::Safe) {
      wanted.push_back(PlannedItem{i, PackageAction::Disable});
    }
  }
  const PlanReport r = BuildPlan(wanted, catalog, PlanLimits{});
  DASO_CHECK_EQ(r.blocked_vital, std::size_t{0});
  DASO_CHECK_EQ(r.planned_packages, wanted.size());
  // El salto de rendimiento se comprueba sobre los datos reales: el lote mas
  // grande debe ser de 64 (el limite por defecto) y no de 95 paquetes.
  std::size_t largest = 0;
  for (const auto& b : r.batches) largest = std::max(largest, b.Size());
  DASO_CHECK_EQ(largest, std::size_t{64});
  DASO_CHECK(r.batches.size() < 4);
}

// ---------------------------------------------------------------------------
// Biseccion
// ---------------------------------------------------------------------------

namespace {

Batch MakeBatch(std::size_t n, PackageAction action = PackageAction::Disable) {
  Batch b;
  b.action = action;
  for (std::size_t i = 0; i < n; ++i) b.items.push_back(PlannedItem{i, action});
  return b;
}

}  // namespace

DASO_TEST(Bisect_ImmediateSuccessNeedsOneRun) {
  BatchResolver r(MakeBatch(64));
  int runs = 0;
  while (auto cmd = r.Next()) {
    ++runs;
    DASO_CHECK_EQ(cmd->Size(), std::size_t{64});
    r.Report(BatchOutcome::Succeeded);
  }
  DASO_CHECK_EQ(runs, 1);
  DASO_CHECK_EQ(r.SucceededCount(), std::size_t{64});
  DASO_CHECK(!r.BudgetExhausted());
}

DASO_TEST(Bisect_FindsSingleCulprit) {
  // Lote de 8, solo el indice 3 es malo. pm no dice cual, asi que se biseca.
  BatchResolver r(MakeBatch(8));
  int runs = 0;
  while (auto cmd = r.Next()) {
    ++runs;
    const bool contains3 = [&] {
      for (const auto& i : cmd->items) {
        if (i.index == 3) return true;
      }
      return false;
    }();
    r.Report(contains3 ? BatchOutcome::Failed : BatchOutcome::Succeeded);
  }

  DASO_CHECK(r.Done());
  const auto outcomes = r.Outcomes();
  DASO_CHECK_EQ(outcomes.size(), std::size_t{8});
  for (std::size_t i = 0; i < outcomes.size(); ++i) {
    if (i == 3) {
      DASO_CHECK(outcomes[i] == PackageOutcome::Failed);
    } else {
      DASO_CHECK(outcomes[i] == PackageOutcome::Ok);
    }
  }
  // Peor caso teorico de una bisección completa sobre 8: 1 + 2 + 4 + ... = 15
  // llamadas. El recorrido por la izquierda debe acercarse a eso.
  DASO_CHECK(runs <= 15);
  DASO_CHECK(runs >= 7);
}

DASO_TEST(Bisect_AllCulprits) {
  // Si todos fallan, la bisección agota la profundidad y no debe mentir:
  // acaba sin affirmar que ninguno funciono.
  BatchResolver r(MakeBatch(4), BisectPolicy{6, 0});
  while (auto cmd = r.Next()) r.Report(BatchOutcome::Failed);

  for (PackageOutcome o : r.Outcomes()) {
    DASO_CHECK(o != PackageOutcome::Ok);
    DASO_CHECK(o != PackageOutcome::Pending);
  }
}

DASO_TEST(Bisect_RespectsDepthLimit) {
  BatchResolver r(MakeBatch(256), BisectPolicy{2, 0});  // solo 4 sub-lotes
  int runs = 0;
  while (auto cmd = r.Next()) {
    ++runs;
    r.Report(BatchOutcome::Failed);
  }
  DASO_CHECK(r.BudgetExhausted());
  // 1 raiz + 2 + 4 = 7 llamadas como mucho con profundidad 2.
  DASO_CHECK(runs <= 7);
  std::size_t unresolved = 0;
  for (PackageOutcome o : r.Outcomes()) {
    if (o == PackageOutcome::Skipped) ++unresolved;
  }
  DASO_CHECK_EQ(unresolved, std::size_t{256});
}

DASO_TEST(Bisect_RespectsRunBudget) {
  // Presupuesto de 2 reintentos: raiz + una particion y nada mas.
  BatchResolver r(MakeBatch(16), BisectPolicy{10, 2});
  int runs = 0;
  while (auto cmd = r.Next()) {
    ++runs;
    r.Report(BatchOutcome::Failed);
  }
  DASO_CHECK(r.BudgetExhausted());
  DASO_CHECK(runs <= 3);
}

DASO_TEST(Bisect_EmptyBatch) {
  BatchResolver r(MakeBatch(0));
  DASO_CHECK(r.Done());
  DASO_CHECK(!r.Next().has_value());
  DASO_CHECK(r.Outcomes().empty());
}

DASO_TEST(Bisect_ReportWithoutNextIsIgnored) {
  BatchResolver r(MakeBatch(4));
  r.Report(BatchOutcome::Succeeded);  // sin Next(): no debe hacer nada
  auto cmd = r.Next();
  DASO_CHECK(cmd.has_value());
  DASO_CHECK_EQ(cmd->Size(), std::size_t{4});
}

DASO_TEST(Bisect_DoubleNextReturnsNullopt) {
  BatchResolver r(MakeBatch(4));
  auto a = r.Next();
  auto b = r.Next();
  DASO_CHECK(a.has_value());
  DASO_CHECK(!b.has_value());  // ya hay una llamada en vuelo
  r.Report(BatchOutcome::Succeeded);
  DASO_CHECK(!r.Next().has_value());
}

DASO_TEST(Bisect_WorstCaseIsBounded) {
  // Con 144 paquetes, un culpable al final: 1 + 2 + 4 + ... + 128 = 255
  // llamadas, el clasico 2N-1. Se comprueba que el peor caso real lo respeta.
  BatchResolver r(MakeBatch(144), BisectPolicy{64, 0});
  int runs = 0;
  while (auto cmd = r.Next()) {
    ++runs;
    bool contains_culprit = false;
    for (const auto& i : cmd->items) {
      if (i.index == 143) contains_culprit = true;
    }
    r.Report(contains_culprit ? BatchOutcome::Failed : BatchOutcome::Succeeded);
    if (runs > 400) break;  // guarda: si el algoritmo se descontrola, paramos
  }
  DASO_CHECK(runs <= 287);
  DASO_CHECK(r.Outcomes()[143] == PackageOutcome::Failed);
  DASO_CHECK_EQ(r.SucceededCount(), std::size_t{143});
}
