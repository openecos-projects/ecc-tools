// SPDX-License-Identifier: MulanPSL-2.0
#pragma once
#include <memory>
#include <string>
namespace idb {
class LibLibrary;
}
namespace ista {
class TimingModel;
class LibertyLibrary;
class CharacterizedLibrary;
namespace liberty_writer {
void writeLib(TimingModel& tc_model, LibertyLibrary& timing_library, std::string& output_path);
std::unique_ptr<idb::LibLibrary> buildLib(CharacterizedLibrary& tc_lib, LibertyLibrary& timing_library);
}  // namespace liberty_writer
}  // namespace ista
