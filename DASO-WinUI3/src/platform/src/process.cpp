#include "daso/process.hpp"

#include <algorithm>
#include <cstring>
#include <mutex>
#include <vector>

namespace daso::win {
namespace {

// RAII para handles de kernel. Cerrar a mano en cada camino de error es la
// forma mas rapida de agotar handles en una app que lanza procesos.
class UniqueHandle {
 public:
  UniqueHandle() = default;
  explicit UniqueHandle(HANDLE h) noexcept : h_(h) {}
  UniqueHandle(const UniqueHandle&) = delete;
  UniqueHandle& operator=(const UniqueHandle&) = delete;
  UniqueHandle(UniqueHandle&& o) noexcept : h_(o.h_) { o.h_ = nullptr; }
  UniqueHandle& operator=(UniqueHandle&& o) noexcept {
    if (this != &o) {
      reset();
      h_ = o.h_;
      o.h_ = nullptr;
    }
    return *this;
  }
  ~UniqueHandle() { reset(); }

  void reset(HANDLE h = nullptr) noexcept {
    if (h_ != nullptr && h_ != INVALID_HANDLE_VALUE) CloseHandle(h_);
    h_ = h;
  }
  [[nodiscard]] HANDLE get() const noexcept { return h_; }
  [[nodiscard]] bool valid() const noexcept {
    return h_ != nullptr && h_ != INVALID_HANDLE_VALUE;
  }

 private:
  HANDLE h_ = nullptr;
};

std::mutex g_adb_mutex;
std::optional<std::wstring> g_adb_path;
std::mutex g_instance_mutex;
HANDLE g_instance_mutex_handle = nullptr;

constexpr std::wstring_view kAdbName = L"adb.exe";

std::optional<std::wstring> SearchAdbIn(const std::wstring& dir) {
  if (dir.empty()) return std::nullopt;
  std::wstring candidate = dir;
  if (candidate.back() != L'\\') candidate += L'\\';
  candidate += kAdbName;
  const DWORD attrs = GetFileAttributesW(candidate.c_str());
  if (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
    return candidate;
  }
  return std::nullopt;
}

std::optional<std::wstring> SearchPathEnv() {
  const DWORD needed = SearchPathW(nullptr, std::wstring(kAdbName).c_str(), nullptr,
                                   MAX_PATH, nullptr, nullptr);
  if (needed == 0 || needed > MAX_PATH) return std::nullopt;
  std::vector<wchar_t> buffer(needed + 1);
  const DWORD written = SearchPathW(nullptr, std::wstring(kAdbName).c_str(), nullptr,
                                     static_cast<DWORD>(buffer.size()), buffer.data(),
                                     nullptr);
  if (written == 0) return std::nullopt;
  return std::wstring(buffer.data(), written);
}

std::wstring EnvVar(const wchar_t* name) {
  const DWORD needed = GetEnvironmentVariableW(name, nullptr, 0);
  if (needed == 0) return {};
  std::vector<wchar_t> buffer(needed + 1);
  const DWORD written = GetEnvironmentVariableW(name, buffer.data(),
                                                static_cast<DWORD>(buffer.size()));
  if (written == 0) return {};
  return std::wstring(buffer.data(), written);
}

// Separa una linea de texto sin asignar: devuelve el salto y actualiza el
// inicio. Es el nucleo del "cero asignaciones por linea de log".
inline const char* FindNewline(const char* begin, const char* end) noexcept {
  return static_cast<const char*>(std::memchr(begin, '\n', static_cast<std::size_t>(end - begin)));
}

}  // namespace

// ---------------------------------------------------------------------------
// Cadenas
// ---------------------------------------------------------------------------

std::wstring Utf8ToWide(std::string_view utf8) {
  if (utf8.empty()) return {};
  const int needed = MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                                         static_cast<int>(utf8.size()), nullptr, 0);
  if (needed <= 0) return {};

  std::wstring out(static_cast<std::size_t>(needed), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(),
                      needed);
  return out;
}

std::string WideToUtf8(std::wstring_view wide) {
  if (wide.empty()) return {};
  const int needed = WideCharToMultiByte(CP_UTF8, 0, wide.data(),
                                         static_cast<int>(wide.size()), nullptr, 0,
                                         nullptr, nullptr);
  if (needed <= 0) return {};

  std::string out(static_cast<std::size_t>(needed), '\0');
  WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), out.data(),
                      needed, nullptr, nullptr);
  return out;
}

std::wstring BuildArgumentsLine(std::vector<std::wstring_view> arguments) {
  std::wstring line;
  std::size_t total = 0;
  for (std::wstring_view a : arguments) total += a.size() + 1;
  line.reserve(total + 16);

  // Reglas de CommandLineToArgvW: se duplican las barras invertidas que
  // preceden a una comilla, y la propia comilla se escapa con una barra.
  // Sin esto, una ruta con espacios se parte en dos argumentos en silencio.
  const auto quote = [&line](std::wstring_view s) {
    line += L'"';
    std::size_t backslashes = 0;
    for (const wchar_t c : s) {
      if (c == L'\\') {
        ++backslashes;
        continue;
      }
      if (c == L'"') {
        line.append(backslashes * 2 + 1, L'\\');
        backslashes = 0;
      } else {
        backslashes = 0;
      }
      line += c;
    }
    line.append(backslashes * 2, L'\\');
    line += L'"';
  };

  const auto needs_quotes = [](std::wstring_view s) {
    return s.empty() || s.find_first_of(L" \t\n\v\"") != std::wstring_view::npos;
  };

  for (std::wstring_view arg : arguments) {
    if (!line.empty()) line += L' ';
    if (needs_quotes(arg)) {
      quote(arg);
    } else {
      line += arg;
    }
  }
  return line;
}

std::wstring BuildCommandLine(std::wstring_view executable,
                              std::vector<std::wstring_view> arguments) {
  std::wstring line;
  const std::string_view::size_type needs_quotes =
      executable.empty() || executable.find_first_of(L" \t\n\v\"") != std::wstring_view::npos;

  if (needs_quotes) {
    line += L'"';
    line += executable;
    line += L'"';
  } else {
    line += executable;
  }

  const std::wstring args = BuildArgumentsLine(std::move(arguments));
  if (!args.empty()) {
    line += L' ';
    line += args;
  }
  return line;
}

// ---------------------------------------------------------------------------
// Ejecucion
// ---------------------------------------------------------------------------

namespace {

// Estado de una lectura solapada. Se reutiliza entre iteraciones: el bucle no
// reserva memoria una vez que la primera lectura ha fijado la capacidad.
struct ReadState {
  OVERLAPPED ov{};
  std::vector<char> buffer;
  bool pending = false;
  DWORD transferred = 0;
};

}  // namespace

ProcessResult RunCaptured(std::wstring_view executable, std::wstring_view arguments,
                          std::wstring_view working_dir, const CancelToken& cancel,
                          const OutputSink& on_line, std::size_t read_buffer_bytes) {
  ProcessResult result;
  if (read_buffer_bytes < 4096) read_buffer_bytes = 4096;

  // --- Tuberias ------------------------------------------------------------
  // Se pide una unica tuberia: stdout y stderr se mezclan a proposito. adb
  // escribe los errores por stderr y separar ambos exigiria dos IOCP o
  // multiplexar el par de handles en una sola asociacion.
  UniqueHandle child_stdout, parent_read;
  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof sa;
  sa.bInheritHandle = TRUE;

  // CreatePipe necesita direcciones de variables reales: UniqueHandle::get()
  // devuelve por valor y no admite &.
  HANDLE raw_read = nullptr;
  HANDLE raw_write = nullptr;
  if (!CreatePipe(&raw_read, &raw_write, &sa, 0)) {
    result.error = GetLastError();
    return result;
  }
  parent_read.reset(raw_read);
  child_stdout.reset(raw_write);

  // Solo el extremo de escritura llega al hijo. El extremo de lectura se marca
  // como no heredable; si el hijo lo heredara, nunca veria EOF porque su propia
  // copia seguiria abierta.
  SetHandleInformation(parent_read.get(), HANDLE_FLAG_INHERIT, 0);

  STARTUPINFOW si{};
  si.cb = sizeof si;
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdOutput = child_stdout.get();
  si.hStdError = child_stdout.get();
  si.hStdInput = nullptr;

  // --- Lista de handles heredables ----------------------------------------
  // PROC_THREAD_ATTRIBUTE_LIST es la forma correcta de decir "el hijo solo hereda
  // ESTE handle". Sin ella hay una carrera: el hijo puede heredar un handle que
  // el padre todavia no ha cerrado, y el recurso no se libera nunca.
  std::vector<char> attr_storage;
  LPPROC_THREAD_ATTRIBUTE_LIST attr_list = nullptr;
  bool attr_ok = false;

  SIZE_T attr_size = 0;
  if (InitializeProcThreadAttributeList(nullptr, 1, 0, &attr_size) == FALSE &&
      GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
    attr_storage.resize(attr_size);
    attr_list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attr_storage.data());
    attr_ok = InitializeProcThreadAttributeList(attr_list, 1, 0, &attr_size) != FALSE;
  }
  if (attr_ok) {
    HANDLE to_inherit[] = {child_stdout.get()};
    attr_ok = UpdateProcThreadAttribute(attr_list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                       to_inherit, sizeof to_inherit, nullptr,
                                       nullptr) != FALSE;
  }

  // CREATE_SUSPENDED: el hijo no ejecuta nada hasta que el Job este asignado.
  // Si nace corriendo, puede hacer daño antes de que exista forma de pararlo.
  DWORD flags = CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED;
  STARTUPINFOEXW sie{};
  STARTUPINFOW* startup = &si;
  if (attr_ok) {
    flags |= EXTENDED_STARTUPINFO_PRESENT;
    sie.StartupInfo = si;
    sie.lpAttributeList = attr_list;
    startup = reinterpret_cast<STARTUPINFOW*>(&sie);
  }

  const std::wstring command = BuildCommandLine(executable, {arguments});
  std::vector<wchar_t> mutable_cmd(command.begin(), command.end());
  mutable_cmd.push_back(L'\0');

  UniqueHandle process, thread;
  PROCESS_INFORMATION pi{};
  if (!CreateProcessW(std::wstring(executable).c_str(), mutable_cmd.data(), nullptr,
                      nullptr, TRUE, flags, nullptr,
                      working_dir.empty() ? nullptr : working_dir.data(), startup, &pi)) {
    result.error = GetLastError();
    if (attr_list) DeleteProcThreadAttributeList(attr_list);
    return result;
  }
  if (attr_list) DeleteProcThreadAttributeList(attr_list);
  process.reset(pi.hProcess);
  thread.reset(pi.hThread);
  result.launched = true;

  // Kill-on-close: si la app se cierra o el usuario cancela, cae el arbol
  // entero de adb en vez de dejar procesos huerfanos.
  UniqueHandle job;
  if (CreateJobObjectW(nullptr, nullptr)) {
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation, &limits,
                                sizeof limits) != FALSE) {
      if (AssignProcessToJobObject(job.get(), process.get()) == FALSE) job.reset();
    } else {
      job.reset();
    }
  }

  // El padre ya no escribe en la salida del hijo: cerrarlo aqui es lo que
  // permite al hijo recibir EOF cuando termine.
  child_stdout.reset();

  // --- E/S asincrona --------------------------------------------------------
  constexpr ULONG_PTR kReadKey = 1;
  constexpr ULONG_PTR kProcessKey = 2;

  const HANDLE iocp = CreateIoCompletionPort(parent_read.get(), nullptr, kReadKey, 1);
  const bool iocp_ok = iocp != nullptr;
  UniqueHandle iocp_handle(iocp_ok ? iocp : nullptr);
  if (iocp_ok) CreateIoCompletionPort(process.get(), iocp, kProcessKey, 1);

  ReadState read;
  read.buffer.resize(read_buffer_bytes);
  std::string line;  // se reutiliza; ninguna linea reserva memoria tras elwarmup
  line.reserve(512);

  bool eof = false;
  bool process_done = !iocp_ok;  // sin IOCP se cae al modo bloqueante simple

  if (iocp_ok) {
    read.ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    UniqueHandle read_event(read.ov.hEvent);
  }

  const auto pump_read = [&](bool blocking) -> bool {
    // Devuelve true si hubo datos que procesar.
    if (eof) return false;
    if (!read.pending) {
      ResetEvent(read.ov.hEvent);
      DWORD transferred = 0;
      const BOOL ok = ReadFile(parent_read.get(), read.buffer.data(),
                               static_cast<DWORD>(read.buffer.size()), &transferred,
                               &read.ov);
      if (ok) {
        read.pending = false;
        read.transferred = transferred;
      } else if (GetLastError() == ERROR_IO_PENDING) {
        read.pending = true;
        (void)blocking;
      } else {
        eof = true;
        return false;
      }
    }
    if (read.pending) return false;  // se recoge en la proxima iteracion

    // Trocear por saltos de linea sin copiar: se localize en el buffer de
    // lectura y se copia solo el trozo que forma una linea completa.
    const char* begin = read.buffer.data();
    const char* end = begin + read.transferred;
    while (begin < end) {
      const char* nl = FindNewline(begin, end);
      if (nl == nullptr) {
        line.append(begin, static_cast<std::size_t>(end - begin));
        break;
      }
      line.append(begin, static_cast<std::size_t>(nl - begin));
      if (!line.empty() && line.back() == '\r') line.pop_back();
      if (on_line) on_line(std::string_view(line));
      line.clear();
      begin = nl + 1;
    }

    if (read.transferred == 0) {
      eof = true;
      // Ultima linea sin salto final.
      if (!line.empty()) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (on_line) on_line(std::string_view(line));
        line.clear();
      }
    }
    return true;
  };

  // Primera lectura y reanudacion: el proceso no corre hasta que todo lo de
  // arriba esta listo.
  if (iocp_ok) pump_read(false);
  if (thread.valid()) ResumeThread(thread.get());

  // --- Bucle de espera -----------------------------------------------------
  // Un unico hilo para todo el trafico de adb. El timeout corto existe porque
  // el CancelToken del nucleo es un flag de usuario, no un evento del kernel:
  // consultar el flag es lo que permite cortar un lote en ~100 ms.
  constexpr DWORD kPollMs = 100;
  DWORD exit_code = 0;

  if (iocp_ok) {
    while (!process_done) {
      DWORD transferred = 0;
      ULONG_PTR key = 0;
      OVERLAPPED* overlapped = nullptr;

      const DWORD timeout = cancel.IsCancelled() ? 0 : kPollMs;
      const BOOL ok =
          GetQueuedCompletionStatus(iocp, &transferred, &key, &overlapped, timeout);

      if (overlapped != nullptr) {
        read.pending = false;
        if (ok) {
          read.transferred = transferred;
          pump_read(false);
        } else if (GetLastError() == ERROR_BROKEN_PIPE) {
          eof = true;
        }
        continue;
      }

      if (ok && key == kProcessKey) {
        process_done = true;
        break;
      }

      if (cancel.IsCancelled()) {
        result.terminated = true;
        if (job.valid()) {
          TerminateJobObject(job.get(), 1);
        } else if (process.valid()) {
          TerminateProcess(process.get(), 1);
        }
        WaitForSingleObject(process.get(), 2000);
        break;
      }

      // Comprobacion de seguridad: si se perdio la notificacion, no nos
      // quedamos esperando para siempre.
      if (WaitForSingleObject(process.get(), 0) == WAIT_OBJECT_0) {
        process_done = true;
        break;
      }
    }

    // Drenar: el proceso ya salio pero puede quedar salida sin entregar.
    while (!eof) {
      DWORD transferred = 0;
      ULONG_PTR key = 0;
      OVERLAPPED* overlapped = nullptr;
      const BOOL ok =
          GetQueuedCompletionStatus(iocp, &transferred, &key, &overlapped, 2000);
      if (overlapped == nullptr) break;
      read.pending = false;
      if (!ok) {
        if (GetLastError() == ERROR_BROKEN_PIPE) eof = true;
        continue;
      }
      read.transferred = transferred;
      pump_read(false);
    }
  } else {
    // Sin IOCP no deberiamos llegar aqui, pero mejor un camino lento que
    // quedarse colgado.
    WaitForSingleObject(process.get(), INFINITE);
  }

  if (!result.terminated && WaitForSingleObject(process.get(), 0) == WAIT_OBJECT_0) {
    GetExitCodeProcess(process.get(), &exit_code);
  }
  result.exit_code = exit_code;
  return result;
}

ProcessResult RunWait(std::wstring_view executable, std::wstring_view arguments,
                      const CancelToken& cancel) {
  ProcessResult result;
  const std::wstring command = BuildCommandLine(executable, {arguments});
  std::vector<wchar_t> mutable_cmd(command.begin(), command.end());
  mutable_cmd.push_back(L'\0');

  PROCESS_INFORMATION pi{};
  if (!CreateProcessW(std::wstring(executable).c_str(), mutable_cmd.data(), nullptr,
                      nullptr, FALSE, CREATE_UNICODE_ENVIRONMENT, nullptr, nullptr,
                      nullptr, &pi)) {
    result.error = GetLastError();
    return result;
  }
  UniqueHandle process(pi.hProcess), thread(pi.hThread);
  result.launched = true;

  UniqueHandle job;
  if (CreateJobObjectW(nullptr, nullptr)) {
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation, &limits,
                                sizeof limits) != FALSE &&
        AssignProcessToJobObject(job.get(), process.get()) == FALSE) {
      job.reset();
    }
  }

  ResumeThread(thread.get());

  constexpr DWORD kPollMs = 100;
  for (;;) {
    if (cancel.IsCancelled()) {
      result.terminated = true;
      if (job.valid()) TerminateJobObject(job.get(), 1);
      WaitForSingleObject(process.get(), 2000);
      break;
    }
    if (WaitForSingleObject(process.get(), kPollMs) == WAIT_OBJECT_0) break;
  }

  if (!result.terminated && WaitForSingleObject(process.get(), 0) == WAIT_OBJECT_0) {
    GetExitCodeProcess(process.get(), &result.exit_code);
  }
  return result;
}

// ---------------------------------------------------------------------------
// Localizacion de adb
// ---------------------------------------------------------------------------

std::optional<std::wstring> FindAdb() {
  std::lock_guard<std::mutex> lock(g_adb_mutex);
  if (g_adb_path) return g_adb_path;

  // 1. Junto al propio ejecutable: es donde va el paquete desplegado.
  if (auto hit = SearchAdbIn(ExeDirectory())) {
    g_adb_path = *hit;
    return g_adb_path;
  }

  // 2. Variables de entorno del SDK.
  for (const wchar_t* var : {L"ANDROID_SDK_ROOT", L"ANDROID_HOME", L"ANDROID_SDK_HOME"}) {
    if (auto hit = SearchAdbIn(EnvVar(var) + L"\\platform-tools")) {
      g_adb_path = *hit;
      return g_adb_path;
    }
  }

  // 3. Rutas habituales del SDK de Google en Windows.
  if (auto hit = SearchAdbIn(L"C:\\Android\\platform-tools")) {
    g_adb_path = *hit;
    return g_adb_path;
  }
  if (auto hit = SearchAdbIn(EnvVar(L"LOCALAPPDATA") + L"\\Android\\Sdk\\platform-tools")) {
    g_adb_path = *hit;
    return g_adb_path;
  }

  // 4. El PATH, que es lo que hacia la version en Python.
  if (auto hit = SearchPathEnv()) {
    g_adb_path = *hit;
    return g_adb_path;
  }

  return std::nullopt;
}

void ClearAdbCache() {
  std::lock_guard<std::mutex> lock(g_adb_mutex);
  g_adb_path.reset();
}

// ---------------------------------------------------------------------------
// Rutas y estado del proceso
// ---------------------------------------------------------------------------

std::wstring ExeDirectory() {
  std::vector<wchar_t> buffer(MAX_PATH);
  for (;;) {
    const DWORD written = GetModuleFileNameW(nullptr, buffer.data(),
                                              static_cast<DWORD>(buffer.size()));
    if (written == 0) return {};
    if (written < buffer.size()) {
      std::wstring path(buffer.data(), written);
      const std::size_t slash = path.find_last_of(L"\\/");
      return slash == std::wstring::npos ? std::wstring{} : path.substr(0, slash);
    }
    buffer.resize(buffer.size() * 2);
  }
}

std::int64_t UnixNow() noexcept {
  FILETIME ft{};
  GetSystemTimeAsFileTime(&ft);
  ULARGE_INTEGER ticks{};
  ticks.LowPart = ft.dwLowDateTime;
  ticks.HighPart = ft.dwHighDateTime;
  // FILETIME cuenta intervalos de 100 ns desde 1601; Unix epoch son 1970.
  return static_cast<std::int64_t>(ticks.QuadPart / 10'000'000ULL) - 11'644'473'600LL;
}

std::wstring AppDataDir() {
  std::wstring base = EnvVar(L"LOCALAPPDATA");
  if (base.empty()) base = ExeDirectory();
  return base + L"\\D.A.S.O";
}

std::wstring ManifestDir() { return AppDataDir() + L"\\manifests"; }
std::wstring ProfilesDir() { return AppDataDir() + L"\\profiles"; }
std::wstring LogsDir() { return AppDataDir() + L"\\logs"; }

bool EnsureDirectory(const std::wstring& path) {
  if (path.empty()) return false;
  const DWORD attrs = GetFileAttributesW(path.c_str());
  if (attrs != INVALID_FILE_ATTRIBUTES) return (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;

  // Crear de abajo arriba: CreateDirectory no crea los antecesores.
  std::size_t slash = path.find_last_of(L"\\/");
  if (slash != std::wstring::npos && slash > 0) {
    if (!EnsureDirectory(path.substr(0, slash))) return false;
  }
  if (CreateDirectoryW(path.c_str(), nullptr)) return true;
  return GetLastError() == ERROR_ALREADY_EXISTS;
}

std::optional<std::int64_t> FileModifiedEpoch(const std::wstring& path) {
  WIN32_FILE_ATTRIBUTE_DATA data{};
  if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) {
    return std::nullopt;
  }
  ULARGE_INTEGER ticks{};
  ticks.LowPart = data.ftLastWriteTime.dwLowDateTime;
  ticks.HighPart = data.ftLastWriteTime.dwHighDateTime;
  // FILETIME -> epoch Unix: delta de 11644473600 segundos.
  return static_cast<std::int64_t>(ticks.QuadPart / 10'000'000ULL) - 11'644'473'600LL;
}

bool IsElevated() {
  // D.A.S.O no necesita administrador. Esto solo existe para poder avisar.
  HANDLE token = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
  UniqueHandle token_handle(token);

  TOKEN_ELEVATION elevation{};
  DWORD size = sizeof elevation;
  const BOOL ok = GetTokenInformation(token, TokenElevation, &elevation, size, &size);
  return ok && elevation.TokenIsElevated != 0;
}

bool AcquireSingleInstance(const wchar_t* name) {
  std::lock_guard<std::mutex> lock(g_instance_mutex);
  if (g_instance_mutex_handle != nullptr) return true;

  HANDLE handle = CreateMutexW(nullptr, TRUE, name);
  if (handle == nullptr) return false;
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    // Ya hay otra instancia: avisamos y salimos, sin tocar su mutex.
    CloseHandle(handle);
    return false;
  }
  g_instance_mutex_handle = handle;
  return true;
}

void ReleaseSingleInstance() {
  std::lock_guard<std::mutex> lock(g_instance_mutex);
  if (g_instance_mutex_handle != nullptr) {
    CloseHandle(g_instance_mutex_handle);
    g_instance_mutex_handle = nullptr;
  }
}

}  // namespace daso::win
