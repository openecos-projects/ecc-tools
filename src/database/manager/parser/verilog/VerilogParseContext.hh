// iEDA is licensed under Mulan PSL v2. See LICENSE for details.
#pragma once

#include <unordered_set>

#include "VerilogSyntax.hh"

namespace idb::verilog::detail {
// Private construction state shared by the generated scanner and grammar.
// It owns the scanner, never resolves symbols or allocates connectivity.
class ParseContext
{
 public:
  ParseContext(SyntaxTree& tree, std::string_view text, const std::vector<SourceLocation>& lines, LanguageMode language);
  ~ParseContext();
  ParseContext(const ParseContext&) = delete;
  ParseContext& operator=(const ParseContext&) = delete;
  void* scanner() const { return _scanner; }
  bool sv() const { return _language == LanguageMode::systemVerilog; }
  LanguageMode language() const { return _language; }
  void requireSv(SourceLocation location) const;
  size_t read(char* destination, size_t capacity);
  void match(std::string_view text);
  void rewindMatch();
  SourceLocation location() const;
  SourceLocation endLocation() const { return mapped(_cursor); }
  SourceLocation token(SourceLocation start);
  SourceLocation lastTokenLocation() const { return _last_token; }
  void startLiteral(std::string_view text);
  void appendLiteral(std::string_view text) { _literal.append(text); }
  std::string finishNumber();
  std::string finishString() { return std::move(_literal); }
  SourceLocation literalLocation() const { return _literal_location; }
  bool adjacentLiteral() const;
  int comment_state = 0;
  SourceLocation comment_location;

  void beginModule(std::string name, std::vector<Attribute> attributes, SourceLocation location);
  void endModule();
  void finish();
  ScopeSyntax& scope();
  void beginBlock(std::string name, SourceLocation location);
  std::unique_ptr<GenerateBlock> endBlock(bool direct_conditional = false);
  std::unique_ptr<Generate> beginGenerate(GenerateKind kind, SourceLocation location);
  void emitGenerate(std::unique_ptr<Generate> node);
  void beginRegion(SourceLocation location);
  void endRegion() { _generate_region = false; }
  void setImplicit(bool enabled) { _implicit = enabled; }
  void beginParameterHeader();
  void endParameterHeader() { _parameter_header = false; }
  void parameterSpec(Parameter spec);
  void parameter(std::string name, ExprId value, SourceLocation location);
  void declarationSpec(Declaration spec) { _declaration = std::move(spec); }
  void declaration(std::string name, ExprId initializer, SourceLocation location);
  void portSpec(Declaration spec, SourceLocation location);
  void port(std::string name, SourceLocation location, bool typed);
  void instanceType(std::string name, std::vector<Connection> parameters);
  void instance(std::string name, Range range, std::vector<Connection> ports, SourceLocation location);
  void validateConnections(const std::vector<Connection>& connections, bool parameters) const;
  ExprId add(ExpressionKind kind, SourceLocation location, std::string text = {}, std::vector<ExprId> operands = {});
  void openExpression(SourceLocation location);
  void closeExpression() { --_expression_nesting; }

 private:
  SourceLocation mapped(SourceLocation location) const;
  SyntaxTree& _tree;
  std::string_view _text;
  const std::vector<SourceLocation>& _lines;
  LanguageMode _language;
  void* _scanner = nullptr;
  size_t _read_offset = 0;
  SourceLocation _cursor, _match_start, _literal_end, _literal_location, _last_token;
  uint32_t _order = 0;
  std::string _literal;
  std::unique_ptr<Module> _module;
  std::vector<std::unique_ptr<GenerateBlock>> _blocks;
  std::unordered_set<std::string> _module_names;
  std::vector<uint16_t> _expression_depth;
  unsigned _expression_nesting = 0;
  unsigned _generate_depth = 0;
  bool _generate_region = false;
  bool _implicit = true;
  bool _parameter_header = false;
  Parameter _parameter;
  Declaration _declaration;
  std::string _instance_type;
  std::vector<Connection> _instance_parameters;
};
void* createScanner(ParseContext& context);
void destroyScanner(void* scanner);
}  // namespace idb::verilog::detail
