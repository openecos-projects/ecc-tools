// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
// ***************************************************************************************
#pragma once

#include "MJHeader.hpp"

namespace imj {

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

struct GetACViolationTypeName
{
  std::string operator()(ACViolationType type) const
  {
    switch (type) {
      case ACViolationType::kNone:
        return "None";
      case ACViolationType::kAntennaPar:
        return "PAR";
      case ACViolationType::kAntennaDiffPar:
        return "DiffPAR";
      case ACViolationType::kAntennaCar:
        return "CAR";
      case ACViolationType::kAntennaDiffCar:
        return "DiffCAR";
      case ACViolationType::kAntennaPsr:
        return "PSR";
      case ACViolationType::kAntennaDiffPsr:
        return "DiffPSR";
      case ACViolationType::kAntennaCsr:
        return "CSR";
      case ACViolationType::kAntennaDiffCsr:
        return "DiffCSR";
      case ACViolationType::kAntennaCutPar:
        return "CutPAR";
      case ACViolationType::kAntennaCutCar:
        return "CutCAR";
      case ACViolationType::kAntennaDiffCutPar:
        return "DiffCutPAR";
      case ACViolationType::kAntennaDiffCutCar:
        return "DiffCutCAR";
      default:
        return "None";
    }
  }
};

}  // namespace imj
