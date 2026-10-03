// D.A.S.O - ventana principal.
//
// ARQUITECTURA CRITICA
// --------------------
// Esta aplicacion WinUI 3 esta escrita SIN /ZW (C++/WinRT language projection)
// y SIN ficheros XAML. El arbol visual se construye aqui, en C++, con las
// clases controlables de WinUI 3.
//
// Motivo: C++/WinRT solo llega hasta C++17, y el objetivo es compilar todo con
// C++26preview. Si hubiera un solo .xaml, el compilador de XAML generaria
// codigo que exige /ZW y la compilacion en C++26 seria imposible. Ademas, sin
// /ZW no existe proyeccion de lenguaje: todo pasa por winrt:: y por
// winrt::implements, que es C++ puro.
//
// Consecuencia aceptada: no hay vista de diseno de Visual Studio. El layout se
// escribe en codigo, lo cual para esta pantalla es una ventaja, no un problema.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

// Cabeceras de WinRT en modo solo-cabecero: sin /ZW no hay proyeccion.
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <winrt/Microsoft.UI.Xaml.Data.h>

#include "daso/adb.hpp"
#include "daso/catalog.hpp"
#include "daso/cancel.hpp"
#include "daso/filter.hpp"
#include "daso/log_buffer.hpp"
#include "daso/plan.hpp"
#include "daso/ui/ui_common.hpp"

namespace daso::ui {

// Estado observable de la ventana. Se notifica con PropertyChanged a mano:
// sin XAML no hay {Binding} generado, y este es el patron MVVM equivalente.
enum class PropertyId : int32_t {
  Status = 0,
  DeviceText,
  SafeMode,
  SelectionCount,
  Progress,
  LogText,
  AdbFound,
};

// Modelo de vista. winrt::implements es C++ puro: no necesita /ZW.
struct MainViewModel : winrt::implements<
                           winrt::Microsoft::UI::Xaml::Data::INotifyPropertyChanged,
                           winrt::Windows::Foundation::IStringable> {
  // Estado
  std::wstring status;
  std::wstring device_text;
  std::wstring log_text;
  std::wstring selection_text;
  bool safe_mode = true;
  bool adb_found = false;
  double progress = 0.0;

  void Raise(PropertyId which);

  // --- INotifyPropertyChanged ---
  winrt::event<winrt::Microsoft::UI::Xaml::Data::PropertyChangedEventHandler> PropertyChanged{
      nullptr, 0};

  void OnPropertyChanged(winrt::Windows::Foundation::IPropertySet const& /*sender*/,
                         winrt::hstring const& propertyName) {
    if (propertyName == L"Status") {
      Raise(PropertyId::Status);
    } else if (propertyName == L"DeviceText") {
      Raise(PropertyId::DeviceText);
    } else if (propertyName == L"LogText") {
      Raise(PropertyId::LogText);
    } else if (propertyName == L"SafeMode") {
      Raise(PropertyId::SafeMode);
    } else if (propertyName == L"SelectionCount") {
      Raise(PropertyId::SelectionCount);
    } else if (propertyName == L"Progress") {
      Raise(PropertyId::Progress);
    } else if (propertyName == L"AdbFound") {
      Raise(PropertyId::AdbFound);
    }
  }

  // --- IStringable (obligatorio para muchas APIs de WinUI) ---
  winrt::hstring ToString() { return L"D.A.S.O"; }
};

// Estado real de la aplicacion. Vive fuera del modelo de vista para que la
// vista no sea dueña de datos del nucleo.
struct AppState {
  std::vector<adb::Device> devices;
  std::size_t selected_device = 0;

  Selection selection{CatalogSize()};
  std::vector<PackageState> states{CatalogSize(), PackageState::Unknown};
  std::vector<std::size_t> visible;  // indices visibles tras el filtro

  LogBuffer log;
  CancelToken cancel;
  bool busy = false;
  std::wstring last_manifest_path;
};

class MainWindow {
 public:
  // Crea la ventana y monta el arbol visual. Devuelve false si el runtime de
  // Windows App SDK no esta disponible.
  [[nodiscard]] static bool Create();
};;

}  // namespace daso::ui
