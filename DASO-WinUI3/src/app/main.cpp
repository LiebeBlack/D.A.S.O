// D.A.S.O - punto de entrada.
//
// ARQUITECTURA DE ARRANQUE (leer antes de tocar nada)
//
// Esta app es WinUI 3 SIN /ZW y SIN ficheros XAML. Eso condiciona el arranque:
//
//  1. No hay `App.xaml` ni `MainWindow.xaml`, asi que no hay
//     `InitializeComponent()` ni `Generated InitializeComponent` que el
//     compilador de XAML inyecte. El punto de entrada arranca WinUI a mano.
//
//  2. Sin la proyeccion de lenguaje de /ZW, las clases base se escriben con
//     `winrt::implements<...>` y el prefijo `winrt::`.
//
//  3. WinUI 3 exige un hilo de interfaz COM de un solo hilo:
//     `winrt::apartment_type::single_threaded`.
//
//  4. Despliegue "unpackaged + self-contained": el runtime va dentro de la
//     carpeta de la app, asi que NO hace falta llamar al bootstrapper
//     DynamicDependency. Ese bloque esta comentado y solo hace falta si se
//     cambia a depender del runtime instalado.
#include <windows.h>

#include <cstdio>
#include <string>

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.ApplicationModel.Activation.h>
#include <winrt/Microsoft.UI.Xaml.h>

#include "daso/process.hpp"
#include "daso/ui/main_window.hpp"
#include "daso/ui/ui_common.hpp"

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;

namespace {

// MessageBox de Win32 a proposito: si WinUI no llega a arrancar, cualquier
// dialogo de XAML tampoco funcionara. Un GetLastMessage de Win32 siempre imprime.
int Fatal(const wchar_t* stage, DWORD error) {
  wchar_t* text = nullptr;
  const DWORD n = FormatMessageW(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
          FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
      reinterpret_cast<wchar_t*>(&text), 0, nullptr);

  std::wstring message;
  if (n > 0 && text != nullptr) {
    message.assign(text, n);
    LocalFree(text);
  } else {
    message = L"(sin descripcion del error)";
  }

  wchar_t buffer[1024];
  swprintf_s(buffer, L"D.A.S.O no pudo arrancar.\n\nFase: %s\nError %lu: %s",
             stage, error, message.c_str());
  ::MessageBoxW(nullptr, buffer, L"D.A.S.O", MB_ICONERROR | MB_OK);
  return static_cast<int>(error);
}

// Un unico mutex por usuario. Dos copias WritingWindow a la vez sobre el mismo
// dispositivo se pisarian entre si durante un lote.
constexpr const wchar_t* kInstanceMutexName = L"Local\\DASO.Nativo.SingleInstance";

}  // namespace

// --- Objeto Application -----------------------------------------------------
//
// Implementa IFrameworkApplicationSource: es el gancho que WinUI llama para
// saber que clase de aplicacion instanciar. Sin /ZW esto se escribe a mano.
struct App : winrt::implements<App, IFrameworkApplicationSource> {
  void OnLaunched(Windows::ApplicationModel::Activation::LaunchActivatedEventArgs const&) {
    // idioma del sistema antes de construir nada: la tabla de textos se
    // consulta durante el montage de la ventana.
    daso::ui::SetLanguage(daso::ui::DetectLanguage());

    if (!daso::ui::MainWindow::Create()) {
      ::MessageBoxW(nullptr, L"La ventana principal no se pudo crear.",
                    L"D.A.S.O", MB_ICONERROR | MB_OK);
      ::ExitProcess(1);
    }
  }
};

int WINAPI wWinMain(HINSTANCE, LPWSTR, int) {
  // 1. Un solo proceso. Si ya hay otra instancia, esta se retira sin tocar el
  //    mutex que posee la otra (mismo error que comprueba AcquireSingleInstance).
  if (!daso::win::AcquireSingleInstance(kInstanceMutexName)) {
    ::MessageBoxW(nullptr,
                  L"D.A.S.O ya est\u00E1 en ejecuci\u00F3n.\n\n"
                  "Ci\u00E9rralo desde la bandeja o cierra la otra ventana.",
                  L"D.A.S.O", MB_ICONINFORMATION | MB_OK);
    return 0;
  }

  // 2. WinUI necesita COM en un solo hilo con/agregar por ventana.
  winrt::init_apartment(winrt::apartment_type::single_threaded);

  // Descomentar SOLO si se despliega dependiendo del runtime instalado en vez
  // de llevarlo dentro (WindowsAppSDKSelfContained=false):
  //
  //   winrt::Windows::Management::Deployment::DynamicDependency::Bootstrap::Initialize(
  //       L"Microsoft.WindowsAppRuntime.2.0", Application::WindowsAppRuntimeVersion());

  // 3. Arranque. Un fallo aqui significa que falta el Windows App Runtime.
  try {
    Application::Start([](auto&&) { winrt::make<::App>(); });
  } catch (hresult_error const& e) {
    daso::win::ReleaseSingleInstance();
    return Fatal(L"Application::Start", e.code().value);
  } catch (...) {
    daso::win::ReleaseSingleInstance();
    return Fatal(L"Application::Start", HRESULT_FROM_WIN32(ERROR_UNHANDLED_EXCEPTION));
  }

  // 4. Aviso, nunca elevacion. D.A.S.O no necesita administrador y no se
  //    auto-eleva; si llega aqui elevado es por otra causa y se dice.
  if (daso::win::IsElevated()) {
    ::MessageBoxW(nullptr,
                  L"Nota: se est\u00E1 ejecutando como administrador.\n\n"
                  "D.A.S.O no lo necesita y funciona igual sin permisos elevados.",
                  L"D.A.S.O", MB_ICONINFORMATION | MB_OK);
  }

  daso::win::ReleaseSingleInstance();
  return 0;
}
