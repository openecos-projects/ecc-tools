#pragma once

#include <string>
#include <vector>

#include "ProcessReader.hpp"
#include "ProcessResult.hpp"

namespace ircx::process_detail {

// Both frontends produce the owning source records retained by the reader.
using Quantity = ProcessQuantity;
using Field = ProcessProperty;
struct Document
{
  std::vector<Field> fields;
};
enum class SyntaxKind { kItf, kTech, kUnknown, kMixed };

struct Failure
{
  std::string code;
  ProcessPosition position;
  std::string field;
  std::string message;
};
template <class T>
using Result = ProcessResult<T, Failure>;
using Status = Result<void>;

std::string upper(std::string_view text);
Result<SyntaxKind> detectSyntax(std::string_view text);
Result<Document> parseItf(std::string_view text, const ProcessParseOptions& options);
Result<Document> parseTech(std::string_view text, const ProcessParseOptions& options);
Result<ProcessTechnology> buildTechnology(const Document& document, SyntaxKind kind, std::vector<ProcessDiagnostic>& diagnostics);

}  // namespace ircx::process_detail
