#include "daso/manifest_store.hpp"

#include <algorithm>
#include <cstdio>

#include "daso/process.hpp"

namespace daso::win {
namespace {

// Escribe un fichero de texto en UTF-8. CreateFileW en vez de ofstream para no
// depender de la anchura del locale del proceso.
bool WriteTextFile(const std::wstring& path, const std::string& utf8) {
  HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;

  std::size_t written = 0;
  const char* data = utf8.data();
  const std::size_t total = utf8.size();
  bool ok = true;
  while (written < total) {
    DWORD chunk = 0;
    const std::size_t remaining = total - written;
    const DWORD to_write =
        static_cast<DWORD>(remaining > 1u << 20 ? (1u << 20) : remaining);
    if (!WriteFile(file, data + written, to_write, &chunk, nullptr)) {
      ok = false;
      break;
    }
    written += chunk;
  }
  CloseHandle(file);
  return ok;
}

bool EndsWithNoCase(std::wstring_view text, std::wstring_view suffix) {
  if (suffix.size() > text.size()) return false;
  return _wcsnicmp(text.data() + (text.size() - suffix.size()), suffix.data(),
                   suffix.size()) == 0;
}

// Prefijo del nombre: "<serial>_" o "" si el manifiesto es de otro dispositivo.
std::wstring_view SerialPrefix(std::wstring_view filename, std::wstring_view serial) {
  if (serial.empty()) return {};
  if (filename.size() <= serial.size() + 1) return {};
  if (_wcsnicmp(filename.data(), serial.data(), serial.size()) != 0) return {};
  if (filename[serial.size()] != L'_') return {};
  return filename.substr(0, serial.size());
}

}  // namespace

std::string ManifestNameFor(std::string_view serial, std::int64_t now_utc) {
  return ManifestFileName(serial, FormatCompactUtc(now_utc));
}

std::optional<std::wstring> SaveManifest(const Manifest& manifest,
                                         std::int64_t now_utc) {
  const std::wstring dir = ManifestDir();
  if (!EnsureDirectory(dir)) return std::nullopt;

  const std::string name = ManifestNameFor(manifest.serial, now_utc);
  const std::wstring path = dir + L"\\" + Utf8ToWide(name);
  const std::string json = SerializeManifest(manifest);
  if (!WriteTextFile(path, json)) return std::nullopt;
  return path;
}

std::optional<std::wstring> LatestManifestPath(std::string_view serial) {
  const std::wstring dir = ManifestDir();

  WIN32_FIND_DATAW find{};
  const std::wstring pattern = dir + L"\\*.json";
  HANDLE handle = FindFirstFileW(pattern.c_str(), &find);
  if (handle == INVALID_HANDLE_VALUE) return std::nullopt;

  std::wstring best_name;
  ULARGE_INTEGER best_time{};
  best_time.QuadPart = 0;
  const std::wstring serial_wide = Utf8ToWide(serial);

  do {
    if (EndsWithNoCase(find.cFileName, L".json")) {
      // Prefijo de otro dispositivo: no es el nuestro.
      if (!SerialPrefix(find.cFileName, serial_wide).empty()) continue;

      // FILETIME es un struct de dos DWORD, no convertible a ULARGE_INTEGER.
      ULARGE_INTEGER t{};
      t.LowPart = find.ftLastWriteTime.dwLowDateTime;
      t.HighPart = find.ftLastWriteTime.dwHighDateTime;

      if (best_name.empty() || t.QuadPart > best_time.QuadPart) {
        best_name = find.cFileName;
        best_time = t;
      }
    }
  } while (FindNextFileW(handle, &find));

  FindClose(handle);
  if (best_name.empty()) return std::nullopt;
  return dir + L"\\" + best_name;
}

std::optional<Manifest> LoadLatestManifest(std::string_view serial) {
  const auto path = LatestManifestPath(serial);
  if (!path) return std::nullopt;

  HANDLE file = CreateFileW(path->c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return std::nullopt;

  LARGE_INTEGER size{};
  GetFileSizeEx(file, &size);
  std::string text;
  text.resize(static_cast<std::size_t>(size.QuadPart));

  std::size_t read = 0;
  const BOOL ok = ReadFile(file, text.data(), static_cast<DWORD>(text.size()),
                           reinterpret_cast<DWORD*>(&read), nullptr);
  CloseHandle(file);
  if (!ok) return std::nullopt;
  text.resize(read);

  auto parsed = ParseManifest(text);
  if (!parsed) return std::nullopt;
  return *parsed;
}

Manifest BuildManifest(std::string_view serial, PackageAction action,
                       std::int64_t now_utc, std::span<const PackageEntry> catalog,
                       std::span<const PlannedItem> planned,
                       std::span<const PackageState> before) {
  Manifest m;
  m.serial = std::string(serial);
  m.timestamp_utc = FormatIso8601Utc(now_utc);
  m.action = action;
  m.entries.reserve(planned.size());

  for (const PlannedItem& item : planned) {
    if (item.index >= catalog.size()) continue;
    ManifestEntry e;
    e.package = std::string(catalog[item.index].name);
    e.before = item.index < before.size() ? before[item.index] : PackageState::Unknown;
    // Si el paquete no estaba instalado, deshabilitarlo no cambia nada y no
    // tiene sentido guardarlo como aplicado.
    if (action == PackageAction::Disable && e.before == PackageState::Absent) {
      e.after = PackageState::Absent;
    } else {
      e.after = action == PackageAction::Disable ? PackageState::Disabled
                                                 : PackageState::Enabled;
    }
    m.entries.push_back(std::move(e));
  }
  return m;
}

std::vector<PlannedItem> PlanFromManifest(const Manifest& manifest,
                                          std::span<const PackageEntry> catalog) {
  // Deshacer una deshabilitacion es habilitar; y al reves.
  const PackageAction inverse =
      manifest.action == PackageAction::Disable ? PackageAction::Enable
                                                : PackageAction::Disable;

  std::vector<PlannedItem> planned;
  planned.reserve(manifest.entries.size());

  for (const ManifestEntry& e : manifest.entries) {
    if (e.after == PackageState::Absent) continue;  // nunca se toco
    const std::size_t index = FindIndex(e.package);
    if (index == catalog.size()) continue;          // ya no esta en el catalogo
    planned.push_back(PlannedItem{index, inverse});
  }
  return planned;
}

}  // namespace daso::win
