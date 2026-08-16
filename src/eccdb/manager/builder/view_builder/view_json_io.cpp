// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// ***************************************************************************************

#include "view_json_io.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <sstream>
#include <vector>

#include <zlib.h>

namespace idb {
namespace {

bool endsWith(const std::string& value, const std::string& suffix)
{
  return value.size() >= suffix.size() && value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string toLower(std::string value)
{
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

bool readPlainText(const std::filesystem::path& path, std::string& content, std::string& error)
{
  std::ifstream stream(path, std::ios::binary);
  if (!stream.is_open()) {
    error = "cannot open " + path.string();
    return false;
  }

  std::ostringstream buffer;
  buffer << stream.rdbuf();
  content = buffer.str();
  if (stream.bad()) {
    error = "read " + path.string() + " failed";
    return false;
  }
  return true;
}

bool readGzipText(const std::filesystem::path& path, std::string& content, std::string& error)
{
  gzFile file = gzopen(path.string().c_str(), "rb");
  if (file == nullptr) {
    error = "cannot open " + path.string();
    return false;
  }

  std::array<char, 16384> buffer{};
  content.clear();
  int read_size = 0;
  while ((read_size = gzread(file, buffer.data(), static_cast<unsigned int>(buffer.size()))) > 0) {
    content.append(buffer.data(), static_cast<size_t>(read_size));
  }

  if (read_size < 0) {
    int error_number = Z_OK;
    const char* message = gzerror(file, &error_number);
    error = "read " + path.string() + " failed";
    if (message != nullptr) {
      error += ": ";
      error += message;
    }
    gzclose(file);
    return false;
  }

  if (gzclose(file) != Z_OK) {
    error = "close " + path.string() + " failed";
    return false;
  }
  return true;
}

}  // namespace

std::string dumpViewJson(const ViewJson& json, ViewJsonFormat format)
{
  return format == ViewJsonFormat::kPretty ? json.dump(2) : json.dump();
}

bool parseViewJsonFormat(const std::string& value, ViewJsonFormat& format)
{
  const std::string lower = toLower(value);
  if (lower == "pretty") {
    format = ViewJsonFormat::kPretty;
    return true;
  }
  if (lower == "compact") {
    format = ViewJsonFormat::kCompact;
    return true;
  }
  return false;
}

std::string viewJsonFormatName(ViewJsonFormat format)
{
  return format == ViewJsonFormat::kPretty ? "pretty" : "compact";
}

std::string storedViewJsonPath(const std::string& relative_path, const ViewJsonWriteOptions& options)
{
  if (!options.compress || relative_path == "manifest.json" || endsWith(relative_path, ".gz")) {
    return relative_path;
  }
  return relative_path + ".gz";
}

bool writeViewJsonText(const std::filesystem::path& path, const std::string& content, bool compress, std::string& error)
{
  if (!compress) {
    std::ofstream stream(path, std::ios::binary);
    if (!stream.is_open()) {
      error = "cannot open " + path.string();
      return false;
    }
    stream << content;
    if (!stream.good()) {
      error = "write " + path.string() + " failed";
      return false;
    }
    return true;
  }

  gzFile file = gzopen(path.string().c_str(), "wb");
  if (file == nullptr) {
    error = "cannot open " + path.string();
    return false;
  }

  const char* data = content.data();
  size_t remaining = content.size();
  while (remaining > 0) {
    const unsigned int chunk = static_cast<unsigned int>(std::min<size_t>(remaining, 1u << 30));
    const int written = gzwrite(file, data, chunk);
    if (written == 0) {
      int error_number = Z_OK;
      const char* message = gzerror(file, &error_number);
      error = "write " + path.string() + " failed";
      if (message != nullptr) {
        error += ": ";
        error += message;
      }
      gzclose(file);
      return false;
    }
    data += written;
    remaining -= static_cast<size_t>(written);
  }

  if (gzclose(file) != Z_OK) {
    error = "close " + path.string() + " failed";
    return false;
  }
  return true;
}

bool readViewJsonText(const std::string& path, bool compressed_hint, std::string& content, std::string& error)
{
  std::vector<std::filesystem::path> candidates;
  if (compressed_hint && !endsWith(path, ".gz")) {
    candidates.emplace_back(path + ".gz");
  }
  candidates.emplace_back(path);

  for (const auto& candidate : candidates) {
    if (!std::filesystem::is_regular_file(candidate)) {
      continue;
    }
    return endsWith(candidate.string(), ".gz") ? readGzipText(candidate, content, error) : readPlainText(candidate, content, error);
  }

  error = "cannot open " + path;
  if (compressed_hint && !endsWith(path, ".gz")) {
    error += " or " + path + ".gz";
  }
  return false;
}

}  // namespace idb
