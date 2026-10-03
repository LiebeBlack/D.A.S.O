// D.A.S.O - manifiesto de cambios.
//
// Se escribe ANTES de tocar el dispositivo, con el estado previo de cada
// paquete. Sin esto, un debloat sin vuelta atras es una apuesta; con esto,
// "reactivar todo" es un comando.
//
// JSON propio y deliberado: el esquema es fijo y pequeño, asi que un escritor
// y un lector minimos evitan arrastrar una dependencia de terceros y mantienen
// el ejecutable autocontenido.
#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include "daso/catalog.hpp"

namespace daso {

inline constexpr int kManifestVersion = 1;

struct ManifestEntry {
  std::string package;
  PackageState before = PackageState::Unknown;
  PackageState after = PackageState::Unknown;
};

struct Manifest {
  int version = kManifestVersion;
  std::string serial;
  std::string timestamp_utc;  // ISO 8601, p. ej. 2026-10-03T07:15:00Z
  PackageAction action = PackageAction::Disable;
  std::vector<ManifestEntry> entries;
};

enum class ManifestError : std::uint8_t {
  NotAnObject,
  UnsupportedVersion,
  MissingField,
  BadValue,
  BadEnum,
  Truncated,
};

// --- Escritura -------------------------------------------------------------

// Produce JSON estable: claves en orden fijo, indentacion de 2 espacios y
// saltos \n, para que el archivo sea legible y los diffs salgan limpios.
[[nodiscard]] std::string SerializeManifest(const Manifest& m);

// --- Lectura ---------------------------------------------------------------

[[nodiscard]] std::expected<Manifest, ManifestError> ParseManifest(std::string_view json);

// Nombre de archivo segun el formato del manifiesto:
//   <serial>_<AAAAMMDD-HHMMSS>.json
[[nodiscard]] std::string ManifestFileName(std::string_view serial,
                                          std::string_view compact_utc);

// --- Utilidades de tiempo --------------------------------------------------

// Marca de tiempo actual en UTC. Lo calcula la capa de plataforma; aqui solo
// se convierte el formato, para que el nucleo siga siendo portable.
[[nodiscard]] std::string FormatIso8601Utc(std::int64_t unix_seconds) noexcept;
[[nodiscard]] std::string FormatCompactUtc(std::int64_t unix_seconds) noexcept;

}  // namespace daso
