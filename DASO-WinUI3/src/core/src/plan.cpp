#include "daso/plan.hpp"

#include <algorithm>

namespace daso {

std::size_t Batch::NameChars(std::span<const PackageEntry> catalog) const noexcept {
  std::size_t n = 0;
  for (const PlannedItem& item : items) {
    if (item.index < catalog.size()) n += catalog[item.index].name.size() + 1;  // + espacio
  }
  return n;
}

PlanReport BuildPlan(std::span<const PlannedItem> requested,
                     std::span<const PackageEntry> catalog,
                     const PlanLimits& limits) {
  PlanReport report;

  // El lote se arma con los indices validos, deduplicados y no vitales.
  // `seen` es un vector plano: 144 paquetes, no merece un hash.
  std::vector<bool> seen(catalog.size(), false);
  std::vector<PlannedItem> accepted;
  accepted.reserve(requested.size());

  for (const PlannedItem& item : requested) {
    if (item.index >= catalog.size()) continue;
    if (seen[item.index]) {
      ++report.duplicates_dropped;
      continue;
    }
    seen[item.index] = true;

    // Guarda de nivel vital. Se aplica aqui, no en la interfaz: es el unico
    // sitio por el que pasa todo lo que se ejecuta, incluidas las listas que
    // el usuario haya editado a mano.
    if (IsVital(catalog[item.index].name)) {
      ++report.blocked_vital;
      continue;
    }
    accepted.push_back(item);
  }

  report.planned_packages = accepted.size();
  if (accepted.empty()) return report;

  // Un lote = una llamada a `pm`. `disable` y `enable` son subcomandos
  // distintos, asi que nunca se mezclan acciones en el mismo lote.
  for (const PackageAction action :
       {PackageAction::Disable, PackageAction::Enable}) {
    Batch batch;
    batch.action = action;
    std::size_t chars = 0;

    const auto flush = [&] {
      if (!batch.items.empty()) report.batches.push_back(std::move(batch));
      batch = Batch{};
      batch.action = action;
      chars = 0;
    };

    for (const PlannedItem& item : accepted) {
      if (item.action != action) continue;

      const std::size_t item_chars = catalog[item.index].name.size() + 1;
      const bool would_exceed_count =
          batch.items.size() >= std::max<std::size_t>(1, limits.max_packages_per_batch);
      const bool would_exceed_chars = !batch.items.empty() &&
                                      (chars + item_chars) > limits.max_command_chars;

      if (would_exceed_count || would_exceed_chars) flush();

      batch.items.push_back(item);
      chars += item_chars;
    }
    flush();
  }

  return report;
}

// ---------------------------------------------------------------------------
// BatchResolver
// ---------------------------------------------------------------------------

BatchResolver::BatchResolver(const Batch& root, BisectPolicy policy)
    : root_(root), policy_(policy) {
  outcomes_.assign(root_.items.size(), PackageOutcome::Pending);
  if (!root_.items.empty()) {
    pending_.push_back(Slice{0, root_.items.size(), 0});
  }
}

std::optional<Batch> BatchResolver::Next() {
  if (in_flight_) return std::nullopt;
  if (pending_.empty()) return std::nullopt;

  const Slice slice = pending_.back();
  pending_.pop_back();

  current_.action = root_.action;
  current_.items.clear();
  current_.items.reserve(slice.end - slice.begin);
  for (std::size_t i = slice.begin; i < slice.end; ++i) {
    current_.items.push_back(root_.items[i]);
  }

  current_slice_ = slice;
  in_flight_ = true;
  return current_;
}

void BatchResolver::Report(BatchOutcome outcome) noexcept {
  if (!in_flight_) return;
  in_flight_ = false;

  const Slice slice = current_slice_;
  const std::size_t size = slice.end - slice.begin;
  ++runs_;

  const auto mark = [&](PackageOutcome o) {
    for (std::size_t i = slice.begin; i < slice.end; ++i) outcomes_[i] = o;
  };

  if (outcome == BatchOutcome::Succeeded) {
    mark(PackageOutcome::Ok);
    return;
  }

  // Un lote de un solo paquete que falla ES el culpable: ya no hay nada que
  // partir.
  if (size <= 1) {
    mark(PackageOutcome::Failed);
    return;
  }

  const bool depth_ok = slice.depth + 1 <= policy_.max_depth;
  const bool budget_ok =
      policy_.max_extra_runs == 0 || extra_runs_ + 2 <= policy_.max_extra_runs;

  if (!depth_ok || !budget_ok) {
    // Se llego aqui solo por profundidad o presupuesto: no se puede precisar
    // mas. Se marcan como omitidos para que la interfaz los muestre, en vez
    // de fingir que funcionaron.
    budget_exhausted_ = true;
    mark(PackageOutcome::Skipped);
    return;
  }

  // Se empuja el lado derecho primero para que el izquierdo se procese antes
  // (pila): ante un fallo comun -- el ultimo paquete del lote -- se identifica
  // antes el culpable.
  const std::size_t mid = slice.begin + size / 2;
  pending_.push_back(Slice{mid, slice.end, slice.depth + 1});
  pending_.push_back(Slice{slice.begin, mid, slice.depth + 1});
  extra_runs_ += 2;
}

std::size_t BatchResolver::SucceededCount() const noexcept {
  std::size_t n = 0;
  for (PackageOutcome o : outcomes_) {
    if (o == PackageOutcome::Ok) ++n;
  }
  return n;
}

}  // namespace daso
