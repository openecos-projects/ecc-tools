#pragma once
#include <fstream>
#include <vector>

#include "file_manager.h"
#include "json.hpp"

namespace iplf {

using json = nlohmann::ordered_json;

class FileZHManager : public FileManager
{
 public:
  explicit FileZHManager(std::string data_path) : FileManager(data_path) {}
  ~FileZHManager() = default;

  virtual bool readFile() override;
  virtual bool saveFileData() override;

 private:
  bool saveJson();
};

}  // namespace iplf
