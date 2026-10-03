// D.A.S.O - implementacion del catalogo.
//
// Las reglas de nivel vital viven en catalog_data.hpp, generadas por
// tools/generate_catalog.py. C++ y Python comparten asi una unica fuente de
// verdad; tests/test_catalog.cpp lo verifica.
#include "daso/catalog.hpp"

#include <algorithm>
#include <cstring>

namespace daso {

std::string_view to_string(RiskTier t) noexcept {
  switch (t) {
    case RiskTier::Vital: return "Vital";
    case RiskTier::Caution: return "Caution";
    case RiskTier::Safe: return "Safe";
  }
  return "?";
}

std::string_view to_string(Category c) noexcept {
  switch (c) {
    case Category::Themes: return "Themes";
    case Category::SystemUi: return "SystemUi";
    case Category::CustomRom: return "CustomRom";
    case Category::Overlay: return "Overlay";
    case Category::Aosp: return "Aosp";
    case Category::Google: return "Google";
    case Category::Vendor: return "Vendor";
    case Category::Other: return "Other";
    case Category::Count: break;
  }
  return "?";
}

std::string_view to_string(PackageAction a) noexcept {
  return a == PackageAction::Disable ? "disable" : "enable";
}

std::string_view to_string(PackageState s) noexcept {
  switch (s) {
    case PackageState::Unknown: return "unknown";
    case PackageState::Absent: return "absent";
    case PackageState::Enabled: return "enabled";
    case PackageState::Disabled: return "disabled";
  }
  return "?";
}

namespace {

constexpr char AsciiLower(char c) noexcept {
  return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

constexpr char AsciiUpper(char c) noexcept {
  return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
}

}  // namespace

// Comparacion ASCII sin sensibilidad a mayusculas. No intenta ser Unicode:
// los nombres de paquete son ASCII por definicion.
std::string_view LocalizedName(const PackageEntry& e, bool spanish) noexcept {
  return spanish ? e.display_es : e.display_en;
}

std::string_view LocalizedDescription(const PackageEntry& e, bool spanish) noexcept {
  return spanish ? e.desc_es : e.desc_en;
}

std::span<const PackageEntry> Catalog() noexcept {
  return std::span<const PackageEntry>(detail::kCatalogRecords.data(),
                                       detail::kCatalogRecords.size());
}

std::size_t FindIndex(std::string_view package_name) noexcept {
  const auto cat = Catalog();
  for (std::size_t i = 0; i < cat.size(); ++i) {
    if (cat[i].name == package_name) return i;
  }
  return cat.size();
}

bool IsVital(std::string_view package_name) noexcept {
  return !VitalReason(package_name).empty();
}

std::string_view VitalReason(std::string_view package_name) noexcept {
  for (const auto& rule : detail::kVitalRules) {
    if (WildcardMatch(rule.pattern, package_name)) return rule.reason;
  }
  return {};
}

// Emparejador de glob con un unico metacaracter `*`.
//
// Implementado a mano en vez de con std::regex porque:
//   - std::regex compila en cada llamada (coste por paquete),
//   - std::regex reserva memoria dinamica,
//   - este caso (comparar contra ~24 reglas por paquete) es un problema de
//     dos indices, no de backtracking.
bool WildcardMatch(std::string_view pattern, std::string_view text) noexcept {
  std::size_t p = 0, t = 0;
  std::size_t star = std::string_view::npos;
  std::size_t match = 0;

  while (t < text.size()) {
    if (p < pattern.size() && AsciiLower(pattern[p]) == AsciiLower(text[t])) {
      ++p;
      ++t;
    } else if (p < pattern.size() && pattern[p] == '*') {
      star = p++;
      match = t;
    } else if (star != std::string_view::npos) {
      p = star + 1;
      t = ++match;
    } else {
      return false;
    }
  }
  while (p < pattern.size() && pattern[p] == '*') ++p;
  return p == pattern.size();
}

}  // namespace daso
