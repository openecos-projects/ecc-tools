// SPDX-License-Identifier: MulanPSL-2.0
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

#include "Logger.hpp"
#include "Utility.hpp"

static void require(bool ok, const char* message)
{
  if (!ok)
    throw std::runtime_error(message);
}
static std::string read(const std::filesystem::path& path)
{
  std::ifstream file(path);
  return {std::istreambuf_iterator<char>(file), {}};
}
int main()
{
  const auto path = std::filesystem::temp_directory_path() / ("ista-resource-" + std::to_string(getpid()));
  const auto second = path.string() + ".next";
  auto& log = ista::Logger::getInst();
  log.info(ista::Loc::current(), "before-open");
  log.openLogFileStream(path.string());
  log.info(ista::Loc::current(), "first-file");
  log.openLogFileStream(second);
  log.info(ista::Loc::current(), "second-file");
  log.closeLogFileStream();
  log.closeLogFileStream();
  log.info(ista::Loc::current(), "between-files");
  bool failed = false;
  try {
    log.openLogFileStream(path.string() + "/missing");
  } catch (const std::exception&) {
    failed = true;
  }
  require(failed, "invalid log destination accepted");
  require(read(path).find("before-open") < read(path).find("first-file"), "buffered log order changed");
  require(read(second).find("second-file") != std::string::npos, "reopened log lost data");
  log.openLogFileStream(second);
  log.info(ista::Loc::current(), "after-reopen");
  ista::Logger::destroyInst();
  require(read(second).find("between-files") < read(second).find("after-reopen"), "closed log retained a dangling stream");
  failed = false;
  try {
    auto file = ista::Utility::openOutputFile(path.string() + "/missing");
  } catch (const std::exception&) {
    failed = true;
  }
  require(failed, "invalid output destination accepted");
  // Linux /dev/full opens successfully and rejects writes/flush.
  if (std::filesystem::exists("/dev/full")) {
    failed = false;
    try {
      auto file = ista::Utility::openOutputFile("/dev/full");
      file << "buffered";
      ista::Utility::finishOutputFile(file);
    } catch (const std::exception&) {
      failed = true;
    }
    require(failed, "failed flush reported success");
  }
  std::filesystem::remove(path);
  std::filesystem::remove(second);
}
