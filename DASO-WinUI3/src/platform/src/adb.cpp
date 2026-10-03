#include "daso/adb.hpp"

#include <algorithm>
#include <string>
#include <unordered_set>

namespace daso::adb {
namespace {

constexpr std::string_view kUserArgPrefix = "--user ";

std::string_view Trim(std::string_view s) noexcept {
  const auto is_space = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
  while (!s.empty() && is_space(s.front())) s.remove_prefix(1);
  while (!s.empty() && is_space(s.back())) s.remove_suffix(1);
  return s;
}

// `pm list packages` devuelve una linea por paquete: "package:com.foo".
std::string_view PackageFromLine(std::string_view line) noexcept {
  line = Trim(line);
  constexpr std::string_view kPrefix = "package:";
  if (line.size() <= kPrefix.size()) return {};
  if (line.substr(0, kPrefix.size()) != kPrefix) return {};
  return line.substr(kPrefix.size());
}

DeviceState ParseDeviceState(std::string_view token) noexcept {
  if (token == "device") return DeviceState::Device;
  if (token == "offline") return DeviceState::Offline;
  if (token == "unauthorized") return DeviceState::Unauthorized;
  return DeviceState::Unknown;
}

// Quita el prefijo `package:` de una lista de salida de `pm list packages`
// dentro de un rango de lineas, usando un conjunto de los ya vistos.
std::string_view ValueAfter(std::string_view key, std::string_view line) noexcept {
  const std::size_t pos = line.find(key);
  if (pos == std::string_view::npos) return {};
  return Trim(line.substr(pos + key.size()));
}

}  // namespace

std::string_view to_string(DeviceState s) noexcept {
  switch (s) {
    case DeviceState::Device: return "device";
    case DeviceState::Offline: return "offline";
    case DeviceState::Unauthorized: return "unauthorized";
    case DeviceState::Unknown: return "unknown";
  }
  return "?";
}

// Une argumentos ya escapados, sin ejecutable.
std::wstring JoinArgs(const std::vector<std::wstring>& args) {
  std::vector<std::wstring_view> views;
  views.reserve(args.size());
  for (const std::wstring& a : args) views.push_back(a);
  return win::BuildArgumentsLine(std::move(views));
}

std::vector<Device> ListDevices(const CancelToken& cancel, const win::OutputSink& log) {
  std::vector<Device> devices;
  const auto adb = win::FindAdb();
  if (!adb) {
    if (log) log("adb no encontrado en PATH ni en las rutas del SDK");
    return devices;
  }

  // `devices -l` imprime una linea de cabecera que hay que saltar:
  //   List of devices attached
  bool header = true;
  const win::ProcessResult result = win::RunCaptured(
      *adb, L"devices -l", L"", cancel, [&](std::string_view line) {
        if (header) {
          if (line.find("List of devices") != std::string_view::npos) return;
          header = false;
        }
        const std::string_view trimmed = Trim(line);
        if (trimmed.empty()) return;

        Device device;
        // "SERIAL<TAB>state" y despues " key:value" si hay mas datos.
        std::string serial_storage;
        std::string model_storage;

        const std::size_t tab = trimmed.find('\t');
        std::string_view rest = trimmed;
        if (tab != std::string_view::npos) {
          serial_storage = std::string(trimmed.substr(0, tab));
          rest = trimmed.substr(tab + 1);
        } else {
          const std::size_t space = trimmed.find(' ');
          if (space == std::string_view::npos) return;
          serial_storage = std::string(trimmed.substr(0, space));
          rest = trimmed.substr(space + 1);
        }

        const std::size_t state_end = rest.find(' ');
        const std::string_view state_token =
            state_end == std::string_view::npos ? rest : rest.substr(0, state_end);
        device.serial = std::move(serial_storage);
        device.state = ParseDeviceState(state_token);

        if (state_end != std::string_view::npos) {
          const std::string_view model = ValueAfter("model:", rest);
          if (!model.empty()) model_storage = std::string(model);
          device.model = std::move(model_storage);
        }

        if (!device.serial.empty()) devices.push_back(std::move(device));
      });

  if (!result.launched && log) {
    log("No se pudo ejecutar adb devices: error " + std::to_string(result.error));
  }
  return devices;
}

std::vector<PackageState> QueryPackageStates(std::string_view serial,
                                              const CancelToken& cancel,
                                              const win::OutputSink& log) {
  const auto catalog = Catalog();
  std::vector<PackageState> states(catalog.size(), PackageState::Unknown);

  const auto adb = win::FindAdb();
  if (!adb) return states;

  // owning strings: Utf8ToWide devuelve un temporal y un string_view
  // a el quedaria colgando en cuanto terminase la expresion.
  std::vector<std::wstring> base;
  if (!serial.empty()) {
    base.push_back(L"-s");
    base.push_back(win::Utf8ToWide(serial));
  }

  // Paso 1: todos los instalados.
  std::unordered_set<std::string_view> installed;
  {
    auto args = base;
    args.push_back(L"shell");
    args.push_back(L"pm");
    args.push_back(L"list");
    args.push_back(L"packages");
    win::RunCaptured(*adb, JoinArgs(args), L"", cancel, [&](std::string_view line) {
      const std::string_view pkg = PackageFromLine(line);
      if (!pkg.empty()) installed.insert(pkg);
    });
  }

  // Paso 2: los deshabilitados, que son un subconjunto de los instalados.
  std::unordered_set<std::string_view> disabled;
  {
    auto args = base;
    args.push_back(L"shell");
    args.push_back(L"pm");
    args.push_back(L"list");
    args.push_back(L"packages");
    args.push_back(L"-d");
    win::RunCaptured(*adb, JoinArgs(args), L"", cancel, [&](std::string_view line) {
      const std::string_view pkg = PackageFromLine(line);
      if (!pkg.empty()) disabled.insert(pkg);
    });
  }

  for (std::size_t i = 0; i < catalog.size(); ++i) {
    const std::string_view name = catalog[i].name;
    if (disabled.count(name) != 0) {
      states[i] = PackageState::Disabled;
    } else if (installed.count(name) != 0) {
      states[i] = PackageState::Enabled;
    } else {
      states[i] = PackageState::Absent;
    }
  }

  if (log) {
    std::size_t enabled = 0, off = 0, missing = 0;
    for (PackageState s : states) {
      if (s == PackageState::Enabled) ++enabled;
      else if (s == PackageState::Disabled) ++off;
      else if (s == PackageState::Absent) ++missing;
    }
    log("Estado del dispositivo: " + std::to_string(enabled) + " habilitados, " +
        std::to_string(off) + " deshabilitados, " + std::to_string(missing) +
        " ausentes de " + std::to_string(catalog.size()));
  }
  return states;
}

std::wstring BuildBatchCommand(std::wstring_view adb_path, std::string_view serial,
                               std::span<const PackageEntry> catalog, const Batch& batch,
                               std::size_t user_id) {
  // `pm disable-user` y `pm enable` aceptan VARIOS paquetes en una sola llamada.
  // Esta es la funcion que convierte 144 viajes en 2 o 3.
  std::vector<std::wstring> args;
  if (!serial.empty()) {
    args.push_back(L"-s");
    args.push_back(win::Utf8ToWide(serial));
  }
  args.push_back(L"shell");
  args.push_back(L"pm");
  args.push_back(batch.action == PackageAction::Disable ? L"disable-user" : L"enable");
  args.push_back(L"--user");
  args.push_back(win::Utf8ToWide(std::to_string(user_id)));
  for (const PlannedItem& item : batch.items) {
    if (item.index < catalog.size()) {
      args.push_back(win::Utf8ToWide(catalog[item.index].name));
    }
  }

  std::vector<std::wstring_view> views;
  views.reserve(args.size());
  for (const std::wstring& a : args) views.push_back(a);
  return win::BuildCommandLine(adb_path, views);
}

ExecuteSummary Execute(std::string_view adb_path, std::string_view serial,
                       const PlanReport& plan, const ExecuteOptions& options,
                       const CancelToken& cancel, LogBuffer& log,
                       const ProgressSink& progress) {
  ExecuteSummary summary;
  const auto catalog = Catalog();
  summary.planned = plan.planned_packages;

  if (plan.blocked_vital > 0) {
    log.AppendLine("[bloqueado] " + std::to_string(plan.blocked_vital) +
                   " paquete(s) vital(es) excluido(s) por seguridad");
  }

  std::size_t done = 0;
  const std::size_t total = std::max<std::size_t>(1, summary.planned);

  for (const Batch& batch : plan.batches) {
    if (cancel.IsCancelled()) {
      summary.cancelled = true;
      break;
    }

    // El resolutor convierte un lote fallido en sub-lotes hasta aislar el
    // paquete culpable. Con presupuesto por defecto el peor caso es 2N-1.
    BatchResolver resolver(batch);
    bool budget_exhausted = false;

    while (auto next = resolver.Next()) {
      if (cancel.IsCancelled()) {
        summary.cancelled = true;
        break;
      }

      std::vector<std::wstring> args;
      args.push_back(L"-s");
      args.push_back(win::Utf8ToWide(serial));
      args.push_back(L"shell");
      args.push_back(L"pm");
      args.push_back(next->action == PackageAction::Disable ? L"disable-user" : L"enable");
      args.push_back(L"--user");
      args.push_back(win::Utf8ToWide(std::to_string(options.user_id)));
      for (const PlannedItem& item : next->items) {
        if (item.index < catalog.size()) {
          args.push_back(win::Utf8ToWide(catalog[item.index].name));
        }
      }

      const std::wstring arguments = JoinArgs(args);
      const std::wstring display =
          win::BuildCommandLine(win::Utf8ToWide(adb_path), [&] {
            std::vector<std::wstring_view> v;
            for (const std::wstring& a : args) v.push_back(a);
            return v;
          }());

      if (options.dry_run) {
        log.AppendLine("[simulacion] " + win::WideToUtf8(display));
        resolver.Report(BatchOutcome::Succeeded);
        ++summary.adb_runs;
        continue;
      }

      log.Append("$ " + win::WideToUtf8(display));

      bool saw_error = false;
      const win::ProcessResult result = win::RunCaptured(
          win::Utf8ToWide(adb_path), arguments, L"", cancel, [&](std::string_view line) {
            if (line.empty()) return;
            // pm escribe los fallos como "Error: ..." o "Unknown package".
            const bool is_error =
                line.rfind("Error", 0) == 0 ||
                line.find("Exception") != std::string_view::npos ||
                line.find("Unknown package") != std::string_view::npos;
            if (is_error) saw_error = true;
            log.AppendLine(std::string(line));
          });

      ++summary.adb_runs;

      if (result.terminated || cancel.IsCancelled()) {
        summary.cancelled = true;
        resolver.Report(BatchOutcome::Failed);
        break;
      }

      // pm devuelve 0 incluso con algunos fallos dentro, asi que la senal
      // fiable es el texto de error, no solo el codigo de salida.
      const bool succeeded = result.exit_code == 0 && !saw_error;
      resolver.Report(succeeded ? BatchOutcome::Succeeded : BatchOutcome::Failed);
    }

    if (resolver.BudgetExhausted()) {
      budget_exhausted = true;
      log.AppendLine(
          "[aviso] se agoto el presupuesto de biseccion: hay paquetes cuyo "
          "estado no se pudo precisar");
    }
    (void)budget_exhausted;

    for (PackageOutcome outcome : resolver.Outcomes()) {
      switch (outcome) {
        case PackageOutcome::Ok:
          ++summary.ok;
          ++done;
          break;
        case PackageOutcome::Failed:
          ++summary.failed;
          ++done;
          break;
        case PackageOutcome::Skipped:
          ++summary.skipped;
          ++done;
          break;
        case PackageOutcome::Pending:
          break;
      }
    }
    if (progress) progress(done, total);

    if (summary.cancelled) break;
  }

  log.AppendLine("Fin: " + std::to_string(summary.ok) + " ok, " +
                 std::to_string(summary.failed) + " fallidos, " +
                 std::to_string(summary.skipped) + " sin determinar, " +
                 std::to_string(summary.adb_runs) + " viajes a adb");
  return summary;
}

}  // namespace daso::adb
