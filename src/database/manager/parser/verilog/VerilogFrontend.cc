// iEDA is licensed under Mulan PSL v2. See LICENSE for details.
#include "VerilogFrontend.hh"

#include "VerilogHierarchy.hh"
#include "VerilogSyntax.hh"

namespace idb::verilog {
NetlistResult compile(const SyntaxTree& syntax, std::string_view top, const LibraryLookup& library)
{
  auto hierarchy = bindHierarchy(syntax, top, library);
  if (!hierarchy)
    return {nullptr, std::move(hierarchy.diagnostics)};
  return lower(*hierarchy.design);
}
NetlistResult compile(std::string_view text, std::string source, std::string_view top, const SourceOptions& options,
                      const LibraryLookup& library)
{
  auto parsed = parse(text, std::move(source), options);
  if (!parsed)
    return {nullptr, std::move(parsed.diagnostics)};
  return compile(*parsed.design, top, library);
}
NetlistResult compileFiles(const std::vector<std::string>& paths, std::string_view top, const SourceOptions& options,
                           const LibraryLookup& library)
{
  auto parsed = readFiles(paths, options);
  if (!parsed)
    return {nullptr, std::move(parsed.diagnostics)};
  return compile(*parsed.design, top, library);
}
}  // namespace idb::verilog
