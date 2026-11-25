// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#include "SPEFSanitizer.hpp"

#include <unistd.h>

#include <atomic>
#include <cctype>
#include <exception>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <vector>

#include "Logger.hpp"
#include "json.hpp"

namespace ircx {

namespace {

bool hasPrecedingEscape(std::string_view text, std::size_t idx)
{
  std::size_t backslashes = 0;
  while (idx > 0 && text[--idx] == '\\') {
    ++backslashes;
  }
  return (backslashes % 2) == 1;
}

std::string_view trimAscii(std::string_view text)
{
  const std::size_t first = text.find_first_not_of(" \t\r\n");
  if (first == std::string_view::npos) {
    return {};
  }
  const std::size_t last = text.find_last_not_of(" \t\r\n");
  return text.substr(first, last - first + 1);
}

bool startsWithKeyword(std::string_view trimmed, std::string_view keyword)
{
  if (!trimmed.starts_with(keyword)) {
    return false;
  }
  return trimmed.size() == keyword.size() || std::isspace(static_cast<unsigned char>(trimmed[keyword.size()])) != 0;
}

bool startsWithAnyKeyword(std::string_view trimmed, std::initializer_list<std::string_view> keywords)
{
  for (std::string_view kw : keywords) {
    if (startsWithKeyword(trimmed, kw)) {
      return true;
    }
  }
  return false;
}

bool isSpefHeaderLine(std::string_view trimmed)
{
  return startsWithAnyKeyword(trimmed, {"*SPEF", "*DESIGN", "*DATE", "*VENDOR", "*PROGRAM", "*VERSION", "*DESIGN_FLOW", "*DIVIDER",
                                        "*DELIMITER", "*BUS_DELIMITER", "*PREFIX_BUS_DELIMITER", "*T_UNIT", "*C_UNIT", "*R_UNIT", "*L_UNIT"});
}

// Keep the original text and an equally sized code view. Comment characters
// become spaces in code, so keywords inside comments cannot change sections.
struct SPEFLine
{
  std::string text;
  std::string code;
  std::string comments;
};

SPEFLine scanLine(const std::string& text, bool& in_block_comment)
{
  SPEFLine line{text, std::string(text.size(), ' '), text};
  bool in_quotes = false;
  for (std::size_t idx = 0; idx < text.size(); ++idx) {
    const char ch = text[idx];
    if (in_block_comment) {
      if (ch == '*' && idx + 1 < text.size() && text[idx + 1] == '/') {
        in_block_comment = false;
        ++idx;
      }
      continue;
    }
    if (!in_quotes && ch == '/' && idx + 1 < text.size()) {
      if (text[idx + 1] == '/') {
        break;
      }
      if (text[idx + 1] == '*') {
        in_block_comment = true;
        ++idx;
        continue;
      }
    }
    line.code[idx] = ch;
    line.comments[idx] = ' ';
    if (ch == '\\' && idx + 1 < text.size()) {
      ++idx;
      line.code[idx] = text[idx];
      line.comments[idx] = ' ';
    } else if (ch == '"') {
      in_quotes = !in_quotes;
    }
  }
  return line;
}

std::string escapeUnquotedDollarLine(const SPEFLine& line, bool& changed)
{
  const std::string_view trimmed = trimAscii(line.code);
  if (trimmed.empty() || isSpefHeaderLine(trimmed) || line.code.find('$') == std::string::npos) {
    return line.text;
  }

  std::string escaped;
  escaped.reserve(line.text.size() + 4);
  bool in_quotes = false;
  for (std::size_t idx = 0; idx < line.text.size(); ++idx) {
    const char ch = line.code[idx];
    if (ch == '"' && !hasPrecedingEscape(line.code, idx)) {
      in_quotes = !in_quotes;
      escaped.push_back(line.text[idx]);
      continue;
    }
    if (!in_quotes && ch == '$' && !hasPrecedingEscape(line.code, idx)) {
      escaped.push_back('\\');
      changed = true;
    }
    escaped.push_back(line.text[idx]);
  }
  return escaped;
}

std::filesystem::path makeUniqueTempPath(const std::filesystem::path& target_path)
{
  static std::atomic<uint64_t> temp_seq{0};
  const uint64_t seq = temp_seq.fetch_add(1, std::memory_order_relaxed);
  std::filesystem::path tmp_path = target_path;
  tmp_path += "." + std::to_string(static_cast<long long>(::getpid())) + "." + std::to_string(seq) + ".tmp";
  return tmp_path;
}

bool endsPortsSection(std::string_view trimmed)
{
  return startsWithAnyKeyword(trimmed, {"*D_NET", "*R_NET", "*D_PNET", "*R_PNET", "*PHYSICAL_PORTS", "*NAME_MAP", "*POWER_NETS",
                                        "*GROUND_NETS", "*DEFINE", "*PDEFINE", "*END"});
}

bool endsConnSection(std::string_view trimmed)
{
  return startsWithAnyKeyword(trimmed, {"*CAP", "*RES", "*INDUC", "*END", "*D_NET", "*R_NET", "*D_PNET", "*R_PNET"});
}

enum class SectionState {
  kNormal,
  kPendingNameMap,
  kPendingPorts,
  kPendingConn,
  kPendingElements,
};

}  // namespace

std::string escapeSPEFName(std::string_view name)
{
  // The native writer uses '/' for hierarchy and '[]' for bus bits. A bus
  // index is structural only at the end of its path component (Syntax 92).
  std::string escaped;
  escaped.reserve(name.size() + 4);
  for (std::size_t idx = 0; idx < name.size(); ++idx) {
    const char ch = name[idx];
    if (ch == '\\') {
      escaped.push_back('\\');
      escaped.push_back(idx + 1 < name.size() ? name[++idx] : '\\');
      continue;
    }
    if (ch == '[' && idx > 0 && (name[idx - 1] != '/' || hasPrecedingEscape(name, idx - 1))) {
      std::size_t end = idx + 1;
      while (end < name.size() && name[end] >= '0' && name[end] <= '9') {
        ++end;
      }
      if (end > idx + 1) {
        if (end < name.size() && name[end] == ']') {
          ++end;
        }
        if (end == name.size() || name[end] == '/') {
          escaped.append(name.substr(idx, end - idx));
          idx = end - 1;
          continue;
        }
      }
    }
    const bool identifier_char = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_';
    if (!identifier_char && ch != '/') {
      escaped.push_back('\\');
    }
    escaped.push_back(ch);
  }
  return escaped;
}

bool sanitizeSPEFFile(const std::filesystem::path& spef_path)
{
  std::ifstream in(spef_path, std::ios::binary);
  if (!in.is_open()) {
    std::cerr << "[iRCX] Failed to open SPEF for sanitization: " << spef_path << "\n";
    return false;
  }

  const std::filesystem::path tmp_path = makeUniqueTempPath(spef_path);
  std::ofstream out(tmp_path, std::ios::binary | std::ios::trunc);
  if (!out.is_open()) {
    std::cerr << "[iRCX] Failed to create temporary SPEF file: " << tmp_path << "\n";
    return false;
  }

  auto cleanup_temp = [&]() {
    if (out.is_open()) {
      out.close();
    }
    std::error_code ec;
    std::filesystem::remove(tmp_path, ec);
  };

  auto emit_line = [&](const SPEFLine& line, bool& modified) {
    const std::string escaped = escapeUnquotedDollarLine(line, modified);
    out << escaped << '\n';
  };

  SectionState state = SectionState::kNormal;
  SPEFLine header_line;
  std::vector<SPEFLine> pending_lines;
  bool modified = false;

  auto emit_comments = [&](const SPEFLine& line) {
    // Removing a header or *N must not remove a block-comment opener/closer.
    if (!trimAscii(line.comments).empty()) {
      out << line.comments << '\n';
    }
  };

  auto flush_pending = [&](bool keep_header, bool drop_conn_nodes) {
    if (keep_header) {
      emit_line(header_line, modified);
    } else {
      modified = true;
      emit_comments(header_line);
    }
    for (const SPEFLine& pending : pending_lines) {
      if (drop_conn_nodes && startsWithKeyword(trimAscii(pending.code), "*N")) {
        modified = true;
        emit_comments(pending);
        continue;
      }
      emit_line(pending, modified);
    }
    header_line = {};
    pending_lines.clear();
    state = SectionState::kNormal;
  };

  bool in_block_comment = false;
  std::string text;
  while (std::getline(in, text)) {
    SPEFLine line = scanLine(text, in_block_comment);
    bool reprocess = true;
    while (reprocess) {
      reprocess = false;
      const std::string_view trimmed = trimAscii(line.code);

      if (state == SectionState::kPendingNameMap) {
        if (trimmed.empty()) {
          pending_lines.push_back(std::move(line));
          break;
        }
        const bool has_entry = trimmed.size() >= 2 && trimmed[0] == '*' && std::isdigit(static_cast<unsigned char>(trimmed[1])) != 0;
        flush_pending(has_entry, false);
        reprocess = true;
        continue;
      }

      if (state == SectionState::kPendingPorts) {
        if (trimmed.empty()) {
          pending_lines.push_back(std::move(line));
          break;
        }
        const bool empty_ports = endsPortsSection(trimmed);
        flush_pending(!empty_ports, false);
        reprocess = true;
        continue;
      }

      if (state == SectionState::kPendingConn) {
        if (endsConnSection(trimmed)) {
          flush_pending(false, true);
          reprocess = true;
          continue;
        }
        if (startsWithAnyKeyword(trimmed, {"*P", "*I"})) {
          flush_pending(true, false);
          emit_line(line, modified);
          break;
        }
        pending_lines.push_back(std::move(line));
        break;
      }

      if (state == SectionState::kPendingElements) {
        if (trimmed.empty()) {
          pending_lines.push_back(std::move(line));
          break;
        }
        const bool has_entry = std::isdigit(static_cast<unsigned char>(trimmed.front())) != 0;
        flush_pending(has_entry, false);
        reprocess = true;
        continue;
      }

      if (trimmed == "*NAME_MAP") {
        state = SectionState::kPendingNameMap;
        header_line = std::move(line);
      } else if (trimmed == "*PORTS") {
        state = SectionState::kPendingPorts;
        header_line = std::move(line);
      } else if (trimmed == "*CONN") {
        state = SectionState::kPendingConn;
        header_line = std::move(line);
      } else if (trimmed == "*CAP" || trimmed == "*RES") {
        state = SectionState::kPendingElements;
        header_line = std::move(line);
      } else {
        emit_line(line, modified);
      }
    }
  }

  if (state == SectionState::kPendingNameMap || state == SectionState::kPendingElements) {
    flush_pending(false, false);
  } else if (state == SectionState::kPendingPorts) {
    flush_pending(false, false);
  } else if (state == SectionState::kPendingConn) {
    flush_pending(false, true);
  }

  in.close();
  if (!modified) {
    cleanup_temp();
    return true;
  }

  out.flush();
  if (!out.good()) {
    std::cerr << "[iRCX] Failed while writing temporary SPEF file: " << tmp_path << "\n";
    cleanup_temp();
    return false;
  }
  out.close();

  std::error_code rename_ec;
  std::filesystem::rename(tmp_path, spef_path, rename_ec);
  if (rename_ec) {
    std::cerr << "[iRCX] Failed to replace SPEF file '" << spef_path << "': " << rename_ec.message() << "\n";
    cleanup_temp();
    return false;
  }
  return true;
}

bool sanitizeSPEFDirectory(const std::filesystem::path& output_dir)
{
  std::error_code ec;
  if (!std::filesystem::exists(output_dir, ec)) {
    if (ec) {
      std::cerr << "[iRCX] Failed to check SPEF output directory '" << output_dir << "': " << ec.message() << "\n";
      return false;
    }
    return true;
  }
  if (!std::filesystem::is_directory(output_dir, ec)) {
    std::cerr << "[iRCX] SPEF output path is not a directory: " << output_dir << "\n";
    return false;
  }

  bool ok = true;
  for (std::filesystem::recursive_directory_iterator it(output_dir, std::filesystem::directory_options::skip_permission_denied, ec), end;
       it != end && !ec; it.increment(ec)) {
    std::error_code entry_ec;
    if (it->is_regular_file(entry_ec) && it->path().extension() == ".spef") {
      if (!sanitizeSPEFFile(it->path())) {
        ok = false;
      }
    } else if (entry_ec) {
      std::cerr << "[iRCX] Failed to inspect path '" << it->path() << "': " << entry_ec.message() << "\n";
      ok = false;
    }
  }
  if (ec) {
    std::cerr << "[iRCX] Failed iterating SPEF output directory '" << output_dir << "': " << ec.message() << "\n";
    return false;
  }
  return ok;
}

bool sanitizeSPEFOutputsForConfig(const std::filesystem::path& config_path)
{
  if (config_path.empty()) {
    std::cerr << "[iRCX] Cannot sanitize SPEF outputs: empty config path.\n";
    return false;
  }

  std::error_code ec;
  const std::filesystem::path abs_config = std::filesystem::absolute(config_path, ec);
  if (ec) {
    std::cerr << "[iRCX] Failed to resolve config path '" << config_path << "': " << ec.message() << "\n";
    return false;
  }

  std::ifstream config_stream(abs_config);
  if (!config_stream.is_open()) {
    std::cerr << "[iRCX] Failed to open iRCX config for SPEF sanitization: " << abs_config << "\n";
    return false;
  }

  const auto config_json = nlohmann::json::parse(config_stream, nullptr, false);
  if (config_stream.bad() || config_json.is_discarded() || !config_json.is_object()) {
    RCXLOG.error(Loc::current(), "Failed to parse iRCX config for SPEF sanitization: ", abs_config);
  }

  if (!config_json.contains("output") || config_json["output"].is_null()) {
    return true;
  }
  if (!config_json["output"].is_string()) {
    std::cerr << "[iRCX] Invalid 'output' entry in iRCX config '" << abs_config << "'.\n";
    return false;
  }

  const std::string output_value = config_json["output"].get<std::string>();
  if (output_value.empty()) {
    return true;
  }

  std::filesystem::path output_dir(output_value);
  if (output_dir.is_relative()) {
    output_dir = abs_config.parent_path() / output_dir;
  }
  return sanitizeSPEFDirectory(output_dir);
}

}  // namespace ircx
