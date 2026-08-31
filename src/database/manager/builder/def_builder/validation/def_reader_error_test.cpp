// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2. This file is a regression test.
// ***************************************************************************************

#include "def_read.h"

#include <filesystem>
#include <fstream>
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

void testComponentFailureIsReported()
{
  const auto def_path = std::filesystem::temp_directory_path() / "def_reader_error_test.def";
  {
    std::ofstream def_file(def_path);
    def_file << "VERSION 5.8 ;\n"
             << "DESIGN def_reader_error_test ;\n"
             << "UNITS DISTANCE MICRONS 1000 ;\n"
             << "COMPONENTS 1 ;\n"
             << "- missing_instance MISSING_MASTER ;\n"
             << "END COMPONENTS\n"
             << "END DESIGN\n";
  }

  idb::IdbLayout layout;
  idb::IdbDefService service(&layout);
  idb::DefRead reader(&service);
  const bool parsed = reader.createDb(def_path.c_str());
  std::filesystem::remove(def_path);

  require(!parsed, "DEF reader must fail when a component master is missing");
  const auto* error = reader.get_last_error();
  require(error != nullptr, "DEF reader failure must provide structured error data");
  require(error->file_path == def_path.string(), "error report must identify the input file");
  require(error->line_number > 0, "error report must identify the source line");
  require(error->stage == "component", "error report must identify the failing callback");
  require(error->status == kDbFail, "error report must preserve the callback failure status");
  require(!error->message.empty(), "error report must include a failure message");
}

}  // namespace

int main()
{
  try {
    testComponentFailureIsReported();
  } catch (const std::exception& error) {
    std::cout << error.what() << '\n';
    return 1;
  }
  return 0;
}
