#include <fstream>

#include "ProcessInput.hpp"
#include "ProcessSyntax.hpp"

namespace ircx {
namespace {
process_detail::Result<ProcessTechnology> readModel(std::string_view text, const ProcessParseOptions& options, std::vector<ProcessDiagnostic>& diagnostics)
{
  using namespace process_detail;
  auto detected = detectSyntax(text);
  if (!detected)
    return detected.error;
  auto kind = *detected;
  if (kind == SyntaxKind::kMixed)
    return Failure{"mixed_process_syntax", {}, {}, "file mixes incompatible process syntax"};
  if (kind == SyntaxKind::kItf || kind == SyntaxKind::kTech) {
    auto parsed = kind == SyntaxKind::kItf ? parseItf(text, options) : parseTech(text, options);
    if (!parsed)
      return parsed.error;
    return buildTechnology(*parsed, kind, diagnostics);
  }
  auto itf = parseItf(text, options);
  auto tech = parseTech(text, options);
  if (text.empty() || (itf && tech && itf->fields.empty() && tech->fields.empty()))
    return Failure{"empty_process", {}, {}, "file contains no process definitions"};
  if (itf && tech)
    return Failure{"ambiguous_process_syntax", {}, {}, "file content does not uniquely identify a process syntax"};
  if (!itf && !tech)
    return Failure{"unknown_process_syntax", {}, {}, "cannot identify process syntax; ITF: " + itf.error.message + "; Tech: " + tech.error.message};
  return buildTechnology(itf ? *itf : *tech, itf ? SyntaxKind::kItf : SyntaxKind::kTech, diagnostics);
}
}  // namespace
ProcessParseResult parseProcess(std::string_view text, std::string_view source_name, const ProcessParseOptions& options)
{
  ProcessParseResult result;
  auto model = readModel(text, options, result.diagnostics);
  if (model) {
    result.technology = std::move(*model);
    result.technology->source_name = source_name;
    result.technology->source_text = text;
  } else {
    const auto& error = model.error;
    result.diagnostics.push_back({ProcessSeverity::kError, error.code, {}, error.position, error.position, error.field, error.message});
  }
  for (auto& diagnostic : result.diagnostics) diagnostic.source_name = source_name;
  return result;
}
ProcessParseResult readProcessFile(const std::filesystem::path& file, const ProcessParseOptions& options)
{
  std::ifstream stream(file, std::ios::binary);
  if (!stream) return {std::nullopt, {{ProcessSeverity::kError, "file_io", file.string(), {}, {}, {}, "cannot open process file"}}};
  std::string text;
  if (!process_detail::readText(stream, text))
    return {std::nullopt, {{ProcessSeverity::kError, "file_io", file.string(), {}, {}, {}, "cannot read process file"}}};
  return parseProcess(text, file.string(), options);
}
}  // namespace ircx
