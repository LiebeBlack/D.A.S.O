// D.A.S.O - planificacion y resolucion de lotes ADB.
//
// Aqui vive el salto de rendimiento: en vez de un viaje a adb por paquete,
// un lote de N paquetes en una sola llamada `pm disable-user`. Cuando un lote
// falla, el resolutor biseca el lote para localizar el paquete culpable sin
// repetir el trabajo ya hecho.
//
// La politica es codigo puro y sin Win32, asi que se testea directamente.
#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "daso/catalog.hpp"

namespace daso {

struct PlanLimits {
  // Android acepta muchos paquetes por llamada, pero un comando con miles de
  // argumentos falla de forma distinta segun la ROM. 64 es conservador.
  std::size_t max_packages_per_batch = 64;
  // Techo de longitud del argumento completo, por debajo del limite de linea
  // de comandos de Windows (32767) para que quepa el resto del comando adb.
  std::size_t max_command_chars = 28'000;
};

struct PlannedItem {
  std::size_t index;            // indice dentro del catalogo
  PackageAction action;
};

struct Batch {
  PackageAction action = PackageAction::Disable;
  std::vector<PlannedItem> items;

  [[nodiscard]] std::size_t Size() const noexcept { return items.size(); }
  [[nodiscard]] bool Empty() const noexcept { return items.empty(); }

  // Longitud aproximada si estos nombres se concatenan separados por espacios.
  // Se usa para trocear antes de construir la linea de comandos real.
  [[nodiscard]] std::size_t NameChars(std::span<const PackageEntry> catalog) const noexcept;
};

struct PlanReport {
  std::vector<Batch> batches;
  std::size_t blocked_vital = 0;       // descartados por la guarda de nivel vital
  std::size_t duplicates_dropped = 0;  // el mismo paquete pedido dos veces
  std::size_t planned_packages = 0;
};

// Construye los lotes. Descarta lo vital y deduplica, y ambos conteos quedan
// visibles para que la interfaz pueda explicarlo en vez de fallar en silencio.
[[nodiscard]] PlanReport BuildPlan(std::span<const PlannedItem> requested,
                                   std::span<const PackageEntry> catalog,
                                   const PlanLimits& limits = {});

// ---------------------------------------------------------------------------
// Resolucion por bisection
// ---------------------------------------------------------------------------

enum class BatchOutcome : std::uint8_t {
  Succeeded,  // `pm` acepto el lote entero
  Failed,     // `pm` devolvio error: al menos un paquete del lote es culpable
};

enum class PackageOutcome : std::uint8_t {
  Pending,  // aun no ejecutado
  Ok,       // aceptado por pm
  Failed,   // identificado como el que hace fallar el lote
  Skipped,  // presupuesto agotado antes de poder determinarlo
};

struct BisectPolicy {
  // Profundidad maxima de reintento. 6 -> como mucho 2^6 sub-lotes por rama.
  std::size_t max_depth = 6;
  // Tope de llamadas adb adicionales. 0 significa 2*N-1, que es el peor caso
  // de una bisección completa sin limite practico.
  std::size_t max_extra_runs = 0;
};

// Machine de estados del resolutor. Uso:
//
//   BatchResolver r(batch);
//   while (auto cmd = r.Next()) { r.Report(ejecutar(cmd) ? Succeeded : Failed); }
//   for (auto o : r.Outcomes()) { ... }
//
// `Outcomes()` tiene un elemento por cada elemento del lote original, en el
// mismo orden.
class BatchResolver {
 public:
  BatchResolver(const Batch& root, BisectPolicy policy = {});

  // Siguiente comando a ejecutar, o nullopt si no queda nada.
  [[nodiscard]] std::optional<Batch> Next();

  // Resultado del lote devuelto por la ultima llamada a Next().
  // Invalido si Next() devolvio nullopt.
  void Report(BatchOutcome outcome) noexcept;

  [[nodiscard]] bool Done() const noexcept { return pending_.empty() && !in_flight_; }
  [[nodiscard]] std::size_t Runs() const noexcept { return runs_; }
  [[nodiscard]] bool BudgetExhausted() const noexcept { return budget_exhausted_; }
  [[nodiscard]] std::span<const PackageOutcome> Outcomes() const noexcept {
    return outcomes_;
  }

  // Numero de paquetes ya resueltos como Ok.
  [[nodiscard]] std::size_t SucceededCount() const noexcept;

 private:
  struct Slice {
    std::size_t begin;
    std::size_t end;
    std::size_t depth;
  };

  Batch root_;
  BisectPolicy policy_;
  std::vector<Slice> pending_;  // pila de trabajo
  std::vector<PackageOutcome> outcomes_;
  Batch current_;
  Slice current_slice_{0, 0, 0};
  bool in_flight_ = false;
  std::size_t runs_ = 0;         // llamadas ya resueltas (incluye la raiz)
  std::size_t extra_runs_ = 0;   // reintentos consumidos
  bool budget_exhausted_ = false;
};

}  // namespace daso
