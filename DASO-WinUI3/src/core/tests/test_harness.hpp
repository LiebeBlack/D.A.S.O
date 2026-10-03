// D.A.S.O - arnés de pruebas minimo.
//
// Sin gtest ni Catch2 a proposito: las pruebas del nucleo son la unica parte
// del proyecto que debe compilarse en cualquier maquina con un compilador de
// C++, sin Windows SDK y sin descarga de dependencias. Un framework de test
// seria justo lo que impediria eso.
//
// Uso:
//   DASO_CHECK(condicion) << "mensaje explicativo";
//   DASO_CHECK_EQ(a, b);
//   DASO_CHECK_THROWS_V(expr, tipo);
#pragma once

#include <cstdio>
#include <exception>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace daso::test {

struct Case {
  const char* name;
  void (*fn)();
};

inline std::vector<Case>& Registry() {
  static std::vector<Case> cases;
  return cases;
}

struct Registrar {
  Registrar(const char* name, void (*fn)()) { Registry().push_back({name, fn}); }
};

// Estado del caso en curso.
struct Context {
  int checks = 0;
  int failures = 0;
};
inline Context& Current() {
  static Context ctx;
  return ctx;
}

class Sink {
 public:
  Sink() = default;
  explicit Sink(std::string_view file, int line) : file_(file), line_(line) {}

  ~Sink() { Report(); }

  template <typename T>
  Sink& operator<<(const T& v) {
    if (!message_.empty()) message_ += " | ";
    std::ostringstream os;
    os << v;
    message_ += os.str();
    return *this;
  }

  void Report() const;

 private:
  std::string file_;
  int line_ = 0;
  std::string message_;
};

inline int RunAll(const char* filter = nullptr);

}  // namespace daso::test

#define DASO_TEST(name)                                                  \
  static void name();                                                    \
  static const ::daso::test::Registrar registrar_##name(#name, &name);  \
  static void name()

#define DASO_CHECK(cond)                                                  \
  do {                                                                    \
    ++::daso::test::Current().checks;                                     \
    if (!(cond)) {                                                        \
      ++::daso::test::Current().failures;                                 \
      ::daso::test::Sink{__FILE__, __LINE__} << "FALLO: " #cond;          \
    }                                                                     \
  } while (0)

// Los operandos se copian, NO se enlazan por referencia.
//
// Enlazar a `hits.size()` (un prvalue) por `const auto&` es vida extendida,
// pero GCC 16 lo marca como -Wdangling-reference porque la extension depende
// del contexto completo. Copiar por valor quita el aviso sin suprimir nada.
#define DASO_CHECK_EQ(a, b)                                               \
  do {                                                                    \
    ++::daso::test::Current().checks;                                     \
    const auto lhs_ = (a);                                                \
    const auto rhs_ = (b);                                                \
    if (!(lhs_ == rhs_)) {                                                \
      ++::daso::test::Current().failures;                                 \
      ::daso::test::Sink{__FILE__, __LINE__}                              \
          << "FALLO: " #a " == " #b " (" << lhs_ << " vs " << rhs_ << ")"; \
    }                                                                     \
  } while (0)

#define DASO_CHECK_NE(a, b)                                               \
  do {                                                                    \
    ++::daso::test::Current().checks;                                     \
    if ((a) == (b)) {                                                     \
      ++::daso::test::Current().failures;                                 \
      ::daso::test::Sink{__FILE__, __LINE__} << "FALLO: " #a " != " #b;  \
    }                                                                     \
  } while (0)

#define DASO_CHECK_THROWS_V(expr, ExType)                                 \
  do {                                                                    \
    ++::daso::test::Current().checks;                                     \
    bool threw_ = false;                                                  \
    try {                                                                 \
      (void)(expr);                                                       \
    } catch (const ExType&) {                                             \
      threw_ = true;                                                      \
    } catch (...) {                                                       \
    }                                                                     \
    if (!threw_) {                                                        \
      ++::daso::test::Current().failures;                                 \
      ::daso::test::Sink{__FILE__, __LINE__}                              \
          << "FALLO: " #expr " no lanzó " #ExType;                       \
    }                                                                     \
  } while (0)
