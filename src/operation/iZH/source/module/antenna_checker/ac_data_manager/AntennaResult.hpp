// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
// ***************************************************************************************
#pragma once

#include "ACViolation.hpp"
#include "ZHHeader.hpp"

namespace izh {

enum class AFRepairType
{
  kNone,
  kHopUp,
  kJog,
  kDiode
};

class AFIterStat
{
 public:
  int32_t iter = 0;
  int32_t violation_num = 0;
  int32_t hop_up_applied = 0;
  int32_t hop_up_rejected = 0;
  int32_t jog_applied = 0;
  int32_t jog_rejected = 0;
  int32_t diode_applied = 0;
  int32_t diode_rejected = 0;
};

class AntennaResult
{
 public:
  std::vector<ACViolation>& get_violation_list() { return _violation_list; }
  const std::vector<ACViolation>& get_violation_list() const { return _violation_list; }
  int32_t get_violation_num() const { return static_cast<int32_t>(_violation_list.size()); }
  std::vector<AFIterStat>& get_iter_stat_list() { return _iter_stat_list; }
  const std::vector<AFIterStat>& get_iter_stat_list() const { return _iter_stat_list; }
  bool& get_logged_no_antenna_cell() { return _logged_no_antenna_cell; }
  bool get_logged_no_antenna_cell() const { return _logged_no_antenna_cell; }

 private:
  std::vector<ACViolation> _violation_list;
  std::vector<AFIterStat> _iter_stat_list;
  bool _logged_no_antenna_cell = false;
};

inline const char* acViolationTypeToString(ACViolationType type)
{
  switch (type) {
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
      return "UNKNOWN";
  }
}

inline bool acViolationIsCut(ACViolationType type)
{
  return type == ACViolationType::kAntennaCutPar || type == ACViolationType::kAntennaCutCar
         || type == ACViolationType::kAntennaDiffCutPar || type == ACViolationType::kAntennaDiffCutCar;
}

}  // namespace izh
