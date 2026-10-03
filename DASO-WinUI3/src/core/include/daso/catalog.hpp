// D.A.S.O - catalogo de paquetes
//
// Capa de nucleo. ISO C++ puro: sin Windows SDK, sin WinRT, sin excepciones.
// Todos los datos viven en constexpr, asi que el catalogo entero acaba en
// .rdata y no hay coste de arranque ni asignaciones en tiempo de ejecucion.
#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace daso {

// Que tan peligroso es deshabilitar el paquete.
// El orden importa: `Vital < Caution < Safe` en cuanto a permision para actuar.
enum class RiskTier : std::uint8_t {
  Vital = 0,   // nunca se deshabilita, ni aunque el usuario lo fuerce
  Caution = 1, // quita algo visible; exige salir del modo seguro
  Safe = 2,    // solo quita una opcion concreta de una galeria
};

enum class Category : std::uint8_t {
  Themes,
  SystemUi,
  CustomRom,
  Overlay,
  Aosp,
  Google,
  Vendor,
  Other,
  Count,
};

enum class PackageAction : std::uint8_t { Disable, Enable };

// Estado real observado en el dispositivo.
enum class PackageState : std::uint8_t { Unknown, Absent, Enabled, Disabled };

struct PackageEntry {
  std::string_view name;        // "com.android.theme.font.rubik"
  std::string_view display_es;  // nombre legible en espanol
  std::string_view display_en;  // nombre legible en ingles
  std::string_view desc_es;     // que se pierde al deshabilitarlo
  std::string_view desc_en;
  RiskTier tier;
  Category category;
};

[[nodiscard]] std::string_view to_string(RiskTier t) noexcept;
[[nodiscard]] std::string_view to_string(Category c) noexcept;
[[nodiscard]] std::string_view to_string(PackageAction a) noexcept;
[[nodiscard]] std::string_view to_string(PackageState s) noexcept;

// --- Guardas de nivel vital -------------------------------------------------
//
// Estas reglas se aplican a TODO paquete, incluidas las listas que el usuario
// cargue desde packages.txt. Son el unico punto donde se decide que no se
// ejecuta algo, y no se puede desactivar desde la interfaz.

// Empareja un patron con `*` (cero o mas caracteres, sin distinguir mayusculas).
// Implementado a mano: std::regex compila y reserva en cada llamada.
[[nodiscard]] bool WildcardMatch(std::string_view pattern, std::string_view text) noexcept;

// `true` si el paquete no debe tocarse jamas.
[[nodiscard]] bool IsVital(std::string_view package_name) noexcept;

// Motivo por el que un paquete es vital (vacio si no lo es).
[[nodiscard]] std::string_view VitalReason(std::string_view package_name) noexcept;

// Aplica el idioma al par (es, en).
[[nodiscard]] std::string_view LocalizedName(const PackageEntry& e, bool spanish) noexcept;
[[nodiscard]] std::string_view LocalizedDescription(const PackageEntry& e,
                                                    bool spanish) noexcept;

// Bitset de seleccion: un bit por paquete del catalogo.
// std::vector<bool> es un proxy lento; esto es un contenedor plano y trivial.
class Selection {
 public:
  Selection() = default;
  explicit Selection(std::size_t catalog_size) { Resize(catalog_size); }

  // Los bits sobrantes de la ultima palabra se enmascaran: si no, SetAll
  // activaria 192 bits en un catalogo de 144 y el recuento mentiria.
  void Resize(std::size_t catalog_size) {
    size_ = catalog_size;
    bits_.assign((catalog_size + 63) / 64, 0);
  }

  [[nodiscard]] bool Test(std::size_t i) const noexcept {
    return i < size_ && ((bits_[i / 64] >> (i % 64)) & 1u) != 0;
  }

  void Set(std::size_t i, bool v) noexcept {
    if (i >= size_) return;
    const auto mask = std::uint64_t{1} << (i % 64);
    if (v) {
      bits_[i / 64] |= mask;
    } else {
      bits_[i / 64] &= ~mask;
    }
  }

  void SetAll(bool v) noexcept {
    for (auto& w : bits_) w = v ? ~std::uint64_t{0} : std::uint64_t{0};
    MaskTail();
  }

  [[nodiscard]] std::size_t Size() const noexcept { return size_; }

  [[nodiscard]] std::size_t Count() const noexcept {
    std::size_t n = 0;
    for (std::size_t w = 0; w < bits_.size(); ++w) {
      std::uint64_t word = bits_[w];
      if (w + 1 == bits_.size()) word &= TailMask();
      n += static_cast<std::size_t>(std::popcount(word));
    }
    return n;
  }

  [[nodiscard]] const std::vector<std::uint64_t>& Words() const noexcept { return bits_; }

 private:
  [[nodiscard]] std::uint64_t TailMask() const noexcept {
    const std::size_t rem = size_ % 64;
    return rem == 0 ? ~std::uint64_t{0} : (std::uint64_t{1} << rem) - 1;
  }

  void MaskTail() noexcept {
    if (size_ % 64 != 0 && !bits_.empty()) {
      bits_.back() &= TailMask();
    }
  }

  std::vector<std::uint64_t> bits_;
  std::size_t size_ = 0;
};

}  // namespace daso

// -----------------------------------------------------------------------------
// El catalogo generado.
//
// catalog_data.hpp declara `daso::detail::kCatalogRecords` y depende de los
// tipos de arriba, asi que se incluye aqui, FUERA de `namespace daso`: incluirlo
// dentro abriria `daso::daso::detail`.
// -----------------------------------------------------------------------------
#include "daso/catalog_data.hpp"

namespace daso {

// Vista de solo lectura al catalogo generado.
[[nodiscard]] std::span<const PackageEntry> Catalog() noexcept;

[[nodiscard]] constexpr std::size_t CatalogSize() noexcept {
  return detail::kCatalogRecords.size();
}

// Busca por nombre exacto. Devuelve el indice o CatalogSize() si no existe.
[[nodiscard]] std::size_t FindIndex(std::string_view package_name) noexcept;

}  // namespace daso
