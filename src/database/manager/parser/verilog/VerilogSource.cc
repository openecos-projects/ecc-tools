// iEDA is licensed under Mulan PSL v2. See LICENSE for details.
#include "VerilogSource.hh"

#include <zlib.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <regex>
#include <sstream>
#include <unordered_set>

namespace idb::verilog::detail {
namespace {
const std::unordered_set<std::string>& compilerDirectives()
{
  static const std::unordered_set<std::string> names{"define",
                                                     "undef",
                                                     "ifdef",
                                                     "ifndef",
                                                     "elsif",
                                                     "else",
                                                     "endif",
                                                     "include",
                                                     "timescale",
                                                     "celldefine",
                                                     "endcelldefine",
                                                     "default_nettype",
                                                     "resetall",
                                                     "line",
                                                     "begin_keywords",
                                                     "end_keywords",
                                                     "unconnected_drive",
                                                     "nounconnected_drive",
                                                     "pragma"};
  return names;
}
bool identifier(char c)
{
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$';
}
std::string trim(std::string_view text)
{
  const auto first = text.find_first_not_of(" \t\r\n\f");
  return first == text.npos ? std::string{} : std::string(text.substr(first, text.find_last_not_of(" \t\r\n\f") - first + 1));
}
struct Macro
{
  std::vector<std::string> parameters;
  std::string body;
  bool function = false;
};
struct Conditional
{
  bool parent;
  bool selected;
  bool active;
  bool seen_else = false;
};
class Preprocessor
{
 public:
  Preprocessor(SourceText& output, const SourceOptions& options) : _output(output), _options(options)
  {
    for (const auto& [name, value] : options.defines)
      _macros.emplace(name, Macro{{}, value, false});
  }
  void run(std::string_view text, const std::string& source)
  {
    const auto file = static_cast<uint32_t>(_output.files.size());
    _output.files.push_back(source);
    _last = {1, 1, file};
    process(text, _last, 0);
    if (!_line_start)
      emit("\n", _last);
  }
  void finish()
  {
    if (!_conditions.empty())
      throw Error(_last, "unterminated conditional compiler directive");
  }

 private:
  bool active() const { return _conditions.empty() || _conditions.back().active; }
  void emit(std::string_view text, SourceLocation location)
  {
    if (_output.text.size() + text.size() > (size_t{1} << 30))
      throw Error(location, "preprocessed input exceeds 1 GiB limit");
    for (char c : text) {
      if (_line_start) {
        _output.lines.push_back(location);
        _line_start = false;
      }
      _output.text += c;
      if (c == '\n') {
        _line_start = true;
        ++location.line;
        location.column = 1;
      } else
        ++location.column;
    }
  }
  static std::string word(std::string_view text, size_t& at)
  {
    const auto start = at;
    while (at < text.size() && identifier(text[at]))
      ++at;
    return std::string(text.substr(start, at - start));
  }
  static void spaces(std::string_view text, size_t& at)
  {
    while (at < text.size() && std::isspace(static_cast<unsigned char>(text[at])))
      ++at;
  }
  // Splitting is token aware: commas in nested calls/concatenations and strings are not argument separators.
  static std::vector<std::string> arguments(std::string_view text, size_t& at, SourceLocation location)
  {
    if (at == text.size() || text[at++] != '(')
      throw Error(location, "expected macro argument list");
    std::vector<std::string> args;
    size_t start = at;
    int depth = 0;
    bool quote = false;
    while (at < text.size()) {
      const char c = text[at++];
      if (c == '\\') {
        if (quote && at < text.size())
          ++at;
        else if (!quote)
          while (at < text.size() && !std::isspace(static_cast<unsigned char>(text[at])))
            ++at;
        continue;
      }
      if (c == '"')
        quote = !quote;
      if (quote)
        continue;
      if (c == '/' && at < text.size() && text[at] == '/') {
        const auto end = text.find('\n', at + 1);
        at = end == text.npos ? text.size() : end;
        continue;
      }
      if (c == '/' && at < text.size() && text[at] == '*') {
        const auto end = text.find("*/", at + 1);
        if (end == text.npos)
          throw Error(location, "unterminated macro argument comment");
        at = end + 2;
        continue;
      }
      if (c == '(' || c == '[' || c == '{')
        ++depth;
      else if (c == ')' && depth == 0) {
        args.push_back(trim(text.substr(start, at - 1 - start)));
        return args;
      } else if (c == ')' || c == ']' || c == '}')
        --depth;
      else if (c == ',' && depth == 0) {
        args.push_back(trim(text.substr(start, at - 1 - start)));
        start = at;
      }
    }
    throw Error(location, "unterminated macro argument list");
  }
  static std::string substitute(const Macro& macro, const std::vector<std::string>& args)
  {
    std::string result;
    for (size_t i = 0; i < macro.body.size();) {
      const char c = macro.body[i];
      if (c == '"' || c == '\\') {
        const auto start = i++;
        if (c == '"') {
          while (i < macro.body.size()) {
            if (macro.body[i++] == '"')
              break;
            if (macro.body[i - 1] == '\\' && i < macro.body.size())
              ++i;
          }
        } else
          while (i < macro.body.size() && !std::isspace(static_cast<unsigned char>(macro.body[i])))
            ++i;
        result.append(macro.body, start, i - start);
      } else if (std::isdigit(static_cast<unsigned char>(c)) || c == '\'') {
        const auto start = i;
        while (i < macro.body.size() && (std::isdigit(static_cast<unsigned char>(macro.body[i])) || macro.body[i] == '_'))
          ++i;
        size_t base = i;
        spaces(macro.body, base);
        if (base < macro.body.size() && macro.body[base] == '\'') {
          i = base + 1;
          if (i < macro.body.size() && (macro.body[i] == 's' || macro.body[i] == 'S'))
            ++i;
          if (i < macro.body.size())
            ++i;
          spaces(macro.body, i);
          while (i < macro.body.size() && (identifier(macro.body[i]) || macro.body[i] == '?'))
            ++i;
        }
        if (i == start)
          ++i;
        result.append(macro.body, start, i - start);
      } else if (identifier(c)) {
        const auto token = word(macro.body, i);
        const auto found = std::find(macro.parameters.begin(), macro.parameters.end(), token);
        result += found == macro.parameters.end() ? token : args[found - macro.parameters.begin()];
      } else
        result += macro.body[i++];
    }
    return result;
  }
  void directive(const std::string& name, std::string_view argument, SourceLocation location, unsigned depth)
  {
    const std::string value = trim(argument);
    if (name == "ifdef" || name == "ifndef") {
      if (value.empty() || !std::all_of(value.begin(), value.end(), identifier))
        throw Error(location, "expected conditional macro name");
      const bool selected = _macros.count(value) != 0;
      const bool condition = name == "ifdef" ? selected : !selected;
      _conditions.push_back({active(), condition, active() && condition});
      return;
    }
    if (name == "elsif" || name == "else" || name == "endif") {
      if (_conditions.empty() || _conditions.back().seen_else && name != "endif")
        throw Error(location, "unmatched or misplaced conditional directive: `" + name);
      auto& branch = _conditions.back();
      if (name == "endif") {
        if (!value.empty())
          throw Error(location, "unexpected text after `endif");
        _conditions.pop_back();
      } else {
        if (name == "else" && !value.empty())
          throw Error(location, "unexpected text after `else");
        if (name == "elsif" && (value.empty() || !std::all_of(value.begin(), value.end(), identifier)))
          throw Error(location, "expected conditional macro name");
        const bool select = name == "else" || _macros.count(value);
        branch.active = branch.parent && !branch.selected && select;
        branch.selected |= select;
        branch.seen_else = name == "else";
      }
      return;
    }
    if (!active())
      return;
    if (name == "define") {
      size_t at = 0;
      auto key = word(value, at);
      if (key.empty() || std::isdigit(static_cast<unsigned char>(key[0])) || key[0] == '$' || compilerDirectives().count(key))
        throw Error(location, "invalid macro name");
      Macro macro;
      if (at < value.size() && value[at] == '(') {
        macro.function = true;
        macro.parameters = arguments(value, at, location);
        if (macro.parameters.size() == 1 && macro.parameters[0].empty())
          macro.parameters.clear();
        std::unordered_set<std::string> names;
        for (const auto& parameter : macro.parameters)
          if (parameter.empty() || !std::all_of(parameter.begin(), parameter.end(), identifier) || !names.insert(parameter).second)
            throw Error(location, "invalid or duplicate macro formal argument");
      }
      macro.body = trim(std::string_view(value).substr(at));
      _macros[key] = std::move(macro);
    } else if (name == "undef")
      _macros.erase(value);
    else if (name == "include") {
      // Include paths may be supplied by macros as well as by string literals.
      std::string path = value;
      if (!path.empty() && path[0] == '`') {
        auto found = _macros.find(path.substr(1));
        if (found == _macros.end() || found->second.function)
          throw Error(location, "undefined include filename macro");
        path = trim(found->second.body);
      }
      if (path.size() < 2 || path.front() != '"' || path.back() != '"')
        throw Error(location, "expected quoted include filename");
      path = path.substr(1, path.size() - 2);
      std::vector<std::filesystem::path> candidates{std::filesystem::path(_output.files[location.file]).parent_path() / path};
      for (const auto& directory : _options.include_paths)
        candidates.push_back(std::filesystem::path(directory) / path);
      bool found = false;
      for (const auto& candidate : candidates) {
        std::error_code error;
        if (!std::filesystem::is_regular_file(candidate, error))
          continue;
        if (depth >= 64)
          throw Error(location, "include nesting exceeds limit (64); check for recursive includes");
        const auto filename = candidate.lexically_normal().string();
        const auto id = static_cast<uint32_t>(_output.files.size());
        _output.files.push_back(filename);
        std::string content;
        try {
          content = readSource(filename);
        } catch (const std::exception& e) {
          throw Error(location, e.what());
        }
        emit("\n", location);
        process(content, {1, 1, id}, depth + 1);
        emit("\n", location);
        found = true;
        break;
      }
      if (!found)
        throw Error(location, "include file not found: " + path);
    } else if (name == "timescale") {
      static const std::regex pattern(R"(^\s*(1|10|100)\s*(s|ms|us|ns|ps|fs)\s*/\s*(1|10|100)\s*(s|ms|us|ns|ps|fs)\s*$)");
      std::smatch match;
      if (!std::regex_match(value, match, pattern))
        throw Error(location, "invalid `timescale");
      const std::vector<std::string> units{"fs", "ps", "ns", "us", "ms", "s"};
      auto scale = [&](int number, int unit) {
        return 3 * (std::find(units.begin(), units.end(), match[unit].str()) - units.begin()) + match[number].length() - 1;
      };
      if (scale(3, 4) > scale(1, 2))
        throw Error(location, "timescale precision is coarser than its unit");
      // Time units have no effect on a delay-free physical connectivity model.
    } else if (name == "celldefine" || name == "endcelldefine") {
      if (!value.empty())
        throw Error(location, "unexpected compiler directive argument");
    } else if (name == "default_nettype" || name == "resetall")
      emit("`" + name + " " + value + "\n", location);
    else
      throw Error(location, "unsupported compiler directive: `" + name);
  }
  void process(std::string_view text, SourceLocation location, unsigned depth)
  {
    if (depth > 128)
      throw Error(location, "macro expansion nesting exceeds limit");
    size_t at = 0;
    auto advance = [&](size_t end) {
      while (at < end) {
        if (text[at++] == '\n') {
          ++location.line;
          location.column = 1;
        } else
          ++location.column;
      }
      _last = location;
    };
    while (at < text.size()) {
      const size_t start = at;
      const SourceLocation origin = location;
      const char c = text[at];
      if (c == '/' && at + 1 < text.size() && (text[at + 1] == '/' || text[at + 1] == '*')) {
        size_t end;
        if (text[at + 1] == '/') {
          end = text.find('\n', at + 2);
          if (end == text.npos)
            end = text.size();
        } else {
          end = text.find("*/", at + 2);
          if (end == text.npos)
            throw Error(origin, "unterminated block comment");
          end += 2;
        }
        // Comments are whitespace, including inside macro actuals; preserve their newlines.
        if (active()) {
          std::string whitespace(text.substr(at, end - at));
          for (auto& ch : whitespace)
            if (ch != '\n')
              ch = ' ';
          emit(whitespace, origin);
        }
        advance(end);
      } else if (c == '"' || c == '\\') {
        size_t end = at + 1;
        if (c == '"') {
          while (end < text.size()) {
            if (text[end++] == '"')
              break;
            if (text[end - 1] == '\\' && end < text.size())
              ++end;
          }
        } else
          while (end < text.size() && !std::isspace(static_cast<unsigned char>(text[end])))
            ++end;
        if (active())
          emit(text.substr(at, end - at), origin);
        advance(end);
      } else if (c == '`') {
        size_t end = at + 1;
        const auto name = word(text, end);
        if (compilerDirectives().count(name)) {
          std::string argument;
          bool quoted = false;
          while (end < text.size() && text[end] != '\n') {
            if (!quoted && text[end] == '\\' && end + 1 < text.size()
                && (text[end + 1] == '\n' || text[end + 1] == '\r' && end + 2 < text.size() && text[end + 2] == '\n')) {
              argument += '\n';
              end += text[end + 1] == '\r' ? 3 : 2;
            } else if (!quoted && text[end] == '/' && end + 1 < text.size() && text[end + 1] == '/') {
              end = text.find('\n', end);
              if (end == text.npos)
                end = text.size();
            } else if (!quoted && text[end] == '/' && end + 1 < text.size() && text[end + 1] == '*') {
              auto close = text.find("*/", end + 2);
              if (close == text.npos)
                throw Error(origin, "unterminated directive comment");
              argument += ' ';
              end = close + 2;
            } else {
              const char next = text[end++];
              argument += next;
              if (quoted && next == '\\' && end < text.size())
                argument += text[end++];
              else if (next == '"')
                quoted = !quoted;
            }
          }
          advance(end);
          directive(name, argument, origin, depth);
        } else if (active()) {
          const auto found = _macros.find(name);
          if (found == _macros.end())
            throw Error(origin, "undefined macro: `" + name);
          const Macro macro = found->second;
          std::vector<std::string> args;
          if (macro.function) {
            spaces(text, end);
            args = arguments(text, end, origin);
            if (macro.parameters.empty() && args.size() == 1 && args[0].empty())
              args.clear();
            if (args.size() != macro.parameters.size())
              throw Error(origin, "macro argument count mismatch: " + name);
          }
          // Expand actual arguments before disabling the called macro, allowing ID(ID(value)).
          for (auto& argument : args) {
            std::string saved_text;
            std::vector<SourceLocation> saved_lines;
            saved_text.swap(_output.text);
            saved_lines.swap(_output.lines);
            const bool saved_start = _line_start;
            _line_start = true;
            process(argument, origin, depth + 1);
            argument = std::move(_output.text);
            _output.text = std::move(saved_text);
            _output.lines = std::move(saved_lines);
            _line_start = saved_start;
          }
          if (!_expanding.insert(name).second)
            throw Error(origin, "recursive macro expansion: " + name);
          const auto expanded = substitute(macro, args);
          advance(end);
          process(expanded, origin, depth + 1);
          _expanding.erase(name);
        } else
          advance(end);
      } else {
        size_t end = at + 1;
        while (end < text.size() && text[end] != '`' && text[end] != '/' && text[end] != '"' && text[end] != '\\')
          ++end;
        if (active())
          emit(text.substr(start, end - start), origin);
        advance(end);
      }
    }
  }
  SourceText& _output;
  const SourceOptions& _options;
  std::unordered_map<std::string, Macro> _macros;
  std::unordered_set<std::string> _expanding;
  std::vector<Conditional> _conditions;
  SourceLocation _last;
  bool _line_start = true;
};
}  // namespace
std::string readSource(const std::string& path)
{
  struct Close
  {
    void operator()(gzFile_s* file) const { gzclose(file); }
  };
  std::unique_ptr<gzFile_s, Close> file(gzopen(path.c_str(), "rb"));
  if (!file)
    throw std::runtime_error("cannot open Verilog input: " + path);
  gzbuffer(file.get(), 1U << 20);
  std::string source;
  std::array<char, 1U << 16> block;
  for (;;) {
    const int count = gzread(file.get(), block.data(), static_cast<unsigned>(block.size()));
    if (count < 0) {
      int code = 0;
      throw std::runtime_error(gzerror(file.get(), &code));
    }
    if (count == 0)
      break;
    source.append(block.data(), count);
  }
  int code = Z_OK;
  const std::string message = gzerror(file.get(), &code);
  if (code != Z_OK && code != Z_STREAM_END)
    throw std::runtime_error("incomplete Verilog input: " + message);
  return source;
}
SourceText preprocessFiles(const std::vector<std::string>& paths, const SourceOptions& options)
{
  SourceText result;
  Preprocessor processor(result, options);
  std::string current;
  try {
    for (const auto& path : paths) {
      current = path;
      processor.run(readSource(path), path);
    }
    processor.finish();
  } catch (const Error& error) {
    result.diagnostics.push_back(
        {error.location.file < result.files.size() ? result.files[error.location.file] : current, error.location, error.what()});
  } catch (const std::exception& error) {
    result.diagnostics.push_back({current, {}, error.what()});
  }
  return result;
}
SourceText preprocess(std::string_view text, const std::string& source, const SourceOptions& options)
{
  SourceText result;
  if (text.find('`') == text.npos) {
    result.text = text;
    result.files.push_back(source);
    return result;
  }
  try {
    Preprocessor processor(result, options);
    processor.run(text, source);
    processor.finish();
  } catch (const Error& error) {
    result.diagnostics.push_back(
        {error.location.file < result.files.size() ? result.files[error.location.file] : source, error.location, error.what()});
  }
  return result;
}
}  // namespace idb::verilog::detail
