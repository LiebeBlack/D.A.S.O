// D.A.S.O - cancelacion cooperativa.
//
// Un solo flag atomico. La capa Win32 lo consulta entre lecturas y lo conecta
// a TerminateJobObject para matar el arbol de procesos de adb.
#pragma once

#include <atomic>

namespace daso {

class CancelToken {
 public:
  CancelToken() = default;
  CancelToken(const CancelToken&) = delete;
  CancelToken& operator=(const CancelToken&) = delete;

  void Cancel() noexcept { flag_.store(true, std::memory_order_relaxed); }
  void Reset() noexcept { flag_.store(false, std::memory_order_relaxed); }

  [[nodiscard]] bool IsCancelled() const noexcept {
    return flag_.load(std::memory_order_relaxed);
  }

 private:
  std::atomic<bool> flag_{false};
};

}  // namespace daso
