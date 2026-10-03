#include "daso/ui/main_window.hpp"

#include <algorithm>
#include <functional>
#include <thread>

#include "daso/manifest.hpp"
#include "daso/manifest_store.hpp"
#include "daso/process.hpp"

using namespace winrt;
using namespace winrt::Windows::Foundation;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using namespace winrt::Microsoft::UI::Xaml::Media;

namespace daso::ui {
namespace {

AppState g_state;
com_ptr<MainViewModel> g_viewmodel;

// --- Utilidades de construccion ---------------------------------------------

uint32_t ToArgb(Color c) noexcept {
  return (static_cast<uint32_t>(c.a) << 24) | (static_cast<uint32_t>(c.r) << 16) |
         (static_cast<uint32_t>(c.g) << 8) | static_cast<uint32_t>(c.b);
}

SolidColorBrush BrushOf(Color c) {
  auto b = SolidColorBrush();
  b.Color(Windows::UI::Color{ToArgb(c)});
  return b;
}

// Degradado de fondo una sola vez, en vez de las cientos de lineas que la
// version Tkinter dibujaba y repintaba en cada evento de redimensionado.
void ApplyGradient(const FrameworkElement& root) {
  auto brush = LinearGradientBrush();
  auto stops = GradientStopCollection();
  auto s1 = GradientStop();
  s1.Color(Windows::UI::Color{ToArgb(kBackgroundTop)});
  s1.Offset(0.0);
  auto s2 = GradientStop();
  s2.Color(Windows::UI::Color{ToArgb(kBackgroundBottom)});
  s2.Offset(1.0);
  stops.Append(s1);
  stops.Append(s2);
  brush.GradientStops(stops);
  brush.StartPoint(Point{0.0, 0.0});
  brush.EndPoint(Point{0.0, 1.0});
  root.Background(brush);
}

TextBlock MakeText(hstring text, double size, Color color) {
  auto tb = TextBlock();
  tb.Text(text);
  tb.FontSize(size);
  tb.Foreground(BrushOf(color));
  tb.TextWrapping(TextWrapping::Wrap);
  return tb;
}

Button MakeButton(hstring text, bool primary) {
  auto b = Button();
  b.Content(text);
  b.Background(BrushOf(primary ? kAccent : kSurfaceAlt));
  b.Foreground(BrushOf(kText));
  b.Padding(Thickness{14, 8, 14, 8});
  b.CornerRadius(CornerRadius{6, 6, 6, 6});
  return b;
}

// --- Refresco de la lista ---------------------------------------------------

void RefreshList(ListView& list, const std::wstring& query) {
  list.Items().Clear();

  FilterRequest request;
  request.text = winrt::to_string(query);
  request.selection = SelectionFilter::Ignore;
  request.spanish = (CurrentLanguage() == Language::Spanish);

  const auto hits = FilterCatalog(Catalog(), request, &g_state.selection);
  g_state.visible = hits;

  for (std::size_t index : hits) {
    const auto& entry = Catalog()[index];

    auto line = Grid();
    auto cols = ColumnDefinitions();
    cols.Append(ColumnDefinition());
    cols.Append(ColumnDefinition());
    ColumnDefinition::SetWidth(cols.Append(ColumnDefinition()), 260.0);
    line.ColumnDefinitions(cols);

    // Casilla de seleccion
    auto box = CheckBox();
    box.IsChecked(g_state.selection.Test(index));
    Grid::SetColumn(box, 0);
    box.Tag(to_hstring(index));
    line.Children().Append(box);

    // Nombre legible + identificador tecnico + descripcion de lo que se pierde.
    const auto text = LocalizedName(entry, CurrentLanguage() == Language::Spanish);
    const auto desc =
        LocalizedDescription(entry, CurrentLanguage() == Language::Spanish);

    auto panel = StackPanel();
    panel.Spacing(2);
    panel.Children().Append(MakeText(to_hstring(text), 15.0, kText));

    auto id = MakeText(to_hstring(entry.name), 11.5, kTextDim);
    panel.Children().Append(id);

    auto why = MakeText(to_hstring(desc), 12.0, TierColor(static_cast<int>(entry.tier)));
    panel.Children().Append(why);

    Grid::SetColumn(panel, 1);
    line.Children().Append(panel);

    list.Items().Append(line);
  }
}

}  // namespace

// ---------------------------------------------------------------------------
// MainViewModel
// ---------------------------------------------------------------------------

void MainViewModel::Raise(PropertyId which) {
  hstring name;
  switch (which) {
    case PropertyId::Status: name = L"Status"; break;
    case PropertyId::DeviceText: name = L"DeviceText"; break;
    case PropertyId::LogText: name = L"LogText"; break;
    case PropertyId::SafeMode: name = L"SafeMode"; break;
    case PropertyId::SelectionCount: name = L"SelectionCount"; break;
    case PropertyId::Progress: name = L"Progress"; break;
    case PropertyId::AdbFound: name = L"AdbFound"; break;
  }
  PropertyChanged(*this, PropertyChangedEventHandler{nullptr, {name}});
}

// ---------------------------------------------------------------------------
// Ventana
// ---------------------------------------------------------------------------

bool MainWindow::Create() {
  if (!winrt::Microsoft::UI::Xaml::Application::Current().DispatcherQueue()) {
    return false;
  }

  auto window = Window();
  window.Title(T(L"app.title"));
  window.ExtendsContentIntoTitleBar(true);

  g_state.selection.Resize(CatalogSize());
  g_state.states.assign(CatalogSize(), PackageState::Unknown);
  g_state.log.AppendLine("D.A.S.O listo.");

  g_viewmodel = winrt::make<MainViewModel>();

  // --- Raiz -----------------------------------------------------------------
  auto root = Grid();
  root.RowDefinitions().Append(RowDefinition());
  root.RowDefinitions().Append(RowDefinition());
  root.RowDefinitions().Append(RowDefinition());
  root.RowDefinitions().Append(RowDefinition());
  root.RowDefinitions().Append(RowDefinition());
  ApplyGradient(root);

  // --- Cabecera -------------------------------------------------------------
  auto header = StackPanel();
  header.Spacing(2);
  header.Margin(Thickness{20, 16, 20, 10});
  header.Children().Append(MakeText(T(L"app.title"), 26.0, kText));
  header.Children().Append(MakeText(T(L"app.subtitle"), 13.0, kTextDim));

  auto device_row = StackPanel();
  device_row.Orientation(Orientation::Horizontal);
  device_row.Spacing(8);
  device_row.Children().Append(MakeText(T(L"device.label"), 13.0, kTextDim));

  auto device_box = ComboBox();
  device_box.MinWidth(260.0);
  device_box.Items().Append(to_hstring(T(L"device.none")));
  device_row.Children().Append(device_box);

  auto verify = MakeButton(T(L"action.verify"), false);
  device_row.Children().Append(verify);

  auto sync = MakeButton(T(L"action.sync"), false);
  device_row.Children().Append(sync);

  header.Children().Append(device_row);
  Grid::SetRow(header, 0);
  root.Children().Append(header);

  // --- Buscador y acciones de seleccion ------------------------------------
  auto toolbar = Grid();
  auto toolbar_cols = ColumnDefinitions();
  toolbar_cols.Append(ColumnDefinition());
  toolbar_cols.Append(ColumnDefinition());
  ColumnDefinition::SetWidth(toolbar_cols.Append(ColumnDefinition()), 150.0);
  toolbar.ColumnDefinitions(toolbar_cols);

  auto search = AutoSuggestBox();
  search.PlaceholderText(T(L"search.placeholder"));
  search.QueryIcon().Visibility(Visibility::Collapsed);
  Grid::SetColumn(search, 0);
  toolbar.Children().Append(search);

  auto select_all = MakeButton(T(L"select.all"), false);
  auto select_none = MakeButton(T(L"select.none"), false);
  auto safe_toggle = ToggleSwitch();
  safe_toggle.IsOn(true);
  safe_toggle.Header(T(L"safe_mode.toggle"));

  auto sel_panel = StackPanel();
  sel_panel.Orientation(Orientation::Horizontal);
  sel_panel.Spacing(6);
  sel_panel.Children().Append(select_all);
  sel_panel.Children().Append(select_none);
  sel_panel.Children().Append(safe_toggle);
  Grid::SetColumn(sel_panel, 1);
  toolbar.Children().Append(sel_panel);

  toolbar.Margin(Thickness{20, 0, 20, 8});
  Grid::SetRow(toolbar, 1);
  root.Children().Append(toolbar);

  // --- Lista ----------------------------------------------------------------
  auto list = ListView();
  list.Margin(Thickness{20, 0, 20, 8});
  list.SelectionMode(ListViewSelectionMode::None);
  RefreshList(list, L"");
  Grid::SetRow(list, 2);
  root.Children().Append(list);

  // --- Barra de estado ------------------------------------------------------
  auto status = MakeText(T(L"status.ready"), 13.0, kTextDim);
  status.Margin(Thickness{20, 0, 20, 6});
  Grid::SetRow(status, 3);
  root.Children().Append(status);

  // --- Acciones + registro --------------------------------------------------
  auto footer = Grid();
  auto footer_cols = ColumnDefinitions();
  footer_cols.Append(ColumnDefinition());
  footer_cols.Append(ColumnDefinition());
  ColumnDefinition::SetWidth(footer_cols.Append(ColumnDefinition()), 420.0);
  footer.ColumnDefinitions(footer_cols);

  auto buttons = StackPanel();
  buttons.Orientation(Orientation::Horizontal);
  buttons.Spacing(8);

  auto disable_btn = MakeButton(T(L"action.disable"), true);
  auto enable_btn = MakeButton(T(L"action.enable"), false);
  auto undo_btn = MakeButton(T(L"action.restore"), false);
  auto dry_btn = MakeButton(T(L"action.dry_run"), false);
  buttons.Children().Append(disable_btn);
  buttons.Children().Append(enable_btn);
  buttons.Children().Append(undo_btn);
  buttons.Children().Append(dry_btn);
  Grid::SetColumn(buttons, 0);
  footer.Children().Append(buttons);

  auto log_view = TextBlock();
  log_view.Text(L"");
  log_view.Foreground(BrushOf(kTextDim));
  log_view.FontSize(12.0);
  log_view.TextWrapping(TextWrapping::Wrap);
  log_view.MaxLines(6);
  Grid::SetColumn(log_view, 1);
  footer.Children().Append(log_view);

  footer.Margin(Thickness{20, 0, 20, 16});
  Grid::SetRow(footer, 4);
  root.Children().Append(footer);

  window.Content(root);
  window.Activate();

  // --- Cableado de eventos --------------------------------------------------
  // Sin XAML no hay atributos Click="...": todo se conecta aqui. Los handlers
  // son lambdas, no metodos con ABI, que es justamente lo que /ZW exigia.
  search.TextChanged([&](auto&&, auto const& args) {
    RefreshList(list, to_hstring(args.NewText()));
  });

  safe_toggle.Toggled([&](auto&&, auto const& args) {
    g_state.safe_mode = args.IsOn();
    if (g_state.safe_mode) {
      // Volver a modo seguro DESHACE la seleccion de riesgo: es la garantia de
      // que el estado inicial es Conservative.
      g_state.selection.SetAll(false);
      for (std::size_t i = 0; i < CatalogSize(); ++i) {
        if (Catalog()[i].tier == RiskTier::Safe) g_state.selection.Set(i, true);
      }
    }
    RefreshList(list, to_hstring(search.Text()));
  });

  select_all.Click([&](auto&&, auto const&) {
    g_state.selection.SetAll(true);
    RefreshList(list, to_hstring(search.Text()));
  });

  select_none.Click([&](auto&&, auto const&) {
    g_state.selection.SetAll(false);
    RefreshList(list, to_hstring(search.Text()));
  });

  verify.Click([&](auto&&, auto const&) {
    if (g_state.busy) return;
    g_state.busy = true;
    verify.IsEnabled(false);
    status.Text(T(L"status.working"));

    // Hilo aparte: la UI no se bloquea nunca.
    std::thread([window2 = window, verify_button = verify]() {
      const auto devices = adb::ListDevices(g_state.cancel, [&](std::string_view line) {
        g_state.log.AppendLine(std::string(line));
      });
      g_state.devices = devices;

      auto queue = window2.DispatcherQueue();
      queue.TryEnqueue([window2, devices]() {
        device_box.Items().Clear();
        if (devices.empty()) {
          device_box.Items().Append(to_hstring(T(L"device.none")));
        } else {
          for (const auto& d : devices) {
            std::wstring label = winrt::to_hstring(d.serial);
            if (!d.model.empty()) label += L"  " + winrt::to_hstring(d.model);
            if (!d.Usable()) label += L"  [" + std::wstring(adb::to_string(d.state)) + L"]";
            device_box.Items().Append(to_hstring(label));
          }
        }
        g_state.busy = false;
        verify_button.IsEnabled(true);
        status.Text(T(L"status.ready"));
      });
    }).detach();
  });

  // Accion principal: deshabilitar lo seleccionado.
  disable_btn.Click([&](auto&&, auto const&) {
    if (g_state.busy) return;

    std::vector<PlannedItem> wanted;
    for (std::size_t i = 0; i < CatalogSize(); ++i) {
      if (g_state.selection.Test(i)) wanted.push_back(PlannedItem{i, PackageAction::Disable});
    }
    if (wanted.empty()) return;

    g_state.busy = true;
    g_state.cancel.Reset();
    disable_btn.IsEnabled(false);
    status.Text(T(L"status.working"));

    std::thread([window2 = window, wanted = std::move(wanted)]() {
      // El nucleo decide: descarta lo vital, deduplica y trocea en lotes.
      const PlanReport plan = BuildPlan(wanted, Catalog(), PlanLimits{});

      const auto adb = win::FindAdb();
      if (!adb) {
        g_state.log.AppendLine("adb no encontrado.");
      } else {
        const std::string serial = g_state.devices.empty()
                                       ? std::string()
                                       : g_state.devices[g_state.selected_device].serial;
        const std::int64_t now = UnixNow();

        // El manifiesto se escribe ANTES de tocar el dispositivo: si el proceso
        // muere a mitad, sigue habiendo registro de lo que iba a cambiarse.
        const Manifest manifest =
            win::BuildManifest(serial, PackageAction::Disable, now, Catalog(), wanted,
                               g_state.states);
        if (const auto path = win::SaveManifest(manifest, now)) {
          g_state.last_manifest_path = win::WideToUtf8(*path);
          g_state.log.AppendLine("Manifiesto: " + g_state.last_manifest_path);
        } else {
          g_state.log.AppendLine("AVISO: no se pudo guardar el manifiesto; "
                                 "esta operacion no podra revertirse");
        }

        const auto summary =
            adb::Execute(win::WideToUtf8(*adb), serial, plan,
                         adb::ExecuteOptions{/*dry_run=*/false, 0}, g_state.cancel,
                         g_state.log);
        g_state.log.AppendLine(std::to_string(summary.ok) + " ok, " +
                               std::to_string(summary.failed) + " fallidos");
      }

      auto queue = window2.DispatcherQueue();
      queue.TryEnqueue([window2]() {
        g_state.busy = false;
        status.Text(T(L"status.ready"));
      });
    }).detach();
  });

  // Revertir: vuelve a habilitar lo del ultimo manifiesto.
  undo_btn.Click([&](window2 = window, undo_button = undo_btn)(auto&&, auto const&) {
    if (g_state.busy) return;

    const std::string serial = g_state.devices.empty()
                                   ? std::string()
                                   : g_state.devices[g_state.selected_device].serial;
    const auto manifest = win::LoadLatestManifest(serial);
    if (!manifest) {
      g_state.log.AppendLine("No hay ningun cambio previo que revertir.");
      return;
    }

    // Deshacer una deshabilitacion es HABILITAR lo que conste en el manifiesto.
    const auto planned = win::PlanFromManifest(*manifest, Catalog());
    if (planned.empty()) {
      g_state.log.AppendLine("El manifiesto no contiene nada que revertir.");
      return;
    }

    // `manifest` es un local de este lambda: el hilo NO puede capturarlo por
    // referencia. Se copia lo justo antes de lanzarlo.
    const std::string stamp = manifest->timestamp_utc;

    g_state.busy = true;
    g_state.cancel.Reset();
    undo_button.IsEnabled(false);

    std::thread([window2, planned = std::move(planned), serial,
                 stamp = std::move(stamp)]() {
      const PlanReport plan = BuildPlan(planned, Catalog(), PlanLimits{});
      const auto adb = win::FindAdb();
      if (!adb) {
        g_state.log.AppendLine("adb no encontrado.");
      } else {
        g_state.log.AppendLine("Revirtiendo " + std::to_string(plan.planned_packages) +
                               " paquete(s) del manifiesto " + stamp);
        const auto summary = adb::Execute(win::WideToUtf8(*adb), serial, plan,
                                          adb::ExecuteOptions{false, 0}, g_state.cancel,
                                          g_state.log);
        g_state.log.AppendLine(std::to_string(summary.ok) + " reactivados, " +
                               std::to_string(summary.failed) + " fallidos");
      }
      auto queue = window2.DispatcherQueue();
      queue.TryEnqueue([window2, undo_button]() {
        g_state.busy = false;
        undo_button.IsEnabled(true);
      });
    }).detach();
  });

  dry_btn.Click([&](auto&&, auto const&) {
    if (g_state.busy) return;
    const auto adb = win::FindAdb();
    if (!adb) {
      g_state.log.AppendLine("adb no encontrado.");
      return;
    }
    std::vector<PlannedItem> wanted;
    for (std::size_t i = 0; i < CatalogSize(); ++i) {
      if (g_state.selection.Test(i)) wanted.push_back(PlannedItem{i, PackageAction::Disable});
    }
    const PlanReport plan = BuildPlan(wanted, Catalog(), PlanLimits{});
    adb::Execute(win::WideToUtf8(*adb), "", plan, adb::ExecuteOptions{true, 0},
                 g_state.cancel, g_state.log);
  });

  return true;
}

}  // namespace daso::ui
