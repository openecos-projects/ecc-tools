// SPDX-License-Identifier: MulanPSL-2.0
#include <cassert>

#include "EndpointQualification.hpp"
#include "advance/Database.hpp"
#include "advance/TimingArc.hpp"
#include "advance/TimingCheckArc.hpp"

int main()
{
  using namespace ista;
  Database db;
  db.get_timing_constraint().get_clock_map()["declared"] = TimingClock{};
  TimingCheckArc check;
  check.set_clock_port("latch:GN");
  check.set_data_port("latch:D");
  check.set_check_type(TimingCheckType::kSetup);
  check.set_clock_trans_type(TransType::kRise);
  TimingArc arc;
  arc.get_check_table_map()[TransType::kRise] = TimingTable{};
  // A fallback clock name does not prove that a clock reached the pin.
  auto& point = db.get_timing_point_map()["latch:GN"];
  point.set_clock_name("declared");
  auto actual = qualifyMaxCheck(db, check, arc);
  assert((actual.valid == std::array<bool, 2>{false, false}));
  assert((actual.reason == std::array{TimingMaxQualificationReason::kNoCaptureClockState,
                                    TimingMaxQualificationReason::kNoCaptureClockState}));
  point.get_clock_state("undeclared").arrival_map[AnalysisType::kMin][TransType::kRise] = 0;
  assert((qualifyMaxCheck(db, check, arc).valid == std::array<bool, 2>{false, false}));
  point.get_clock_state("declared").arrival_map[AnalysisType::kMin][TransType::kRise] = 0;
  assert((qualifyMaxCheck(db, check, arc).valid == std::array<bool, 2>{true, false}));
  // The same connectivity rule applies to recovery and ordinary registers.
  check.set_check_type(TimingCheckType::kRecovery);
  check.set_data_port("latch:RN");
  arc.get_check_table_map().clear();
  arc.get_check_table_map()[TransType::kFall] = TimingTable{};
  assert((qualifyMaxCheck(db, check, arc).valid == std::array<bool, 2>{false, true}));
  check.set_check_type(TimingCheckType::kRemoval);
  assert((qualifyMaxCheck(db, check, arc).valid == std::array<bool, 2>{false, false}));
  check.set_check_type(TimingCheckType::kSetup);
  check.set_clock_port("ff:CK");
  db.get_timing_point_map()["ff:CK"].get_clock_state("declared").arrival_map[AnalysisType::kMin][TransType::kRise] = 0;
  assert((qualifyMaxCheck(db, check, arc).valid == std::array<bool, 2>{false, true}));

  auto& port = db.get_timing_constraint().get_port_constraint_map()["output"];
  assert((qualifyMaxOutput(db, "output").valid == std::array<bool, 2>{false, false}));
  TimingIoDelay delay;
  delay.set_clock_name("declared");
  delay.set_trans_type(TransType::kRise);
  port.set_output_delays({delay}, false);
  assert((qualifyMaxOutput(db, "output").valid == std::array<bool, 2>{true, false}));
  TimingException exception;
  exception.set_type(TimingExceptionType::kFalsePath);
  db.get_timing_constraint().get_path_exception_list().push_back(exception);
  assert((qualifyMaxOutput(db, "output").reason == std::array{TimingMaxQualificationReason::kUnsupportedConstraint,
                                                           TimingMaxQualificationReason::kUnsupportedConstraint}));
}
