// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// ***************************************************************************************

#include "view_json_io.h"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message)
{
  if (!condition) {
    throw std::runtime_error(message);
  }
}

std::filesystem::path makeTempDir()
{
  auto dir = std::filesystem::temp_directory_path() / "view_json_io_test";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);
  return dir;
}

void testDumpFormat()
{
  idb::ViewJson json;
  json["schema"] = "ecc.view.v1";
  json["data"] = idb::ViewJson::array({1, 2});

  const std::string pretty = idb::dumpViewJson(json, idb::ViewJsonFormat::kPretty);
  const std::string compact = idb::dumpViewJson(json, idb::ViewJsonFormat::kCompact);

  require(pretty.find('\n') != std::string::npos, "pretty json must contain newlines");
  require(compact.find('\n') == std::string::npos, "compact json must not contain newlines");
  require(compact == R"({"schema":"ecc.view.v1","data":[1,2]})", "compact json must not contain extra spaces");
}

void testStoredPath()
{
  idb::ViewJsonWriteOptions options;
  options.compress = true;

  require(idb::storedViewJsonPath("manifest.json", options) == "manifest.json", "manifest must stay uncompressed");
  require(idb::storedViewJsonPath("design/instances.json", options) == "design/instances.json.gz",
          "compressed package files must use .gz suffix");
  require(idb::storedViewJsonPath("regular_wires.json", options) == "regular_wires.json.gz",
          "compressed index file references must use .gz suffix");
}

void testGzipRoundTrip()
{
  auto dir = makeTempDir();
  const auto gzip_path = dir / "layout_edits.json.gz";

  std::string error;
  require(idb::writeViewJsonText(gzip_path, R"({"kind":"layout_edits","data":[1]})", true, error), error);

  std::string content;
  require(idb::readViewJsonText(gzip_path.string(), false, content, error), error);
  require(content == R"({"kind":"layout_edits","data":[1]})", "gzip content must round-trip");

  const auto legacy_path = dir / "layout_edits.json";
  content.clear();
  require(idb::readViewJsonText(legacy_path.string(), true, content, error), error);
  require(content == R"({"kind":"layout_edits","data":[1]})", "compressed read should find path + .gz");

  require(idb::writeViewJsonText(legacy_path, R"({"kind":"layout_edits","data":[2]})", false, error), error);
  content.clear();
  require(idb::readViewJsonText(legacy_path.string(), true, content, error), error);
  require(content == R"({"kind":"layout_edits","data":[1]})", "compressed read should prefer path + .gz over plain path");
}

void testFormatParser()
{
  idb::ViewJsonFormat format = idb::ViewJsonFormat::kPretty;
  require(idb::parseViewJsonFormat("compact", format), "compact format must parse");
  require(format == idb::ViewJsonFormat::kCompact, "compact format must select compact enum");
  require(idb::parseViewJsonFormat("PRETTY", format), "format parser must be case-insensitive");
  require(format == idb::ViewJsonFormat::kPretty, "pretty format must select pretty enum");
  require(!idb::parseViewJsonFormat("binary", format), "invalid format must be rejected");
}

}  // namespace

int main()
{
  try {
    testDumpFormat();
    testStoredPath();
    testGzipRoundTrip();
    testFormatParser();
  } catch (const std::exception& error) {
    std::cerr << error.what() << std::endl;
    return 1;
  }
  return 0;
}
