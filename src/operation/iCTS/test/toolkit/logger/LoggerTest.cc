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
/**
 * @file LoggerTest.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-07-29
 * @brief Tests for the iCTS runtime Logger contract.
 */

#include <gtest/gtest.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "Logger.hh"

namespace icts {
namespace {

class ConsoleCapture
{
 public:
  ConsoleCapture() : _original(std::cout.rdbuf(_stream.rdbuf())) {}
  ~ConsoleCapture() { std::cout.rdbuf(_original); }

  auto str() const -> std::string { return _stream.str(); }

 private:
  std::ostringstream _stream;
  std::streambuf* _original = nullptr;
};

auto readFile(const std::filesystem::path& path) -> std::string
{
  std::ifstream stream(path);
  return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

class LoggerTestInterface : public ::testing::Test
{
 protected:
  void SetUp() override
  {
    Logger::destroyInst();
    Logger::initInst();
    _output_dir = std::filesystem::temp_directory_path() / ("icts_logger_test_" + std::to_string(getpid()));
    std::filesystem::remove_all(_output_dir);
    std::filesystem::create_directories(_output_dir);
  }

  void TearDown() override
  {
    Logger::destroyInst();
    std::filesystem::remove_all(_output_dir);
  }

  std::filesystem::path _output_dir;
};

TEST_F(LoggerTestInterface, BuffersPreOpenLinesAndMirrorsColoredConsole)
{
  const auto log_path = _output_dir / "cts.log";
  ConsoleCapture console;

  CTSLOG.info(icts::Loc::current(), "startup", "=", 1);
  CTSLOG.openLogFileStream(log_path.string());
  CTSLOG.warn(icts::Loc::current(), "recoverable warning");
  CTSLOG.closeLogFileStream();

  const auto console_text = console.str();
  const auto file_text = readFile(log_path);
  EXPECT_NE(console_text.find("\033[1;34mInfo\033[0m"), std::string::npos);
  EXPECT_NE(console_text.find("\033[1;33mWarn\033[0m"), std::string::npos);
  EXPECT_EQ(file_text.find("\033["), std::string::npos);
  EXPECT_LT(file_text.find("startup=1"), file_text.find("recoverable warning"));
  EXPECT_NE(file_text.find("LoggerTest.cc:"), std::string::npos);
  EXPECT_NE(file_text.find("[CTS "), std::string::npos);
}

TEST_F(LoggerTestInterface, ReopenTruncatesAndDoesNotLeakPreviousRun)
{
  const auto log_path = _output_dir / "cts.log";
  ConsoleCapture console;

  CTSLOG.openLogFileStream(log_path.string());
  CTSLOG.info(icts::Loc::current(), "first run");
  CTSLOG.closeLogFileStream();
  CTSLOG.openLogFileStream(log_path.string());
  CTSLOG.info(icts::Loc::current(), "second run");
  CTSLOG.closeLogFileStream();

  const auto file_text = readFile(log_path);
  EXPECT_EQ(file_text.find("first run"), std::string::npos);
  EXPECT_NE(file_text.find("second run"), std::string::npos);
}

TEST_F(LoggerTestInterface, ConcurrentWritersEmitCompleteLines)
{
  constexpr std::size_t thread_count = 8U;
  constexpr std::size_t lines_per_thread = 40U;
  const auto log_path = _output_dir / "cts.log";
  ConsoleCapture console;
  CTSLOG.openLogFileStream(log_path.string());

  std::vector<std::thread> workers;
  workers.reserve(thread_count);
  for (std::size_t thread_index = 0U; thread_index < thread_count; ++thread_index) {
    workers.emplace_back([thread_index]() -> void {
      for (std::size_t line_index = 0U; line_index < lines_per_thread; ++line_index) {
        CTSLOG.info(icts::Loc::current(), "thread=", thread_index, ",line=", line_index);
      }
    });
  }
  for (auto& worker : workers) {
    worker.join();
  }
  CTSLOG.closeLogFileStream();

  std::istringstream lines(readFile(log_path));
  std::string line;
  std::size_t line_count = 0U;
  while (std::getline(lines, line)) {
    EXPECT_EQ(line.rfind("[CTS ", 0U), 0U);
    EXPECT_NE(line.find(" Info "), std::string::npos);
    EXPECT_NE(line.find("thread="), std::string::npos);
    EXPECT_NE(line.find(",line="), std::string::npos);
    ++line_count;
  }
  EXPECT_EQ(line_count, thread_count * lines_per_thread);
}

TEST_F(LoggerTestInterface, ErrorTerminatesWithFailureStatus)
{
  const auto child = fork();
  ASSERT_GE(child, 0);
  if (child == 0) {
    Logger::destroyInst();
    Logger::initInst();
    CTSLOG.error(icts::Loc::current(), "terminal invariant");
  }

  int status = 0;
  ASSERT_EQ(waitpid(child, &status, 0), child);
  ASSERT_TRUE(WIFEXITED(status));
  EXPECT_EQ(WEXITSTATUS(status), EXIT_FAILURE);
}

}  // namespace
}  // namespace icts
