// Pruebas del manifiesto: ida y vuelta, escapes, errores y fechas.
#include <string>
#include <vector>

#include "daso/manifest.hpp"
#include "test_harness.hpp"

using namespace daso;

namespace {

// El arnés usa #cond para stringificar, y stringificar NO escapa barras
// invertidas: escribir '\n' dentro de un DASO_CHECK rompe la compilacion.
// Los literales con escape se llevan a constantes con nombre.
constexpr char kNewline = '\n';
constexpr const char* kJsonVersion = "\"version\": 1";
constexpr const char* kJsonSerial = "\"serial\": \"R5CT30ABCDE\"";
constexpr const char* kJsonAction = "\"action\": \"disable\"";
constexpr const char* kEscapedQuotes = "\\\"comillas\\\"";
constexpr const char* kEscapedBackslash = "com\\\\dentro";

Manifest MakeManifest() {
  Manifest m;
  m.serial = "R5CT30ABCDE";
  m.timestamp_utc = "2026-10-03T07:15:00Z";
  m.action = PackageAction::Disable;
  m.entries.push_back({"com.android.theme.font.rubik", PackageState::Enabled,
                       PackageState::Disabled});
  m.entries.push_back({"com.android.wallpaper", PackageState::Disabled,
                       PackageState::Disabled});
  m.entries.push_back({"com.example.no.instalado", PackageState::Absent, PackageState::Absent});
  return m;
}

}  // namespace

DASO_TEST(Manifest_RoundTrip) {
  const Manifest original = MakeManifest();
  const std::string json = SerializeManifest(original);
  const auto parsed = ParseManifest(json);
  DASO_CHECK(parsed.has_value());
  if (!parsed) return;

  const Manifest& m = *parsed;
  DASO_CHECK_EQ(m.version, kManifestVersion);
  DASO_CHECK_EQ(m.serial, original.serial);
  DASO_CHECK_EQ(m.timestamp_utc, original.timestamp_utc);
  DASO_CHECK(m.action == original.action);
  DASO_CHECK_EQ(m.entries.size(), original.entries.size());
  for (std::size_t i = 0; i < m.entries.size(); ++i) {
    DASO_CHECK_EQ(m.entries[i].package, original.entries[i].package);
    DASO_CHECK(m.entries[i].before == original.entries[i].before);
    DASO_CHECK(m.entries[i].after == original.entries[i].after);
  }
}

DASO_TEST(Manifest_SerializesValidJsonShape) {
  const std::string json = SerializeManifest(MakeManifest());
  DASO_CHECK(json.front() == '{');
  DASO_CHECK(json.back() == kNewline);
  DASO_CHECK(json.find(kJsonVersion) != std::string::npos);
  DASO_CHECK(json.find(kJsonSerial) != std::string::npos);
  DASO_CHECK(json.find(kJsonAction) != std::string::npos);
}

DASO_TEST(Manifest_EscapesStrings) {
  Manifest m;
  m.serial = "com\\dentro";
  m.timestamp_utc = "2026-01-01T00:00:00Z";
  m.entries.push_back({"com.\"comillas\"", PackageState::Enabled, PackageState::Disabled});

  const std::string json = SerializeManifest(m);
  // Las comillas y barras del contenido no pueden romper el documento.
  // Los literales con escape viven fuera del macro: ver nota arriba.
  DASO_CHECK(json.find(kEscapedQuotes) != std::string::npos);
  DASO_CHECK(json.find(kEscapedBackslash) != std::string::npos);

  const auto parsed = ParseManifest(json);
  DASO_CHECK(parsed.has_value());
  if (parsed) {
    DASO_CHECK_EQ((*parsed).serial, std::string("com\\dentro"));
    DASO_CHECK_EQ((*parsed).entries[0].package, std::string("com.\"comillas\""));
  }
}

DASO_TEST(Manifest_HandlesNonAsciiInComments) {
  // El manifiesto guarda nombres de paquete (ASCII), pero un usuario puede
  // dejar texto acentuado en el campo serial. Debe sobrevivir la ida y vuelta.
  Manifest m;
  m.serial = "MÓVIL-Ñ";
  m.timestamp_utc = "2026-01-01T00:00:00Z";
  m.entries.push_back({"com.test.ñ", PackageState::Enabled, PackageState::Disabled});

  const auto parsed = ParseManifest(SerializeManifest(m));
  DASO_CHECK(parsed.has_value());
  if (parsed) {
    DASO_CHECK_EQ((*parsed).serial, std::string("MÓVIL-Ñ"));
    DASO_CHECK_EQ((*parsed).entries[0].package, std::string("com.test.ñ"));
  }
}

DASO_TEST(Manifest_DecodesUnicodeEscapes) {
  const char* json =
      R"({"version":1,"serial":"\u00d1o\u00f1o","timestamp":"2026-01-01T00:00:00Z",)"
      R"("action":"enable","packages":[{"package":"com.a","before":"enabled","after":"enabled"}]})";
  const auto parsed = ParseManifest(json);
  DASO_CHECK(parsed.has_value());
  if (parsed) DASO_CHECK_EQ((*parsed).serial, std::string("Ñoño"));
}

DASO_TEST(Manifest_EmptyPackagesList) {
  const char* json =
      R"({"version":1,"serial":"X","timestamp":"2026-01-01T00:00:00Z",)"
      R"("action":"disable","packages":[]})";
  const auto parsed = ParseManifest(json);
  DASO_CHECK(parsed.has_value());
  if (parsed) DASO_CHECK(parsed->entries.empty());
}

DASO_TEST(Manifest_RejectsGarbage) {
  DASO_CHECK(!ParseManifest("").has_value());
  DASO_CHECK(!ParseManifest("no soy json").has_value());
  DASO_CHECK(!ParseManifest("{").has_value());
  DASO_CHECK(!ParseManifest("[]").has_value());               // no es un objeto
  DASO_CHECK(!ParseManifest(R"({"serial":"X"})").has_value());  // falta version
}

DASO_TEST(Manifest_RejectsWrongVersion) {
  const char* json =
      R"({"version":99,"serial":"X","timestamp":"t","action":"disable","packages":[]})";
  const auto parsed = ParseManifest(json);
  DASO_CHECK(!parsed.has_value());
  if (!parsed) DASO_CHECK(parsed.error() == ManifestError::UnsupportedVersion);
}

DASO_TEST(Manifest_RejectsBadActionEnum) {
  const char* json =
      R"({"version":1,"serial":"X","timestamp":"t","action":"borrar","packages":[]})";
  const auto parsed = ParseManifest(json);
  DASO_CHECK(!parsed.has_value());
  if (!parsed) DASO_CHECK(parsed.error() == ManifestError::BadEnum);
}

DASO_TEST(Manifest_RejectsMissingPackagesKey) {
  const char* json = R"({"version":1,"serial":"X","timestamp":"t","action":"disable"})";
  DASO_CHECK(!ParseManifest(json).has_value());
}

DASO_TEST(Manifest_IgnoresUnknownKeys) {
  // Un manifiesto escrito por una version futura no debe Romperse al leerse
  // si los campos que nos interesan siguen ahi.
  const char* json =
      R"({"version":1,"serial":"X","timestamp":"t","action":"disable","packages":[],)"
      R"("future_field":{"a":[1,2,3]},"another":null})";
  const auto parsed = ParseManifest(json);
  DASO_CHECK(parsed.has_value());
}

DASO_TEST(Manifest_UnknownStateDegradesToUnknown) {
  const char* json =
      R"({"version":1,"serial":"X","timestamp":"t","action":"disable",)"
      R"("packages":[{"package":"com.a","before":"weird","after":"enabled"}]})";
  const auto parsed = ParseManifest(json);
  DASO_CHECK(parsed.has_value());
  if (parsed) {
    DASO_CHECK((*parsed).entries[0].before == PackageState::Unknown);
  }
}

DASO_TEST(Manifest_FileNameShape) {
  const std::string name = ManifestFileName("R5CT30ABCDE", "20261003-071500");
  DASO_CHECK_EQ(name, std::string("R5CT30ABCDE_20261003-071500.json"));
}

// ---------------------------------------------------------------------------
// Fechas
// ---------------------------------------------------------------------------

DASO_TEST(Date_Iso8601FromEpoch) {
  DASO_CHECK_EQ(FormatIso8601Utc(0), std::string("1970-01-01T00:00:00Z"));
  DASO_CHECK_EQ(FormatIso8601Utc(1), std::string("1970-01-01T00:00:01Z"));
  DASO_CHECK_EQ(FormatIso8601Utc(86'399), std::string("1970-01-01T23:59:59Z"));
  DASO_CHECK_EQ(FormatIso8601Utc(86'400), std::string("1970-01-02T00:00:00Z"));
}

DASO_TEST(Date_KnownEpochValues) {
  // Valores calculados aparte, no generados con la misma funcion bajo prueba.
  DASO_CHECK_EQ(FormatIso8601Utc(1'767'225'600), std::string("2026-01-01T00:00:00Z"));
  DASO_CHECK_EQ(FormatIso8601Utc(1'735'084'800), std::string("2024-12-25T00:00:00Z"));
  DASO_CHECK_EQ(FormatIso8601Utc(1'735'689'600), std::string("2025-01-01T00:00:00Z"));
  // Salto de ano bisiesto: 2024-02-29T12:00:00Z
  DASO_CHECK_EQ(FormatIso8601Utc(1'709'208'000), std::string("2024-02-29T12:00:00Z"));
}

DASO_TEST(Date_HandlesNegativeEpoch) {
  DASO_CHECK_EQ(FormatIso8601Utc(-1), std::string("1969-12-31T23:59:59Z"));
  DASO_CHECK_EQ(FormatIso8601Utc(-86'400), std::string("1969-12-31T00:00:00Z"));
}

DASO_TEST(Date_CompactFormatHasNoSeparators) {
  const std::string c = FormatCompactUtc(1'767'225'600);
  DASO_CHECK_EQ(c, std::string("20260101-000000"));
  DASO_CHECK_EQ(c.size(), std::size_t{15});
  DASO_CHECK(c.find(':') == std::string::npos);
}

DASO_TEST(Date_RoundTripsThroughFileName) {
  const std::int64_t t = 1'767'225'600;
  const std::string name = ManifestFileName("DEV", FormatCompactUtc(t));
  const auto parsed = ParseManifest(
      std::string(R"({"version":1,"serial":"DEV","timestamp":")") + FormatIso8601Utc(t) +
      R"(","action":"disable","packages":[]})");
  DASO_CHECK(parsed.has_value());
  DASO_CHECK(name.find(FormatCompactUtc(t)) != std::string::npos);
}
