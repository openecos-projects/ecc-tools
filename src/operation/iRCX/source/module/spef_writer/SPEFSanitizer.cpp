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

bool isCommentOrBlank(std::string_view trimmed)
{
  return trimmed.empty() || trimmed.starts_with("//");
}

bool isSpefHeaderLine(std::string_view trimmed)
{
  return startsWithAnyKeyword(trimmed, {"*SPEF", "*DESIGN", "*DATE", "*VENDOR", "*PROGRAM", "*VERSION", "*DESIGN_FLOW", "*DIVIDER",
                                        "*DELIMITER", "*BUS_DELIMITER", "*PREFIX_BUS_DELIMITER", "*T_UNIT", "*C_UNIT", "*R_UNIT", "*L_UNIT"});
}

std::string escapeUnquotedDollarLine(const std::string& line, bool& changed)
{
  const std::string_view trimmed = trimAscii(line);
  if (isCommentOrBlank(trimmed) || isSpefHeaderLine(trimmed) || line.find('$') == std::string::npos) {
    return line;
  }

  std::string escaped;
  escaped.reserve(line.size() + 4);
  bool in_quotes = false;
  for (std::size_t idx = 0; idx < line.size(); ++idx) {
    const char ch = line[idx];
    if (!in_quotes && ch == '/' && idx + 1 < line.size() && line[idx + 1] == '/') {
      escaped.append(line, idx);
      break;
    }
    if (ch == '"' && !hasPrecedingEscape(line, idx)) {
      in_quotes = !in_quotes;
      escaped.push_back(ch);
      continue;
    }
    if (!in_quotes && ch == '$' && !hasPrecedingEscape(line, idx)) {
      escaped.push_back('\\');
      changed = true;
    }
    escaped.push_back(ch);
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
};

}  // namespace

std::string escapeSPEFName(std::string_view name)
{
  // IEEE 1481-1998 identifiers must escape hierarchy/bus delimiters inside
  // flattened names and every '$' (e.g. Yosys-generated "$_NAND_" names).
  const bool has_dot = name.find('.') != std::string_view::npos;
  const bool has_dollar = name.find('$') != std::string_view::npos;
  if (!has_dot && !has_dollar) {
    return std::string(name);
  }

  std::string escaped;
  escaped.reserve(name.size() + 4);
  for (std::size_t idx = 0; idx < name.size(); ++idx) {
    const char ch = name[idx];
    const bool is_dot_or_bracket = has_dot && (ch == '.' || ch == '[' || ch == ']');
    const bool is_dollar = ch == '$';
    if ((is_dot_or_bracket || is_dollar) && !hasPrecedingEscape(name, idx)) {
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

  auto emit_line = [&](const std::string& line, bool& modified) {
    const std::string escaped = escapeUnquotedDollarLine(line, modified);
    out << escaped << '\n';
  };

  SectionState state = SectionState::kNormal;
  std::string header_line;
  std::vector<std::string> pending_lines;
  bool modified = false;

  auto flush_pending = [&](bool keep_header, bool drop_conn_nodes) {
    if (keep_header) {
      emit_line(header_line, modified);
    } else {
      modified = true;
    }
    for (const std::string& pending : pending_lines) {
      if (drop_conn_nodes && startsWithKeyword(trimAscii(pending), "*N")) {
        modified = true;
        continue;
      }
      emit_line(pending, modified);
    }
    header_line.clear();
    pending_lines.clear();
    state = SectionState::kNormal;
  };

  std::string line;
  while (std::getline(in, line)) {
    bool reprocess = true;
    while (reprocess) {
      reprocess = false;
      const std::string_view trimmed = trimAscii(line);

      if (state == SectionState::kPendingNameMap) {
        if (isCommentOrBlank(trimmed)) {
          pending_lines.push_back(std::move(line));
          break;
        }
        const bool has_entry = trimmed.size() >= 2 && trimmed[0] == '*' && std::isdigit(static_cast<unsigned char>(trimmed[1])) != 0;
        flush_pending(has_entry, false);
        reprocess = true;
        continue;
      }

      if (state == SectionState::kPendingPorts) {
        if (isCommentOrBlank(trimmed)) {
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

      if (trimmed == "*NAME_MAP") {
        state = SectionState::kPendingNameMap;
        header_line = std::move(line);
      } else if (trimmed == "*PORTS") {
        state = SectionState::kPendingPorts;
        header_line = std::move(line);
      } else if (trimmed == "*CONN") {
        state = SectionState::kPendingConn;
        header_line = std::move(line);
      } else {
        emit_line(line, modified);
      }
    }
  }

  if (state == SectionState::kPendingNameMap) {
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
