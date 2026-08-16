// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// ***************************************************************************************

#pragma once

#include <filesystem>
#include <string>

#include "json.hpp"

namespace idb {

using ViewJson = nlohmann::ordered_json;

enum class ViewJsonFormat
{
  kPretty,
  kCompact,
};

struct ViewJsonWriteOptions
{
  ViewJsonFormat format = ViewJsonFormat::kPretty;
  bool compress = false;
};

std::string dumpViewJson(const ViewJson& json, ViewJsonFormat format);
bool parseViewJsonFormat(const std::string& value, ViewJsonFormat& format);
std::string viewJsonFormatName(ViewJsonFormat format);
std::string storedViewJsonPath(const std::string& relative_path, const ViewJsonWriteOptions& options);
bool writeViewJsonText(const std::filesystem::path& path, const std::string& content, bool compress, std::string& error);
bool readViewJsonText(const std::string& path, bool compressed_hint, std::string& content, std::string& error);

}  // namespace idb
