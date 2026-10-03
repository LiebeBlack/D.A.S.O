// D.A.S.O - persistencia de manifiestos en disco.
//
// Escribe el manifiesto ANTES de tocar el dispositivo y lo relee para
// revertir. Vive en la capa Win32 porque usa la API de archivos de Windows y
// asi queda verificable sin Windows App SDK.
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "daso/catalog.hpp"
#include "daso/manifest.hpp"
#include "daso/plan.hpp"  // PlannedItem

namespace daso::win {

// Nombre base: <serial>_<AAAAMMDD-HHMMSS>.json
[[nodiscard]] std::string ManifestNameFor(std::string_view serial, std::int64_t now_utc);

// Guarda el manifiesto en %LOCALAPPDATA%\D.A.S.O\manifests.
// Devuelve la ruta completa, o nullopt si no se pudo escribir.
[[nodiscard]] std::optional<std::wstring> SaveManifest(const Manifest& manifest,
                                                       std::int64_t now_utc);

// Manifiesto mas reciente del dispositivo dado.
// `serial` vacio = cualquiera.
[[nodiscard]] std::optional<Manifest> LoadLatestManifest(std::string_view serial);

// Ruta completa del manifiesto mas reciente, sin leerlo.
[[nodiscard]] std::optional<std::wstring> LatestManifestPath(std::string_view serial);

// Construye un manifiesto a partir de lo que se va a ejecutar.
// `before` es el estado real observado en el dispositivo.
[[nodiscard]] Manifest BuildManifest(std::string_view serial, PackageAction action,
                                      std::int64_t now_utc,
                                      std::span<const PackageEntry> catalog,
                                      std::span<const PlannedItem> planned,
                                      std::span<const PackageState> before);

// Paquetes a reactivar a partir de un manifiesto: lo que conste como aplicado
// y no como ausente. Si el manifiesto era de una operacion `disable`, lo que
// se quiere es `enable`.
[[nodiscard]] std::vector<PlannedItem> PlanFromManifest(
    const Manifest& manifest, std::span<const PackageEntry> catalog);

}  // namespace daso::win
