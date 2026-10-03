#include "daso/ui/ui_common.hpp"

#include <algorithm>
#include <cstring>
#include <map>

namespace daso::ui {
namespace {

struct Entry {
  std::string_view key;
  std::string_view es;
  std::string_view en;
};

// Todas las cadenas visibles de la app. Anadir una aqui es lo unico que hace
// que aparezca en los dos idiomas.
constexpr std::array<Entry, 46> kStrings{{
    {"app.title", "D.A.S.O — Debloater", "D.A.S.O — Debloater"},
    {"app.subtitle", "Debloater Android Script Optimizer",
     "Debloater Android Script Optimizer"},

    {"common.close", "Cerrar", "Close"},
    {"common.cancel", "Cancelar", "Cancel"},
    {"common.save", "Guardar", "Save"},
    {"common.yes", "Sí", "Yes"},

    {"device.none", "Sin dispositivo conectado", "No device connected"},
    {"device.refreshing", "Buscando dispositivos…", "Looking for devices…"},
    {"device.label", "Dispositivo", "Device"},
    {"device.offline", "Sin conexión", "Offline"},
    {"device.unauthorized", "Sin autorizar (acepta el aviso en el móvil)",
     "Unauthorized (accept the prompt on the phone)"},
    {"device.model_unknown", "modelo desconocido", "unknown model"},

    {"search.placeholder", "Buscar paquete o descripción…",
     "Search package or description…"},
    {"search.no_results", "Ningún paquete coincide", "No package matches"},

    {"select.all", "Seleccionar todo", "Select all"},
    {"select.none", "No seleccionar nada", "Clear selection"},
    {"select.safe_only", "Solo seguros", "Safe only"},
    {"select.invert", "Invertir", "Invert"},
    {"select.count", "{} de {} seleccionados", "{} of {} selected"},

    {"safe_mode.on", "Modo seguro: solo se seleccionan los paquetes sin riesgo.",
     "Safe mode: only risk-free packages are selected."},
    {"safe_mode.off", "Precaución: has salido del modo seguro.",
     "Caution: you have left safe mode."},
    {"safe_mode.toggle", "Modo seguro", "Safe mode"},

    {"tier.safe", "Seguro", "Safe"},
    {"tier.caution", "Precaución", "Caution"},
    {"tier.vital", "Vital", "Vital"},
    {"tier.summary", "{} seguros · {} con precaución · {} bloqueados",
     "{} safe · {} caution · {} blocked"},

    {"action.verify", "Comprobar dispositivo", "Check device"},
    {"action.sync", "Leer estado del dispositivo", "Read device state"},
    {"action.disable", "Deshabilitar seleccionados", "Disable selected"},
    {"action.enable", "Reactivar todo", "Re-enable all"},
    {"action.restore", "Revertir último cambio", "Undo last change"},
    {"action.dry_run", "Simular (no cambia nada)", "Dry run (changes nothing)"},
    {"action.save_log", "Guardar registro", "Save log"},

    {"confirm.title", "Confirmar", "Confirm"},
    {"confirm.body", "Se {} {} paquetes en {}. ¿Continuar?",
     "{} {} packages on {}. Continue?"},
    {"confirm.disable", "deshabilitarán", "will disable"},
    {"confirm.enable", "reactivarán", "will re-enable"},
    {"confirm.danger", "Esta acción se puede deshacer con «Revertir último cambio».",
     "This can be undone with “Undo last change”."},

    {"status.ready", "Listo", "Ready"},
    {"status.working", "Trabajando…", "Working…"},
    {"status.done", "{} ok · {} fallidos · {} viajes a adb",
     "{} ok · {} failed · {} adb runs"},
    {"status.cancelled", "Cancelado", "Cancelled"},
    {"status.no_adb", "No se encontró adb. Instala platform-tools o añade adb al PATH.",
     "adb not found. Install platform-tools or add adb to PATH."},
    {"status.elevated",
     "Nota: se está ejecutando como administrador. D.A.S.O no lo necesita.",
     "Note: running elevated. D.A.S.O does not need administrator rights."},
    {"status.undo_missing", "No hay ningún cambio previo que revertir.",
     "There is no previous change to undo."},
}};

Language g_language = Language::Spanish;

// Conversion UTF-8 -> UTF-16 cacheada por clave e idioma. La tabla es estatica
// y solo se traduce una vez por clave, no en cada repintado.
std::map<std::string_view, std::wstring> g_cache_es;
std::map<std::string_view, std::wstring> g_cache_en;

}  // namespace

Language DetectLanguage() {
  wchar_t locale[LOCALE_NAME_MAX_LENGTH]{};
  const int n = GetUserDefaultLocaleName(locale, LOCALE_NAME_MAX_LENGTH);
  if (n <= 0) return Language::Spanish;
  // es-ES, es-MX, es-419... cualquier cosa que empiece por "es".
  return (locale[0] == L'e' && locale[1] == L's') ? Language::Spanish : Language::English;
}

void SetLanguage(Language language) noexcept { g_language = language; }
Language CurrentLanguage() noexcept { return g_language; }

std::wstring_view T(Language lang, std::string_view key) {
  for (const Entry& e : kStrings) {
    if (e.key == key) {
      const std::string_view utf8 = (lang == Language::Spanish) ? e.es : e.en;
      // La tabla es UTF-8 estatico; la conversion se hace una vez y se
      // guarda en el propio slot (el array es mutable y de interior const).
      auto& cache = (lang == Language::Spanish) ? g_cache_es : g_cache_en;
      auto it = cache.find(key);
      if (it != cache.end()) return it->second;

      const int needed = MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                                             static_cast<int>(utf8.size()), nullptr, 0);
      if (needed <= 0) return {};
      std::wstring wide(static_cast<std::size_t>(needed), L'\0');
      MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
                          wide.data(), needed);
      return cache.emplace(key, std::move(wide)).first->second;
    }
  }
  return {};
}

std::wstring_view T(std::string_view key) { return T(g_language, key); }

}  // namespace daso::ui
