// Pruebas de la logica de manifiesto y reversion.
//
// BuildManifest y PlanFromManifest no tocan el disco ni la API de Windows: son
// funciones puras sobre el catalogo. Lo que se prueba aqui es exactamente lo
// que decide si "Revertir ultimo cambio" hace lo correcto.
#include <string>
#include <vector>

#include "daso/manifest_store.hpp"
#include "test_harness.hpp"

using namespace daso;
using namespace daso::win;

namespace {

constexpr std::int64_t kNow = 1'767'225'600;  // 2026-01-01T00:00:00Z

}  // namespace

DASO_TEST(ManifestStore_BuildsEntriesWithObservedState) {
  const auto catalog = Catalog();
  std::vector<PlannedItem> planned = {
      PlannedItem{0, PackageAction::Disable},
      PlannedItem{1, PackageAction::Disable},
  };
  std::vector<PackageState> before(catalog.size(), PackageState::Enabled);
  before[1] = PackageState::Absent;  // este no existe en el ROM

  const Manifest m = BuildManifest("SER123", PackageAction::Disable, kNow, catalog,
                                   planned, before);

  DASO_CHECK_EQ(m.serial, std::string("SER123"));
  DASO_CHECK_EQ(m.timestamp_utc, FormatIso8601Utc(kNow));
  DASO_CHECK(m.action == PackageAction::Disable);
  DASO_CHECK_EQ(m.entries.size(), std::size_t{2});
  DASO_CHECK_EQ(m.entries[0].package, std::string(catalog[0].name));
  DASO_CHECK(m.entries[0].before == PackageState::Enabled);
  DASO_CHECK(m.entries[0].after == PackageState::Disabled);
  // Un paquete ausente no se marca como aplicado: no hubo nada que revertir.
  DASO_CHECK(m.entries[1].after == PackageState::Absent);
}

DASO_TEST(ManifestStore_SkipsOutOfRangeIndices) {
  const auto catalog = Catalog();
  std::vector<PlannedItem> planned = {
      PlannedItem{0, PackageAction::Disable},
      PlannedItem{99999, PackageAction::Disable},
  };
  std::vector<PackageState> before(catalog.size(), PackageState::Enabled);
  const Manifest m =
      BuildManifest("S", PackageAction::Disable, kNow, catalog, planned, before);
  DASO_CHECK_EQ(m.entries.size(), std::size_t{1});
}

DASO_TEST(ManifestStore_RoundTripsThroughTheJsonCodec) {
  const auto catalog = Catalog();
  std::vector<PlannedItem> planned = {PlannedItem{0, PackageAction::Disable},
                                      PlannedItem{5, PackageAction::Disable}};
  std::vector<PackageState> before(catalog.size(), PackageState::Enabled);

  const Manifest m =
      BuildManifest("SER123", PackageAction::Disable, kNow, catalog, planned, before);
  const auto parsed = ParseManifest(SerializeManifest(m));
  DASO_CHECK(parsed.has_value());
  if (parsed) DASO_CHECK_EQ(parsed->entries.size(), m.entries.size());
}

DASO_TEST(ManifestStore_UndoOfDisableBecomesEnable) {
  // El caso central: se deshabilito, y deshacer tiene que HABILITAR.
  const auto catalog = Catalog();
  std::vector<PlannedItem> applied = {PlannedItem{0, PackageAction::Disable},
                                      PlannedItem{1, PackageAction::Disable}};
  std::vector<PackageState> before(catalog.size(), PackageState::Enabled);
  before[1] = PackageState::Absent;  // el indice 1 no existe en el ROM

  const Manifest m =
      BuildManifest("SER123", PackageAction::Disable, kNow, catalog, applied, before);
  const auto planned = PlanFromManifest(m, catalog);

  DASO_CHECK_EQ(planned.size(), std::size_t{1});   // el ausente se descarta
  DASO_CHECK(planned[0].action == PackageAction::Enable);
  DASO_CHECK_EQ(planned[0].index, std::size_t{0});
}

DASO_TEST(ManifestStore_UndoOfEnableBecomesDisable) {
  const auto catalog = Catalog();
  std::vector<PlannedItem> applied = {PlannedItem{0, PackageAction::Enable}};
  std::vector<PackageState> before(catalog.size(), PackageState::Disabled);

  const Manifest m =
      BuildManifest("SER123", PackageAction::Enable, kNow, catalog, applied, before);
  const auto planned = PlanFromManifest(m, catalog);
  DASO_CHECK_EQ(planned.size(), std::size_t{1});
  DASO_CHECK(planned[0].action == PackageAction::Disable);
}

DASO_TEST(ManifestStore_UndoIgnoresPackagesNotInCatalog) {
  Manifest m;
  m.serial = "SER123";
  m.timestamp_utc = FormatIso8601Utc(kNow);
  m.action = PackageAction::Disable;
  m.entries.push_back({"com.paquete.que.no.esta.en.el.catalogo", PackageState::Enabled,
                       PackageState::Disabled});
  const auto planned = PlanFromManifest(m, Catalog());
  DASO_CHECK(planned.empty());
}

DASO_TEST(ManifestStore_UndoPlanSurvivesTheVitalGuard) {
  // Revertir NO debe saltarse la guarda de nivel vital: es el planificador el
  // que la aplica, no la UI. Se comprueba que un manifiesto manipulado con un
    // paquete vital no llega a ejecutarse.
  Manifest m;
  m.action = PackageAction::Disable;
  m.entries.push_back({"com.android.systemui", PackageState::Enabled,
                       PackageState::Disabled});
  const auto planned = PlanFromManifest(m, Catalog());

  std::vector<PlannedItem> as_span(planned.begin(), planned.end());
  const PlanReport report = BuildPlan(as_span, Catalog(), PlanLimits{});
  DASO_CHECK_EQ(report.blocked_vital, std::size_t{0});  // no esta en el catalogo
  DASO_CHECK_EQ(report.planned_packages, std::size_t{0});
}

DASO_TEST(ManifestStore_FileNameIsScopedPerDeviceAndTime) {
  const std::string a = ManifestNameFor("SER123", kNow);
  const std::string b = ManifestNameFor("OTRO456", kNow);
  DASO_CHECK_EQ(a, std::string("SER123_20260101-000000.json"));
  DASO_CHECK_EQ(b, std::string("OTRO456_20260101-000000.json"));
  DASO_CHECK(a != b);
}
