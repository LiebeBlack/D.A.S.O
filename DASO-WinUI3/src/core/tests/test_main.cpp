#include "test_harness.hpp"

#include <chrono>
#include <cstring>

namespace daso::test {

void Sink::Report() const {
  if (message_.empty()) return;
  std::printf("    %s:%d  %s\n", file_.c_str(), line_, message_.c_str());
}

int RunAll(const char* filter) {
  int failed_cases = 0;
  int ran = 0;
  int total_checks = 0;
  int total_failures = 0;

  const auto t0 = std::chrono::steady_clock::now();

  for (const Case& c : Registry()) {
    if (filter != nullptr && std::strstr(c.name, filter) == nullptr) continue;
    ++ran;
    Current() = Context{};
    const auto tc0 = std::chrono::steady_clock::now();
    std::printf("[ RUN  ] %s\n", c.name);
    try {
      c.fn();
    } catch (const std::exception& e) {
      ++Current().failures;
      std::printf("    excepcion no capturada: %s\n", e.what());
    } catch (...) {
      ++Current().failures;
      std::printf("    excepcion desconocida\n");
    }
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - tc0)
                        .count();

    if (Current().failures == 0) {
      std::printf("[  OK  ] %s  (%d comprobaciones, %lld ms)\n", c.name, Current().checks,
                  static_cast<long long>(ms));
    } else {
      std::printf("[ FALLO] %s  (%d de %d comprobaciones)\n", c.name, Current().failures,
                  Current().checks);
      ++failed_cases;
    }
    total_checks += Current().checks;
    total_failures += Current().failures;
  }

  const auto total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - t0)
                            .count();

  std::printf("\n%d casos ejecutados, %d comprobaciones, %d fallos  (%lld ms)\n", ran,
              total_checks, total_failures, static_cast<long long>(total_ms));

  if (failed_cases == 0) {
    std::printf("TODO EN VERDE\n");
    return 0;
  }
  std::printf("%d CASOS CON FALLOS\n", failed_cases);
  return 1;
}

}  // namespace daso::test

int main(int argc, char** argv) {
  // Filtro opcional por nombre de caso:  daso_core_tests Catalog
  (void)argc;
  (void)argv;
  return daso::test::RunAll(nullptr);
}
