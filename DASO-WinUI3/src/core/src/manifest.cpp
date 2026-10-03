#include "daso/manifest.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <memory>

namespace daso {
namespace {

// ---------------------------------------------------------------------------
// Escritura
// ---------------------------------------------------------------------------

void AppendEscaped(std::string& out, std::string_view s) {
  out += '"';
  for (const char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      default:
        // El resto se emite tal cual: el manifiesto se guarda en UTF-8 y los
        // nombres de paquete son ASCII.
        if (static_cast<unsigned char>(c) < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof buf, "\\u%04x", static_cast<unsigned>(c) & 0xFF);
          out += buf;
        } else {
          out += c;
        }
    }
  }
  out += '"';
}

void AppendKey(std::string& out, std::string_view key) {
  AppendEscaped(out, key);
  out += ": ";
}

// ---------------------------------------------------------------------------
// Lectura: JSON minimo
//
// Solo hace falta un esquema fijo, pero el parser es generico para no tener
// que Assumeir el formato de entrada.
// ---------------------------------------------------------------------------

struct JValue;
using JPtr = std::unique_ptr<JValue>;

struct JValue {
  enum class Kind { Null, Bool, Number, String, Array, Object };
  Kind kind = Kind::Null;
  bool boolean = false;
  double number = 0.0;
  std::string string;
  std::vector<JPtr> array;
  std::vector<std::pair<std::string, JPtr>> object;

  [[nodiscard]] const JValue* Find(std::string_view key) const noexcept {
    if (kind != Kind::Object) return nullptr;
    for (const auto& [k, v] : object) {
      if (k == key) return v.get();
    }
    return nullptr;
  }
};

class JParser {
 public:
  explicit JParser(std::string_view text) : s_(text) {}

  std::expected<JPtr, ManifestError> Parse() {
    auto root = ParseValue();
    if (!root) return root;
    SkipWs();
    return root;
  }

 private:
  [[nodiscard]] bool Eof() const noexcept { return pos_ >= s_.size(); }
  [[nodiscard]] char Peek() const noexcept { return Eof() ? '\0' : s_[pos_]; }

  void SkipWs() noexcept {
    while (!Eof()) {
      const char c = s_[pos_];
      if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
        ++pos_;
      } else {
        break;
      }
    }
  }

  bool Consume(char c) noexcept {
    SkipWs();
    if (Peek() == c) {
      ++pos_;
      return true;
    }
    return false;
  }

  [[nodiscard]] std::expected<JPtr, ManifestError> ParseValue() {
    SkipWs();
    if (Eof()) return std::unexpected(ManifestError::Truncated);
    switch (Peek()) {
      case '{': return ParseObject();
      case '[': return ParseArray();
      case '"': return ParseString();
      case 't':
      case 'f': return ParseBool();
      case 'n': return ParseNull();
      default: return ParseNumber();
    }
  }

  [[nodiscard]] std::expected<JPtr, ManifestError> ParseObject() {
    auto node = std::make_unique<JValue>();
    node->kind = JValue::Kind::Object;
    ++pos_;  // '{'
    SkipWs();
    if (Consume('}')) return node;

    for (;;) {
      SkipWs();
      auto key = ParseString();
      if (!key) return std::unexpected(key.error());
      if (!Consume(':')) return std::unexpected(ManifestError::BadValue);
      auto value = ParseValue();
      if (!value) return std::unexpected(value.error());
      node->object.emplace_back((*key)->string, std::move(*value));
      if (Consume(',')) continue;
      if (Consume('}')) return node;
      return std::unexpected(ManifestError::BadValue);
    }
  }

  [[nodiscard]] std::expected<JPtr, ManifestError> ParseArray() {
    auto node = std::make_unique<JValue>();
    node->kind = JValue::Kind::Array;
    ++pos_;  // '['
    SkipWs();
    if (Consume(']')) return node;

    for (;;) {
      auto value = ParseValue();
      if (!value) return std::unexpected(value.error());
      node->array.push_back(std::move(*value));
      if (Consume(',')) continue;
      if (Consume(']')) return node;
      return std::unexpected(ManifestError::BadValue);
    }
  }

  void AppendUtf8(std::string& out, std::uint32_t cp) {
    if (cp <= 0x7F) {
      out += static_cast<char>(cp);
    } else if (cp <= 0x7FF) {
      out += static_cast<char>(0xC0 | (cp >> 6));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp <= 0xFFFF) {
      out += static_cast<char>(0xE0 | (cp >> 12));
      out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
      out += static_cast<char>(0xF0 | (cp >> 18));
      out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
      out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    }
  }

  std::expected<JPtr, ManifestError> ParseString() {
    if (Peek() != '"') return std::unexpected(ManifestError::BadValue);
    ++pos_;

    auto node = std::make_unique<JValue>();
    node->kind = JValue::Kind::String;

    while (!Eof()) {
      const char c = s_[pos_++];
      if (c == '"') return node;

      if (c != '\\') {
        node->string += c;
        continue;
      }
      if (Eof()) return std::unexpected(ManifestError::Truncated);

      const char esc = s_[pos_++];
      switch (esc) {
        case '"': node->string += '"'; break;
        case '\\': node->string += '\\'; break;
        case '/': node->string += '/'; break;
        case 'n': node->string += '\n'; break;
        case 'r': node->string += '\r'; break;
        case 't': node->string += '\t'; break;
        case 'b': node->string += '\b'; break;
        case 'f': node->string += '\f'; break;
        case 'u': {
          if (pos_ + 4 > s_.size()) return std::unexpected(ManifestError::Truncated);
          std::uint32_t cp = 0;
          const auto res = std::from_chars(s_.data() + pos_, s_.data() + pos_ + 4, cp, 16);
          if (res.ec != std::errc{}) return std::unexpected(ManifestError::BadValue);
          pos_ += 4;
          // Par surrogate bajo -> se combina con el alto que sigue.
          if (cp >= 0xD800 && cp <= 0xDBFF && pos_ + 6 <= s_.size() &&
              s_[pos_] == '\\' && s_[pos_ + 1] == 'u') {
            std::uint32_t lo = 0;
            const auto r2 = std::from_chars(s_.data() + pos_ + 2, s_.data() + pos_ + 6, lo, 16);
            if (r2.ec == std::errc{} && lo >= 0xDC00 && lo <= 0xDFFF) {
              cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
              pos_ += 6;
            }
          }
          AppendUtf8(node->string, cp);
          break;
        }
        default: return std::unexpected(ManifestError::BadValue);
      }
    }
    return std::unexpected(ManifestError::Truncated);
  }

  [[nodiscard]] std::expected<JPtr, ManifestError> ParseBool() {
    if (s_.compare(pos_, 4, "true") == 0) {
      pos_ += 4;
      auto node = std::make_unique<JValue>();
      node->kind = JValue::Kind::Bool;
      node->boolean = true;
      return node;
    }
    if (s_.compare(pos_, 5, "false") == 0) {
      pos_ += 5;
      auto node = std::make_unique<JValue>();
      node->kind = JValue::Kind::Bool;
      node->boolean = false;
      return node;
    }
    return std::unexpected(ManifestError::BadValue);
  }

  [[nodiscard]] std::expected<JPtr, ManifestError> ParseNull() {
    if (s_.compare(pos_, 4, "null") == 0) {
      pos_ += 4;
      return std::make_unique<JValue>();
    }
    return std::unexpected(ManifestError::BadValue);
  }

  [[nodiscard]] std::expected<JPtr, ManifestError> ParseNumber() {
    const std::size_t start = pos_;
    if (Peek() == '-' || Peek() == '+') ++pos_;
    while (!Eof()) {
      const char c = Peek();
      if ((c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' || c == '-' || c == '+') {
        ++pos_;
      } else {
        break;
      }
    }
    if (pos_ == start) return std::unexpected(ManifestError::BadValue);

    double value = 0.0;
    const char* first = s_.data() + start;
    const char* last = s_.data() + pos_;
    const auto res = std::from_chars(first, last, value);
    if (res.ec != std::errc{}) return std::unexpected(ManifestError::BadValue);

    auto node = std::make_unique<JValue>();
    node->kind = JValue::Kind::Number;
    node->number = value;
    return node;
  }

  std::string_view s_;
  std::size_t pos_ = 0;
};

// ---------------------------------------------------------------------------
// Fechas: conversion civil a partir de epoch, sin depender de timegm
// (que no existe en MSVC como POSIX).
// ---------------------------------------------------------------------------

struct Civil {
  int year, month, day, hour, minute, second;
};

Civil FromUnix(std::int64_t secs) noexcept {
  std::int64_t days = secs / 86'400;
  std::int64_t rem = secs % 86'400;
  if (rem < 0) {
    rem += 86'400;
    --days;
  }
  const int hour = static_cast<int>(rem / 3600);
  const int minute = static_cast<int>((rem % 3600) / 60);
  const int second = static_cast<int>(rem % 60);

  // Algoritmo de Howard Hinnant: dias desde 1970-01-01 -> fecha civil.
  days += 719'468;
  const std::int64_t era = (days >= 0 ? days : days - 146'096) / 146'097;
  const std::int64_t doe = days - era * 146'097;
  const std::int64_t yoe = (doe - doe / 1460 + doe / 36'524 - doe / 146'096) / 365;
  const std::int64_t y = yoe + era * 400;
  const std::int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const std::int64_t mp = (5 * doy + 2) / 153;
  const std::int64_t d = doy - (153 * mp + 2) / 5 + 1;
  const std::int64_t m = mp < 10 ? mp + 3 : mp - 9;

  return Civil{static_cast<int>(y + (m <= 2 ? 1 : 0)), static_cast<int>(m),
               static_cast<int>(d), hour, minute, second};
}

PackageState ParseState(std::string_view s) noexcept {
  if (s == "enabled") return PackageState::Enabled;
  if (s == "disabled") return PackageState::Disabled;
  if (s == "absent") return PackageState::Absent;
  return PackageState::Unknown;
}

}  // namespace

std::string FormatIso8601Utc(std::int64_t unix_seconds) noexcept {
  const Civil c = FromUnix(unix_seconds);
  char buf[32];
  std::snprintf(buf, sizeof buf, "%04d-%02d-%02dT%02d:%02d:%02dZ", c.year, c.month,
                c.day, c.hour, c.minute, c.second);
  return buf;
}

std::string FormatCompactUtc(std::int64_t unix_seconds) noexcept {
  const Civil c = FromUnix(unix_seconds);
  char buf[24];
  std::snprintf(buf, sizeof buf, "%04d%02d%02d-%02d%02d%02d", c.year, c.month, c.day,
                c.hour, c.minute, c.second);
  return buf;
}

std::string ManifestFileName(std::string_view serial, std::string_view compact_utc) {
  std::string out;
  out.reserve(serial.size() + compact_utc.size() + 7);
  out += serial;
  out += '_';
  out += compact_utc;
  out += ".json";
  return out;
}

std::string SerializeManifest(const Manifest& m) {
  std::string out;
  out.reserve(64 + m.entries.size() * 80);

  out += "{\n";
  out += "  ";
  AppendKey(out, "version");
  out += std::to_string(m.version);
  out += ",\n";

  out += "  ";
  AppendKey(out, "serial");
  AppendEscaped(out, m.serial);
  out += ",\n";

  out += "  ";
  AppendKey(out, "timestamp");
  AppendEscaped(out, m.timestamp_utc);
  out += ",\n";

  out += "  ";
  AppendKey(out, "action");
  AppendEscaped(out, to_string(m.action));
  out += ",\n";

  out += "  ";
  AppendKey(out, "packages");
  out += "[\n";

  for (std::size_t i = 0; i < m.entries.size(); ++i) {
    const ManifestEntry& e = m.entries[i];
    out += "    {\n      ";
    AppendKey(out, "package");
    AppendEscaped(out, e.package);
    out += ",\n      ";
    AppendKey(out, "before");
    AppendEscaped(out, to_string(e.before));
    out += ",\n      ";
    AppendKey(out, "after");
    AppendEscaped(out, to_string(e.after));
    out += "\n    }";
    if (i + 1 < m.entries.size()) out += ',';
    out += '\n';
  }

  out += "  ]\n}\n";
  return out;
}

std::expected<Manifest, ManifestError> ParseManifest(std::string_view json) {
  JParser parser(json);
  auto root = parser.Parse();
  if (!root) return std::unexpected(root.error());
  if ((*root)->kind != JValue::Kind::Object) {
    return std::unexpected(ManifestError::NotAnObject);
  }

  const JValue& obj = **root;

  const JValue* version = obj.Find("version");
  if (version == nullptr || version->kind != JValue::Kind::Number) {
    return std::unexpected(ManifestError::MissingField);
  }
  if (static_cast<int>(version->number) != kManifestVersion) {
    return std::unexpected(ManifestError::UnsupportedVersion);
  }

  const JValue* serial = obj.Find("serial");
  const JValue* timestamp = obj.Find("timestamp");
  const JValue* action = obj.Find("action");
  const JValue* packages = obj.Find("packages");
  if (serial == nullptr || timestamp == nullptr || action == nullptr ||
      packages == nullptr) {
    return std::unexpected(ManifestError::MissingField);
  }
  if (serial->kind != JValue::Kind::String || timestamp->kind != JValue::Kind::String ||
      action->kind != JValue::Kind::String || packages->kind != JValue::Kind::Array) {
    return std::unexpected(ManifestError::BadValue);
  }

  Manifest m;
  m.version = static_cast<int>(version->number);
  m.serial = serial->string;
  m.timestamp_utc = timestamp->string;

  if (action->string == "disable") {
    m.action = PackageAction::Disable;
  } else if (action->string == "enable") {
    m.action = PackageAction::Enable;
  } else {
    return std::unexpected(ManifestError::BadEnum);
  }

  m.entries.reserve(packages->array.size());
  for (const JPtr& item : packages->array) {
    if (item->kind != JValue::Kind::Object) {
      return std::unexpected(ManifestError::BadValue);
    }
    const JValue* pkg = item->Find("package");
    const JValue* before = item->Find("before");
    const JValue* after = item->Find("after");
    if (pkg == nullptr || before == nullptr || after == nullptr) {
      return std::unexpected(ManifestError::MissingField);
    }
    if (pkg->kind != JValue::Kind::String || before->kind != JValue::Kind::String ||
        after->kind != JValue::Kind::String) {
      return std::unexpected(ManifestError::BadValue);
    }

    ManifestEntry e;
    e.package = pkg->string;
    e.before = ParseState(before->string);
    e.after = ParseState(after->string);
    m.entries.push_back(std::move(e));
  }

  return m;
}

}  // namespace daso
