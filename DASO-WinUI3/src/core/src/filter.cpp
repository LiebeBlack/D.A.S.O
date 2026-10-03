#include "daso/filter.hpp"

#include <algorithm>
#include <cstring>
#include <functional>

namespace daso {
namespace {

constexpr char AsciiLower(char c) noexcept {
  return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

// Busqueda sin sensibilidad a mayusculas. Devuelve offset o npos.
std::size_t IFind(std::string_view haystack, std::string_view needle) noexcept {
  if (needle.empty()) return 0;
  if (needle.size() > haystack.size()) return std::string_view::npos;
  const std::size_t last = haystack.size() - needle.size();
  for (std::size_t i = 0; i <= last; ++i) {
    std::size_t j = 0;
    while (j < needle.size() && AsciiLower(haystack[i + j]) == AsciiLower(needle[j])) ++j;
    if (j == needle.size()) return i;
  }
  return std::string_view::npos;
}

bool IStartsWith(std::string_view text, std::string_view prefix) noexcept {
  return text.size() >= prefix.size() && IFind(text.substr(0, prefix.size()), prefix) == 0;
}

}  // namespace

bool CaseInsensitiveCompare(std::string_view a, std::string_view b) noexcept {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (AsciiLower(a[i]) != AsciiLower(b[i])) return false;
  }
  return true;
}

int ScoreMatch(std::string_view query, std::string_view package_name,
               std::string_view display_es, std::string_view display_en,
               std::string_view desc_es, std::string_view desc_en) noexcept {
  if (query.empty()) return 1;  // todo coincide, con la puntuacion mas baja

  int best = 0;

  // El nombre del paquete es la clave tecnica: se busca literal.
  if (CaseInsensitiveCompare(package_name, query)) {
    best = std::max(best, 1000);
  }
  if (IStartsWith(package_name, query)) {
    best = std::max(best, 500);
  }
  if (IFind(package_name, query) != std::string_view::npos) {
    best = std::max(best, 300);
  }

  // El nombre legible es lo que el usuario normalmente busca ("rubik", "reloj").
  for (std::string_view display : {display_es, display_en}) {
    if (IStartsWith(display, query)) best = std::max(best, 250);
    if (IFind(display, query) != std::string_view::npos) best = std::max(best, 200);
  }

  // La descripcion es texto largo: matching mas debil.
  for (std::string_view desc : {desc_es, desc_en}) {
    if (IFind(desc, query) != std::string_view::npos) best = std::max(best, 100);
  }

  return best;
}

std::vector<std::size_t> FilterCatalog(std::span<const PackageEntry> catalog,
                                       const FilterRequest& request,
                                       const Selection* selection) noexcept {
  struct Hit {
    std::uint32_t score;
    std::uint32_t index;
  };
  std::vector<Hit> hits;
  hits.reserve(catalog.size());

  for (std::size_t i = 0; i < catalog.size(); ++i) {
    const PackageEntry& e = catalog[i];

    if (request.tier && e.tier != *request.tier) continue;
    if (request.category && e.category != *request.category) continue;

    if (selection != nullptr && request.selection != SelectionFilter::Ignore) {
      const bool selected = selection->Test(i);
      if (request.selection == SelectionFilter::OnlySelected && !selected) continue;
      if (request.selection == SelectionFilter::OnlyUnselected && selected) continue;
    }

    const int score = ScoreMatch(request.text, e.name, e.display_es, e.display_en,
                                 e.desc_es, e.desc_en);
    if (score == 0) continue;
    hits.push_back(Hit{static_cast<std::uint32_t>(score), static_cast<std::uint32_t>(i)});
  }

  // Puntuacion descendente y, en caso de empate, indice ASCENDENTE. El orden
  // tiene que ser estable entre pulsaciones de teclado, asi que el desempate
  // se fija explicitamente en vez de delegarlo en el algoritmo de ordenacion.
  std::ranges::sort(hits, [](const Hit& a, const Hit& b) {
    if (a.score != b.score) return a.score > b.score;
    return a.index < b.index;
  });

  std::vector<std::size_t> out;
  out.reserve(hits.size());
  for (const Hit& h : hits) out.push_back(static_cast<std::size_t>(h.index));
  return out;
}

TierCount CountTiers(std::span<const PackageEntry> catalog) noexcept {
  TierCount c;
  for (const auto& e : catalog) {
    switch (e.tier) {
      case RiskTier::Safe: ++c.safe; break;
      case RiskTier::Caution: ++c.caution; break;
      case RiskTier::Vital: ++c.vital; break;
    }
  }
  return c;
}

}  // namespace daso
