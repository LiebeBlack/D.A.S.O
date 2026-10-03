#include "daso/log_buffer.hpp"

#include <algorithm>
#include <cstring>

namespace daso {

LogBuffer::LogBuffer(std::size_t capacity) : cap_(capacity == 0 ? 1 : capacity) {
#if DASO_HAVE_INPLACE_VECTOR
  storage_.resize(cap_);
#else
  storage_.assign(cap_, '\0');
#endif
  head_ = 0;
  len_ = 0;
}

void LogBuffer::Append(std::string_view chunk) noexcept {
  if (chunk.empty()) return;
  total_ += chunk.size();
  if (storage_.size() < cap_) {
#if DASO_HAVE_INPLACE_VECTOR
    storage_.resize(cap_);
#else
    storage_.resize(cap_, '\0');
#endif
  }

  // Un fragmento mayor que la capacidad descarta todo lo anterior: no tiene
  // sentido conservar la cola de un texto al que ya se le cayó la cabeza.
  if (chunk.size() >= cap_) {
    chunk.remove_prefix(chunk.size() - cap_);
    std::memcpy(Data(), chunk.data(), cap_);
    head_ = 0;
    len_ = cap_;
    truncated_ = true;
    return;
  }

  const std::size_t tail = (head_ + len_) % cap_;
  const std::size_t first = std::min(chunk.size(), cap_ - tail);
  std::memcpy(Data() + tail, chunk.data(), first);
  if (chunk.size() > first) {
    std::memcpy(Data(), chunk.data() + first, chunk.size() - first);
  }

  const std::size_t total = len_ + chunk.size();
  if (total > cap_) {
    head_ = (head_ + (total - cap_)) % cap_;
    len_ = cap_;
    truncated_ = true;
  } else {
    len_ = total;
  }
}

std::size_t LogBuffer::CopyOut(char* dest) const noexcept {
  if (dest == nullptr) return len_;
  if (len_ == 0) return 0;
  const std::size_t first = std::min(len_, cap_ - head_);
  std::memcpy(dest, Data() + head_, first);
  if (len_ > first) std::memcpy(dest + first, Data(), len_ - first);
  return len_;
}

std::string LogBuffer::Snapshot() const {
  std::string out;
  out.resize(len_);
  const std::size_t written = CopyOut(out.data());
  out.resize(written);
  return out;
}

void LogBuffer::Clear() noexcept {
  head_ = 0;
  len_ = 0;
  truncated_ = false;
}

std::size_t LogBuffer::LineCount() const noexcept {
  if (len_ == 0) return 0;
  std::size_t n = 0;
  for (std::size_t i = 0; i < len_; ++i) {
    if (Data()[(head_ + i) % cap_] == '\n') ++n;
  }
  return n;
}

}  // namespace daso
