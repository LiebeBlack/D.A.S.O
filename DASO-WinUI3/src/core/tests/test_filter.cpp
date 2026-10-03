// Pruebas del filtro, el log circular y la cancelacion.
#include <string>
#include <string_view>
#include <vector>

#include "daso/filter.hpp"
#include "daso/log_buffer.hpp"
#include "test_harness.hpp"

using namespace daso;

namespace {

// Ver nota en test_manifest.cpp: los literales con escape no pueden aparecer
// dentro de un DASO_CHECK porque #cond no los escapa.
constexpr char kNewline = '\n';
constexpr char kNull = '\0';
constexpr std::string_view kDescEs = "desc es";
constexpr std::string_view kDescEn = "desc en";

// IMPORTANTE: los parametros son string_view, NO string.
//
// PackageEntry guarda string_view. Si esta funcion recibiera std::string por
// valor y hiciera std::move al string_view, las vistas quedarian colgando de
// parametros ya destruidos: a -O0 el hueco de pila sobrevive y las pruebas
// pasan; a -O2 el compilador reutiliza ese hueco y las pruebas fallan sin
// motivo aparente. Los argumentos de las pruebas son literales, que tienen
// duracion estatica, asi que las vistas son validas mientras dure el catalogo.
PackageEntry Make(std::string_view name, std::string_view es, std::string_view en,
                  RiskTier tier = RiskTier::Safe) {
  return PackageEntry{name, es, en, kDescEs, kDescEn, tier, Category::Themes};
}

}  // namespace

DASO_TEST(Filter_EmptyQueryMatchesEverything) {
  const std::vector<PackageEntry> cat = {Make("a.one", "Uno", "One"),
                                         Make("b.two", "Dos", "Two")};
  FilterRequest q;
  DASO_CHECK_EQ(FilterCatalog(cat, q).size(), std::size_t{2});
}

DASO_TEST(Filter_ExactPackageNameRanksFirst) {
  const std::vector<PackageEntry> cat = {
      Make("com.android.theme.icon.x", "Icono X", "Icon X"),
      Make("com.example.rubik", "Rubik", "Rubik"),
  };
  FilterRequest q;
  q.text = "com.example.rubik";
  const auto hits = FilterCatalog(cat, q);
  DASO_CHECK_EQ(hits.size(), std::size_t{1});
  DASO_CHECK_EQ(hits[0], std::size_t{1});
}

DASO_TEST(Filter_ExactBeatsPrefixBeatsSubstring) {
  const std::string name = "com.a.rubik.font";
  const int exact = ScoreMatch("com.a.rubik.font", name, "d", "d", "d", "d");
  const int prefix = ScoreMatch("com.a.rubik", name, "d", "d", "d", "d");
  const int sub = ScoreMatch("rubik", name, "d", "d", "d", "d");
  const int miss = ScoreMatch("nada", name, "d", "d", "d", "d");
  DASO_CHECK(exact > prefix);
  DASO_CHECK(prefix > sub);
  DASO_CHECK(sub > miss);
  DASO_CHECK_EQ(miss, 0);
}

DASO_TEST(Filter_MatchesDisplayNameInEitherLanguage) {
  const std::vector<PackageEntry> cat = {Make("com.x.y", "Tipografía Roboto", "Roboto Font")};
  FilterRequest q;

  q.text = "roboto";
  DASO_CHECK_EQ(FilterCatalog(cat, q).size(), std::size_t{1});

  // Buscar en el idioma que no se esta mostrando tambien debe funcionar.
  q.text = "tipografía";
  DASO_CHECK_EQ(FilterCatalog(cat, q).size(), std::size_t{1});
}

DASO_TEST(Filter_IsCaseInsensitive) {
  const std::vector<PackageEntry> cat = {Make("com.x.y", "Rubik", "Rubik")};
  FilterRequest q;
  q.text = "RUBIK";
  DASO_CHECK_EQ(FilterCatalog(cat, q).size(), std::size_t{1});
  q.text = "rUbIk";
  DASO_CHECK_EQ(FilterCatalog(cat, q).size(), std::size_t{1});
}

DASO_TEST(Filter_MatchesDescription) {
  PackageEntry e = Make("com.x.y", "Reloj metro", "Metro clock");
  e.desc_es = "Un diseño de reloj para la barra de estado.";
  e.desc_en = "One clock design for the status bar.";
  const std::vector<PackageEntry> cat = {e};

  FilterRequest q;
  q.text = "barra de estado";
  DASO_CHECK_EQ(FilterCatalog(cat, q).size(), std::size_t{1});

  q.text = "status bar";
  DASO_CHECK_EQ(FilterCatalog(cat, q).size(), std::size_t{1});

  // El nombre tiene mas peso: debe ganar aunque ambos coincidan.
  q.text = "reloj";
  const auto hits = FilterCatalog(cat, q);
  DASO_CHECK_EQ(hits.size(), std::size_t{1});
}

DASO_TEST(Filter_NoMatchReturnsEmpty) {
  const std::vector<PackageEntry> cat = {Make("com.x.y", "Uno", "One")};
  FilterRequest q;
  q.text = "esto-no-existe-jamas";
  DASO_CHECK(FilterCatalog(cat, q).empty());
}

DASO_TEST(Filter_TierAndCategoryNarrow) {
  const std::vector<PackageEntry> cat = {
      Make("com.a", "A", "A", RiskTier::Safe),
      Make("com.b", "B", "B", RiskTier::Caution),
      Make("com.c", "C", "C", RiskTier::Safe),
  };
  FilterRequest q;
  q.tier = RiskTier::Caution;
  const auto hits = FilterCatalog(cat, q);
  DASO_CHECK_EQ(hits.size(), std::size_t{1});
  DASO_CHECK_EQ(hits[0], std::size_t{1});
}

DASO_TEST(Filter_SelectionFilter) {
  const std::vector<PackageEntry> cat = {Make("com.a", "A", "A"), Make("com.b", "B", "B"),
                                         Make("com.c", "C", "C")};
  Selection sel(3);
  sel.Set(0, true);
  sel.Set(2, true);

  FilterRequest q;
  q.selection = SelectionFilter::OnlySelected;
  DASO_CHECK_EQ(FilterCatalog(cat, q, &sel).size(), std::size_t{2});

  q.selection = SelectionFilter::OnlyUnselected;
  const auto hits = FilterCatalog(cat, q, &sel);
  DASO_CHECK_EQ(hits.size(), std::size_t{1});
  DASO_CHECK_EQ(hits[0], std::size_t{1});

  q.selection = SelectionFilter::Ignore;
  DASO_CHECK_EQ(FilterCatalog(cat, q, &sel).size(), std::size_t{3});
}

DASO_TEST(Filter_IsStableForEqualScores) {
  const std::vector<PackageEntry> cat = {Make("com.a.1", "X", "X"), Make("com.b.2", "X", "X"),
                                         Make("com.c.3", "X", "X")};
  FilterRequest q;
  q.text = "X";
  const auto hits = FilterCatalog(cat, q);
  DASO_CHECK_EQ(hits.size(), std::size_t{3});
  // Empate por indice: el orden no puede cambiar entre pulsaciones.
  DASO_CHECK_EQ(hits[0], std::size_t{0});
  DASO_CHECK_EQ(hits[1], std::size_t{1});
  DASO_CHECK_EQ(hits[2], std::size_t{2});
}

DASO_TEST(Filter_RealCatalogRanksExactNameFirst) {
  FilterRequest q;
  q.text = "com.android.theme.font.rubik";
  const auto hits = FilterCatalog(Catalog(), q);
  DASO_CHECK(!hits.empty());
  DASO_CHECK_EQ(Catalog()[hits[0]].name, std::string_view("com.android.theme.font.rubik"));
}

DASO_TEST(Filter_RealCatalogFindsByDisplayName) {
  // "rubik" no aparece en ningun nombre de paquete salvo en jetbrainsmono, que
  // no lo contiene: tiene que encontrarlo por el nombre legible.
  FilterRequest q;
  q.text = "rubik";
  const auto hits = FilterCatalog(Catalog(), q);
  DASO_CHECK(!hits.empty());
  DASO_CHECK_EQ(Catalog()[hits[0]].name, std::string_view("com.android.theme.font.rubik"));
  DASO_CHECK(LocalizedName(Catalog()[hits[0]], true).find("Rubik") != std::string_view::npos);
}

DASO_TEST(Filter_RealCatalogNarrowsWithCategory) {
  FilterRequest q;
  q.text = "reloj";
  q.category = Category::SystemUi;
  const auto hits = FilterCatalog(Catalog(), q);
  for (std::size_t index : hits) {
    DASO_CHECK(Catalog()[index].category == Category::SystemUi);
  }
}

DASO_TEST(Filter_CountTiers) {
  const TierCount c = CountTiers(Catalog());
  DASO_CHECK_EQ(c.safe + c.caution + c.vital, CatalogSize());
  DASO_CHECK(c.safe > 0);
  DASO_CHECK(c.caution > 0);
  DASO_CHECK_EQ(c.vital, std::size_t{0});  // el catalogo no debe traer ninguno
}

// ---------------------------------------------------------------------------
// Log circular
// ---------------------------------------------------------------------------

DASO_TEST(Log_AppendsInOrder) {
  LogBuffer log(1024);
  log.AppendLine("primera");
  log.AppendLine("segunda");
  DASO_CHECK_EQ(log.Snapshot(), std::string("primera\nsegunda\n"));
  DASO_CHECK_EQ(log.LineCount(), std::size_t{2});
  DASO_CHECK_EQ(log.Size(), std::string("primera\nsegunda\n").size());
  DASO_CHECK(!log.Truncated());
}

DASO_TEST(Log_SplitsLinesAcrossChunks) {
  LogBuffer log(1024);
  log.Append("pri");
  log.Append("m");
  log.Append("era\n");
  DASO_CHECK_EQ(log.Snapshot(), std::string("primera\n"));
  DASO_CHECK_EQ(log.LineCount(), std::size_t{1});
}

DASO_TEST(Log_DropsOldestWhenFull) {
  const std::size_t cap = 128;
  LogBuffer log(cap);
  for (int i = 0; i < 100; ++i) {
    log.AppendLine("linea " + std::to_string(i));
  }
  DASO_CHECK_EQ(log.Size(), cap);
  DASO_CHECK(log.Truncated());
  DASO_CHECK_EQ(log.Capacity(), cap);

  const std::string snap = log.Snapshot();
  DASO_CHECK_EQ(snap.size(), cap);
  // Lo mas reciente tiene que estar: es un registro, no una arqueologia.
  DASO_CHECK(snap.find("linea 99") != std::string::npos);
  DASO_CHECK(snap.find("linea 0\n") == std::string::npos);
}

DASO_TEST(Log_HugeChunkReplacesEverything) {
  LogBuffer log(64);
  log.AppendLine("viejo");
  log.Append(std::string(200, 'z'));
  DASO_CHECK_EQ(log.Size(), std::size_t{64});
  DASO_CHECK_EQ(log.Snapshot(), std::string(64, 'z'));
  DASO_CHECK(log.Truncated());
}

DASO_TEST(Log_WrapAroundKeepsTextInOrder) {
  // Escritura que da varias vueltas al buffer: el orden circular debe seguir
  // siendo el cronologico.
  const std::size_t cap = 40;
  LogBuffer log(cap);
  for (int i = 0; i < 5; ++i) log.AppendLine("0123456789");
  DASO_CHECK_EQ(log.Size(), cap);

  // 5 lineas de 11 bytes = 55. Un buffer de 40 solo conserva los ultimos 40
  // bytes, que empiezan a mitad de la primera linea: por eso el resultado
  // arranca en "456789" y no en "0123456789".
  const std::string expected = "456789\n0123456789\n0123456789\n0123456789\n";
  DASO_CHECK_EQ(expected.size(), cap);
  DASO_CHECK_EQ(log.Snapshot(), expected);
  DASO_CHECK_EQ(log.Snapshot().back(), kNewline);  // el mas reciente al final
}

DASO_TEST(Log_ClearResets) {
  LogBuffer log(256);
  log.AppendLine("algo");
  log.Clear();
  DASO_CHECK_EQ(log.Size(), std::size_t{0});
  DASO_CHECK(log.Snapshot().empty());
  DASO_CHECK(!log.Truncated());
  DASO_CHECK_EQ(log.LineCount(), std::size_t{0});
}

DASO_TEST(Log_CopyOutMatchesSnapshot) {
  LogBuffer log(256);
  log.AppendLine("hola");
  std::string buffer(log.Size(), '\0');
  const std::size_t n = log.CopyOut(buffer.data());
  DASO_CHECK_EQ(n, log.Size());
  DASO_CHECK_EQ(buffer, log.Snapshot());
  DASO_CHECK_EQ(log.CopyOut(nullptr), log.Size());  // solo contar
}

DASO_TEST(Log_CountsTotalAppendedIndependently) {
  LogBuffer log(16);
  log.Append("hola");
  log.Append("mundo");
  DASO_CHECK_EQ(log.TotalAppended(), std::uint64_t{9});
}

DASO_TEST(Log_HandlesEmbeddedNulls) {
  // adb puede devolver bytes NUL en la salida; el buffer es de bytes, no de
  // texto, y no debe romper.
  LogBuffer log(64);
  log.Append(std::string_view("a\0b", 3));
  DASO_CHECK_EQ(log.Size(), std::size_t{3});
  const std::string snap = log.Snapshot();
  DASO_CHECK_EQ(snap.size(), std::size_t{3});
  DASO_CHECK_EQ(snap[1], kNull);
}
