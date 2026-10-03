// Pruebas del catalogo: unicidad, metadatos completos, guardas vitales y busqueda.
#include <algorithm>
#include <set>
#include <string>

#include "daso/catalog.hpp"
#include "test_harness.hpp"

using namespace daso;

namespace {

// El batch lista 278 entradas para estos mismos paquetes. El numero sale de
// los datos reales del repositorio, no de la documentacion, que dice 145.
constexpr std::size_t kExpectedPackages = 144;

}  // namespace

DASO_TEST(Catalog_HasExpectedSize) {
  DASO_CHECK_EQ(CatalogSize(), kExpectedPackages);
  DASO_CHECK(Catalog().size() == CatalogSize());
}

DASO_TEST(Catalog_HasNoDuplicates) {
  std::set<std::string_view> seen;
  for (const auto& e : Catalog()) {
    DASO_CHECK(seen.insert(e.name).second);
  }
  DASO_CHECK_EQ(seen.size(), CatalogSize());
}

DASO_TEST(Catalog_EveryEntryIsFullyDescribed) {
  for (const auto& e : Catalog()) {
    DASO_CHECK(!e.name.empty());
    DASO_CHECK(!e.display_es.empty());
    DASO_CHECK(!e.display_en.empty());
    DASO_CHECK(!e.desc_es.empty());
    DASO_CHECK(!e.desc_en.empty());
    DASO_CHECK(e.category != Category::Count);
  }
}

DASO_TEST(Catalog_NamesAreAscii) {
  for (const auto& e : Catalog()) {
    for (const char c : e.name) {
      const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                      (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
      DASO_CHECK(ok);
      if (!ok) break;
    }
  }
}

DASO_TEST(Catalog_HasSpanishAndEnglishContent) {
  // Ningun texto debe quedarse en el idioma del otro por accidente: si las
  // cadenas son identicas y contienen asciiUtil, el generador se equivoco.
  std::size_t identical = 0;
  for (const auto& e : Catalog()) {
    if (e.display_es == e.display_en && e.desc_es == e.desc_en) ++identical;
  }
  // Los nombres proprios (Rubik, JetBrains Mono) si son iguales; el resto no.
  DASO_CHECK(identical < CatalogSize() / 2);
}

DASO_TEST(Catalog_FindIndexRoundTrips) {
  for (std::size_t i = 0; i < CatalogSize(); ++i) {
    DASO_CHECK_EQ(FindIndex(Catalog()[i].name), i);
  }
  DASO_CHECK_EQ(FindIndex("com.example.no.existe"), CatalogSize());
  DASO_CHECK_EQ(FindIndex(""), CatalogSize());
}

DASO_TEST(Catalog_SelectionBitset) {
  Selection sel(CatalogSize());
  DASO_CHECK_EQ(sel.Count(), std::size_t{0});

  sel.Set(0, true);
  sel.Set(63, true);   // ultimo bit del primer palabra
  sel.Set(64, true);   // primer bit del segundo
  DASO_CHECK_EQ(sel.Count(), std::size_t{3});
  DASO_CHECK(sel.Test(0));
  DASO_CHECK(sel.Test(63));
  DASO_CHECK(sel.Test(64));
  DASO_CHECK(!sel.Test(1));
  DASO_CHECK(!sel.Test(65));

  sel.Set(0, false);
  DASO_CHECK(!sel.Test(0));
  DASO_CHECK_EQ(sel.Count(), std::size_t{2});

  sel.SetAll(true);
  DASO_CHECK_EQ(sel.Count(), CatalogSize());
  sel.SetAll(false);
  DASO_CHECK_EQ(sel.Count(), std::size_t{0});

  // Fuera de rango: no debe corrupcionar memoria ni propagarse al resto.
  sel.Set(CatalogSize() + 999, true);
  DASO_CHECK_EQ(sel.Count(), std::size_t{0});
}

DASO_TEST(Vital_MatchesSystemCriticalPackages) {
  const char* vital[] = {
      "com.android.systemui",
      "com.android.settings",
      "com.android.shell",
      "com.android.phone",
      "com.android.launcher3",
      "com.android.launcher3.config",
      "org.lineageos.launcher",
      "com.android.inputmethod.latin",
      "com.google.android.gms",
      "com.android.bluetooth",
      "com.android.externalstorage",
      "com.miui.home",
  };
  for (const char* p : vital) {
    DASO_CHECK(IsVital(p));
    DASO_CHECK(!VitalReason(p).empty());
  }
}

DASO_TEST(Vital_DoesNotMatchDebloatablePackages) {
  const char* safe[] = {
      "com.android.theme.font.rubik",
      "com.android.theme.icon.heart",
      "com.android.systemui.clocks.bignum",
      "com.android.avatarpicker",
      "com.android.wallpaper",
  };
  for (const char* p : safe) {
    DASO_CHECK(!IsVital(p));
    DASO_CHECK(VitalReason(p).empty());
  }
}

DASO_TEST(Vital_IsCaseInsensitive) {
  DASO_CHECK(IsVital("COM.ANDROID.SYSTEMUI"));
  DASO_CHECK(IsVital("com.Android.Shell"));
}

DASO_TEST(Vital_CatalogHasNoVitalEntries) {
  // Ninguno de los 144 paquetes del catalogo es vital: el catalogo es, por
  // definicion, la lista de lo que se puede quitar. Si esto falla, se ha
  // colado algo que no se deberia poder tocar.
  for (const auto& e : Catalog()) {
    DASO_CHECK(!IsVital(e.name));
  }
}

DASO_TEST(Vital_IsSeparateFromCatalogTier) {
  // El nivel vital es un predicado sobre el NOMBRE, no una propiedad de la
  // entrada. Un paquete que el usuario meta a mano en packages.txt tambien
  // pasa por el filtro.
  DASO_CHECK(IsVital("com.android.settings"));
  const std::size_t idx = FindIndex("com.android.settings");
  DASO_CHECK_EQ(idx, CatalogSize());
}

DASO_TEST(WildcardMatch_Basics) {
  DASO_CHECK(WildcardMatch("com.android.*", "com.android.settings"));
  DASO_CHECK(!WildcardMatch("com.android.*", "com.androidx.settings"));
  DASO_CHECK(WildcardMatch("*", "cualquiera"));
  DASO_CHECK(WildcardMatch("", ""));
  DASO_CHECK(!WildcardMatch("", "x"));
  DASO_CHECK(WildcardMatch("android", "android"));
  DASO_CHECK(!WildcardMatch("android", "androidx"));
  DASO_CHECK(WildcardMatch("a*b*c", "axxbyyc"));
  DASO_CHECK(!WildcardMatch("a*b*c", "axxbyy"));
  DASO_CHECK(WildcardMatch("*.inputmethod.*", "com.android.inputmethod.latin"));
  DASO_CHECK(WildcardMatch("a**b", "ab"));
  DASO_CHECK(WildcardMatch("**", ""));
}

DASO_TEST(WildcardMatch_Backtracking) {
  // El algoritmo clasico de dos indices debe acertar el patron patologico:
  // hace backtracking aunque ".*" en POSIX lo resolveria de otra forma.
  DASO_CHECK(WildcardMatch("*a*b*c*d", "aaabbbcccd"));
  DASO_CHECK(!WildcardMatch("*a*b*c*d", "aaabbbccc"));
  DASO_CHECK(WildcardMatch("*x", "xxx"));
  DASO_CHECK(WildcardMatch("x*", "xxx"));
}
