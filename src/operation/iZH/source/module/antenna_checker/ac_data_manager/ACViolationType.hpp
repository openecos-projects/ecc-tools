// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
// ***************************************************************************************
#pragma once

namespace izh {

enum class ACViolationType
{
  kNone,
  kAntennaPar,
  kAntennaDiffPar,
  kAntennaCar,
  kAntennaDiffCar,
  kAntennaPsr,
  kAntennaDiffPsr,
  kAntennaCsr,
  kAntennaDiffCsr,
  kAntennaCutPar,
  kAntennaCutCar,
  kAntennaDiffCutPar,
  kAntennaDiffCutCar
};

}  // namespace izh
