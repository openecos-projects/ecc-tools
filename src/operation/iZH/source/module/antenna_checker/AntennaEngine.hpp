// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
// ***************************************************************************************
#pragma once

#include "AFComParam.hpp"
#include "AntennaResult.hpp"
#include "ZHHeader.hpp"

namespace izh {

#define ZHAE (izh::AntennaEngine::getInst())

class AntennaEngine
{
 public:
  static void initInst();
  static AntennaEngine& getInst();
  static void destroyInst();

  AntennaResult checkAndFix(std::map<std::string, std::any> config_map);

 private:
  static AntennaEngine* _ae_instance;

  AntennaEngine() = default;
  AntennaEngine(const AntennaEngine& other) = delete;
  AntennaEngine(AntennaEngine&& other) = delete;
  ~AntennaEngine() = default;
  AntennaEngine& operator=(const AntennaEngine& other) = delete;
  AntennaEngine& operator=(AntennaEngine&& other) = delete;

  AFComParam initParam(std::map<std::string, std::any>& config_map);
  void writeFixReport(const AntennaResult& result, const AFComParam& param) const;
};

}  // namespace izh
