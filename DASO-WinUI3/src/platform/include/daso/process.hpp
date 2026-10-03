// D.A.S.O - capa Win32.
//
// Aqui NO hay WinUI: solo API de Windows. Compila igual con el SDK de MSVC y
// con MinGW, lo que permite verificarlo en cualquier maquina.
//
// Diseno (los puntos que no son obvios y estan comentados en el .cpp):
//   - CreateProcessW con PROC_THREAD_ATTRIBUTE_HANDLE_LIST: solo los handles
//     que el hijo necesita son heredables. Sin esto hay una carrera clasica en
//     la que el hijo hereda un handle que el padre todavia no ha cerrado.
//   - CREATE_SUSPENDED + AssignProcessToJobObject: si el proceso nace corriendo,
//     puede ejecutar codigo antes de que esteamos en condiciones de matarlo.
//     Nace suspendido, se configura el Job y luego se reanuda.
//   - Pipes con FILE_FLAG_OVERLAPPED y UN SOLO CreateIoCompletionPort para todo
//     el trafico. Ningun ReadFile bloqueante en el hilo de la interfaz.
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "daso/cancel.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace daso::win {

// Codigo de salida y error de un proceso ya terminado.
struct ProcessResult {
  DWORD exit_code = 0;
  bool launched = false;        // CreateProcessW tuvo exito
  bool terminated = false;      // lo cortamos nosotros por cancelacion
  DWORD error = 0;              // GetLastError() si !launched
};

// Linea de salida del proceso. Se entrega sin '\n' ni '\r'.
using OutputSink = std::function<void(std::string_view line)>;

// Ejecuta un proceso capturando stdout y stderr fundidos.
//
// Bloquea hasta que el proceso termina o se cancela. Pensado para el hilo
// de trabajo, nunca para el hilo de la interfaz.
ProcessResult RunCaptured(std::wstring_view executable, std::wstring_view arguments,
                          std::wstring_view working_dir, const CancelToken& cancel,
                          const OutputSink& on_line, std::size_t read_buffer_bytes = 64 * 1024);

// Version sin redireccion: solo espera al proceso. Para comandos rapidos.
ProcessResult RunWait(std::wstring_view executable, std::wstring_view arguments,
                      const CancelToken& cancel);

// --- Localizacion de adb ----------------------------------------------------

// Busca adb.exe en: la ruta del propio ejecutable, ANDROID_SDK_ROOT,
// ANDROID_HOME, platform-tools del SDK conocido y, en ultimo lugar, el PATH.
// El resultado se cachea: se llama en cada refresco de la lista de dispositivos.
[[nodiscard]] std::optional<std::wstring> FindAdb();
void ClearAdbCache();

// --- Cadenas de Windows -----------------------------------------------------

// UTF-8 -> UTF-16. Es la conversion que mas se olvida y la fuente clasica de
// caracteres raros en rutas y mensajes de adb.
[[nodiscard]] std::wstring Utf8ToWide(std::string_view utf8);
[[nodiscard]] std::string WideToUtf8(std::wstring_view wide);

// Construye una linea de comandos con el escapado correcto de CommandLineToArgvW.
// Sin esto, un serial o una ruta con espacios rompe la invocacion en silencio.
[[nodiscard]] std::wstring BuildCommandLine(std::wstring_view executable,
                                            std::vector<std::wstring_view> arguments);

// Solo los argumentos, sin ejecutable. Se pasa a RunCaptured, que ya recibe el
// ejecutable por separado.
[[nodiscard]] std::wstring BuildArgumentsLine(std::vector<std::wstring_view> arguments);

// Segundos desde 1970 (UTC). El nucleo no puede calcularlo porque no depende
// del reloj del sistema; lo hace la capa de plataforma.
[[nodiscard]] std::int64_t UnixNow() noexcept;

// --- Rutas de datos de la aplicacion ---------------------------------------

// %LOCALAPPDATA%\D.A.S.O  (creada si hace falta)
[[nodiscard]] std::wstring AppDataDir();
[[nodiscard]] std::wstring ManifestDir();   // AppDataDir\manifests
[[nodiscard]] std::wstring ProfilesDir();   // AppDataDir\profiles
[[nodiscard]] std::wstring LogsDir();       // AppDataDir\logs

// Crea un directorio y todos sus antecesores. Devuelve false si ya no se pudo.
bool EnsureDirectory(const std::wstring& path);

// Directorio del propio ejecutable (sin barra final).
[[nodiscard]] std::wstring ExeDirectory();

[[nodiscard]] std::optional<std::int64_t> FileModifiedEpoch(const std::wstring& path);

// true si el proceso actual corre con privilegios de administrador.
// Solo para AVISAR: D.A.S.O no necesita administrador y nunca se eleva.
[[nodiscard]] bool IsElevated();

// Instancia unica por usuario. Devuelve false si ya hay otra ventana abierta.
[[nodiscard]] bool AcquireSingleInstance(const wchar_t* name);
void ReleaseSingleInstance();

}  // namespace daso::win
