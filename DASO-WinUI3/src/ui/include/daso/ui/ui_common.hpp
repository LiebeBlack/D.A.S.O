// D.A.S.O - textos y tema de la interfaz.
//
// Todo el texto visible pasa por aqui. No hay ni una cadena suelta en el
// codigo de la UI: es lo unico que garantiza que la app este completa en los
// dos idiomas.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace daso::ui {

enum class Language : std::uint8_t { Spanish, English };

// Idioma del sistema en el arranque.
[[nodiscard]] Language DetectLanguage();

// Idioma actual. Cambiarlo no reinicia nada: la vista se refresca.
void SetLanguage(Language language) noexcept;
[[nodiscard]] Language CurrentLanguage() noexcept;

// Texto por clave. Devuelve la clave si no existe, para que la falta se vea
// en pantalla en vez de dejar un hueco.
[[nodiscard]] std::wstring_view T(Language lang, std::string_view key);
[[nodiscard]] std::wstring_view T(std::string_view key);

// --- Paleta -----------------------------------------------------------------
//
// El degradado azul -> negro de la version Tkinter se conserva, pero aqui lo
// compone WinUI con un LinearGradientBrush en vez de dibujar cientos de lineas
// y repintarlas en cada Configure.

struct Color {
  std::uint8_t r = 0, g = 0, b = 0, a = 255;
};

inline constexpr Color kAccent{0x3B, 0x7D, 0xF7, 255};
inline constexpr Color kBackgroundTop{0x0B, 0x3D, 0x91, 255};
inline constexpr Color kBackgroundBottom{0x00, 0x00, 0x00, 255};
inline constexpr Color kSurface{0x15, 0x15, 0x17, 255};
inline constexpr Color kSurfaceAlt{0x1E, 0x1E, 0x22, 255};
inline constexpr Color kText{0xF5, 0xF5, 0xF7, 255};
inline constexpr Color kTextDim{0xA0, 0xA0, 0xA8, 255};
inline constexpr Color kSafe{0x3F, 0xC1, 0x7A, 255};
inline constexpr Color kCaution{0xE2, 0xB7, 0x1E, 255};
inline constexpr Color kVital{0xE0, 0x4F, 0x4F, 255};

// Color del indicador de nivel de riesgo.
[[nodiscard]] constexpr Color TierColor(int tier_value) noexcept {
  switch (tier_value) {
    case 0: return kVital;
    case 1: return kCaution;
    default: return kSafe;
  }
}

}  // namespace daso::ui
