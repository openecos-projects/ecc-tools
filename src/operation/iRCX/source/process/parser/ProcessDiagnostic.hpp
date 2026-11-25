#pragma once

#include <cstddef>
#include <string>

#include "ProcessProperty.hpp"

namespace ircx {
enum class ProcessSeverity { kWarning, kError };
struct ProcessDiagnostic
{
  ProcessSeverity severity = ProcessSeverity::kError;
  std::string code;
  std::string source_name;
  ProcessPosition begin;
  ProcessPosition end;
  std::string field_path;
  std::string message;
};
}  // namespace ircx
