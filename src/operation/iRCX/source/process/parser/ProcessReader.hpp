#pragma once

#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

#include "ProcessDiagnostic.hpp"
#include "ProcessTechnology.hpp"

namespace ircx {

struct ProcessParseOptions
{
  std::size_t max_nesting = 128;
  std::size_t max_expression_nodes = 4096;
};
struct ProcessParseResult
{
  std::optional<ProcessTechnology> technology;
  std::vector<ProcessDiagnostic> diagnostics;
  explicit operator bool() const noexcept { return technology.has_value(); }
};

// Both entry points identify syntax from content, independently of the file name.
[[nodiscard]] ProcessParseResult parseProcess(std::string_view text, std::string_view source_name = "<string>",
                                            const ProcessParseOptions& options = {});
[[nodiscard]] ProcessParseResult readProcessFile(const std::filesystem::path& file, const ProcessParseOptions& options = {});

}  // namespace ircx
