// D.A.S.O - filtrado y orden de la lista de paquetes.
//
// Puro: no sabe nada de la interfaz. Devuelve indices del catalogo, ya
// ordenados por relevancia (empate por indice, para que el orden sea estable
// entre pulsaciones de teclado).
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "daso/catalog.hpp"

namespace daso {

enum class SelectionFilter : std::uint8_t {
  Ignore,
  OnlySelected,
  OnlyUnselected,
};

struct FilterRequest {
  std::string_view text;  // vacio = coincide todo
  std::optional<RiskTier> tier{};
  std::optional<Category> category{};
  SelectionFilter selection = SelectionFilter::Ignore;
  // Spanish: las etiquetas se comparan en ambos idiomas siempre, pero este
  // campo decide cual se devuelve en la vista.
  bool spanish = true;
};

// Puntuacion de un paquete frente a un texto. 0 = no coincide.
[[nodiscard]] int ScoreMatch(std::string_view query, std::string_view package_name,
                             std::string_view display_es, std::string_view display_en,
                             std::string_view desc_es, std::string_view desc_en) noexcept;

// Indices del catalogo que satisfacen la peticion, de mas a menos relevante.
[[nodiscard]] std::vector<std::size_t> FilterCatalog(
    std::span<const PackageEntry> catalog, const FilterRequest& request,
    const Selection* selection = nullptr) noexcept;

// Cuantas entradas hay por nivel. Lo usa el panel lateral.
struct TierCount {
  std::size_t safe = 0;
  std::size_t caution = 0;
  std::size_t vital = 0;
};
[[nodiscard]] TierCount CountTiers(std::span<const PackageEntry> catalog) noexcept;

}  // namespace daso
