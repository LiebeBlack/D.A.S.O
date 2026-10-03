// D.A.S.O - buffer circular del registro de actividad.
//
// Requisitos que motivan el diseno:
//   - 0 asignaciones por linea de log. El buffer tiene tamano fijo y el
//     almacenamiento inline se reserva una sola vez al construir.
//   - Cuando se llena, se descarta lo mas antiguo en vez de crecer sin limite.
//   - Snapshot() entrega texto contiguo para que la vista lo pinte de una vez.
//
// El almacenamiento usa std::inplace_vector (C++26): memoria inline sin coste
// de heap hasta que el log supera la capacidad inline. Con compiladores que no
// tengan <inplace_vector> se degrada a std::vector, que funciona igual pero
// reserva en la primera escritura.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// Test de capacidad de biblioteca, no de disponibilidad del cabecero.
//
// __has_include(<inplace_vector>) NO sirve: libstdc++ instala el fichero pero
// solo declara la plantilla cuando el estandar es c++26 o posterior. Con
// -std=c++23 el include tiene exito y luego el tipo no existe.
#if defined(__has_include)
#if __has_include(<inplace_vector>)
#include <inplace_vector>
#endif
#endif

#if defined(__cpp_lib_inplace_vector)
#define DASO_HAVE_INPLACE_VECTOR 1
#else
#define DASO_HAVE_INPLACE_VECTOR 0
#endif

namespace daso {

class LogBuffer {
 public:
  static constexpr std::size_t kInlineBytes = 8 * 1024;
  static constexpr std::size_t kDefaultCapacity = 256 * 1024;

  explicit LogBuffer(std::size_t capacity = kDefaultCapacity);

  // Anade bytes crudos. Un fragmento puede partir una linea o un caracter
  // UTF-8 por la mitad; no se pierde nada porque el corte es a nivel de byte.
  void Append(std::string_view chunk) noexcept;

  void AppendLine(std::string_view line) noexcept {
    Append(line);
    Append("\n");
  }

  // Copia el texto valido (mas antiguo primero) en un destino del tamanio dado.
  // Devuelve bytes escritos. Si dest es nullptr solo cuenta.
  [[nodiscard]] std::size_t CopyOut(char* dest) const noexcept;

  // Texto contiguo mas reciente. Reserva una vez por llamada.
  [[nodiscard]] std::string Snapshot() const;

  void Clear() noexcept;

  [[nodiscard]] std::size_t Size() const noexcept { return len_; }
  [[nodiscard]] std::size_t Capacity() const noexcept { return cap_; }
  // `true` si hubo que descartar texto antiguo por falta de espacio.
  [[nodiscard]] bool Truncated() const noexcept { return truncated_; }
  [[nodiscard]] std::uint64_t TotalAppended() const noexcept { return total_; }

  // Numero de lineas completas terminadas en '\n'. Se recorre sin copiar.
  [[nodiscard]] std::size_t LineCount() const noexcept;

 private:
  char* Data() noexcept { return storage_.data(); }
  const char* Data() const noexcept { return storage_.data(); }

#if DASO_HAVE_INPLACE_VECTOR
  std::inplace_vector<char, kInlineBytes> storage_;
#else
  std::vector<char> storage_;  // mismo comportamiento, reserva en la 1a escritura
#endif
  std::size_t cap_ = 0;
  std::size_t head_ = 0;  // indice del byte mas antiguo
  std::size_t len_ = 0;   // bytes validos
  bool truncated_ = false;
  std::uint64_t total_ = 0;
};

}  // namespace daso
