// D.A.S.O - motor ADB.
//
// Une el planificador del nucleo (que decide QUE hacer) con la ejecucion real
// (que lanza adb y lee su salida). No sabe nada de interfaz.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "daso/cancel.hpp"
#include "daso/catalog.hpp"
#include "daso/log_buffer.hpp"
#include "daso/plan.hpp"
#include "daso/process.hpp"

namespace daso::adb {

// --- Dispositivos -----------------------------------------------------------

enum class DeviceState : std::uint8_t { Offline, Unauthorized, Device, Unknown };

struct Device {
  std::string serial;
  std::string model;   // lo que devuelve `adb devices -l`, campo `model:`
  DeviceState state = DeviceState::Unknown;

  [[nodiscard]] bool Usable() const noexcept { return state == DeviceState::Device; }
};

// `adb devices -l`
[[nodiscard]] std::vector<Device> ListDevices(const CancelToken& cancel,
                                              const win::OutputSink& log);

[[nodiscard]] std::string_view to_string(DeviceState s) noexcept;

// --- Estado de paquetes -----------------------------------------------------

// Tri-estado por indice del catalogo, leyendo:
//   pm list packages -u   (todos)
//   pm list packages -d   (deshabilitados)
// Dos llamadas en vez de una por paquete.
[[nodiscard]] std::vector<PackageState> QueryPackageStates(std::string_view serial,
                                                            const CancelToken& cancel,
                                                            const win::OutputSink& log);

// --- Ejecucion --------------------------------------------------------------

struct ExecuteOptions {
  bool dry_run = false;   // genera los comandos y los registra, no lanza nada
  std::size_t user_id = 0;
};

struct ExecuteSummary {
  std::size_t planned = 0;
  std::size_t ok = 0;
  std::size_t failed = 0;
  std::size_t skipped = 0;  // la biseccion se quedo sin presupuesto
  std::size_t adb_runs = 0;  // viajes reales a adb
  bool cancelled = false;
  std::string last_error;
};

using ProgressSink = std::function<void(std::size_t done, std::size_t total)>;

// Ejecuta el plan. Cuando un lote falla, bISECA para localizar el paquete
// culpable en vez de darlo por perdido entero.
[[nodiscard]] ExecuteSummary Execute(std::string_view adb_path, std::string_view serial,
                                     const PlanReport& plan, const ExecuteOptions& options,
                                     const CancelToken& cancel, LogBuffer& log,
                                     const ProgressSink& progress = {});

// Construye la linea de comandos de un lote. Publica para poder registrarla y
// probarla sin lanzar nada (modo de simulacion).
[[nodiscard]] std::wstring BuildBatchCommand(std::wstring_view adb_path,
                                             std::string_view serial,
                                             std::span<const PackageEntry> catalog,
                                             const Batch& batch, std::size_t user_id);

}  // namespace daso::adb
