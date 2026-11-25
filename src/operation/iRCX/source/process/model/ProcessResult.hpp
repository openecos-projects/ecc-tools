#pragma once

#include <optional>
#include <string>
#include <utility>

namespace ircx {
// Checked results keep the process library independent of application logging.
// A failed operation never exposes a partial value.
template <class T, class Error = std::string>
struct [[nodiscard]] ProcessResult
{
  std::optional<T> value;
  Error error{};

  ProcessResult(T result) : value(std::move(result)) {}
  ProcessResult(Error failure) : error(std::move(failure)) {}
  explicit operator bool() const { return value.has_value(); }
  T& operator*() { return *value; }
  const T& operator*() const { return *value; }
  T* operator->() { return &*value; }
  const T* operator->() const { return &*value; }
};

template <class Error>
struct [[nodiscard]] ProcessResult<void, Error>
{
  Error error{};
  bool succeeded = true;

  ProcessResult() = default;
  ProcessResult(Error failure) : error(std::move(failure)), succeeded(false) {}
  explicit operator bool() const { return succeeded; }
};
}  // namespace ircx
