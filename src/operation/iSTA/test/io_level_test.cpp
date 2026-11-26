// SPDX-License-Identifier: MulanPSL-2.0
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "DelayCalculator.hpp"
#include "TimingAnalyzer.hpp"
using namespace ista;

int main()
{
  // Enumerate periodic clock events independently of the production pairing
  // logic. A level output selects its closing edge for setup and its opposite
  // opening edge for hold. This prescribed-input fixture does not model time borrowing.
  for (int duty = 0; duty < 2; ++duty)
    for (int input_level : {0, 1})
      for (int output_level : {0, 1})
        for (int in_fall : {0, 1})
          for (int out_fall : {0, 1})
            for (int sign : {-1, 1}) {
              Database db;
              Config config;
              for (const auto* name : {"in", "out"}) {
                auto& pin = db.network().pins()[name];
                pin.set_full_name(name);
                pin.set_pin_name(name);
                pin.set_is_port(true);
                pin.set_net_name("wire");
                pin.set_direction(std::string(name) == "in" ? PinDirection::kInput : PinDirection::kOutput);
              }
              auto& net = db.network().nets()["wire"];
              net.set_net_name("wire");
              net.set_pin_name_list({"in", "out"});
              auto& clock = db.sdc().get_clock_map()["V"];
              clock.set_clock_name("V");
              clock.set_period(10);
              clock.set_fall_edge(duty == 0 ? 3 : 7);
              for (auto mode : {AnalysisType::kMin, AnalysisType::kMax}) {
                TimingIoDelay in, out;
                for (auto* delay : {&in, &out}) {
                  delay->set_clock_name("V");
                  delay->set_analysis_type(mode);
                  delay->set_trans_type(TransType::kRise);
                }
                in.set_clock_trans_type(in_fall ? TransType::kFall : TransType::kRise);
                out.set_clock_trans_type(out_fall ? TransType::kFall : TransType::kRise);
                in.set_delay(sign * .2);
                out.set_delay(sign * .3);
                in.set_level_sensitive(input_level);
                out.set_level_sensitive(output_level);
                db.sdc().get_port_constraint_map()["in"].set_input_delays({in}, true);
                db.sdc().get_port_constraint_map()["out"].set_output_delays({out}, true);
              }
              db.sdc().get_port_constraint_map()["in"].set_input_transition(.05);
              DelayCalculator delay(db, config);
              TimingAnalyzer(db, delay).analyze();
              int count = 0;
              for (const auto& group : db.pathGroups())
                for (const auto& [endpoint, paths] : group.endpointPaths())
                  for (const auto& path : paths) {
                    if (endpoint != "out" || path.get_trans_type() != TransType::kRise)
                      continue;
                    ++count;
                    const bool setup = path.get_analysis_type() == AnalysisType::kMax;
                    const double period = clock.get_period();
                    const double launch = in_fall ? clock.get_fall_edge() : clock.get_rise_edge();
                    const bool capture_fall = !setup && output_level ? !out_fall : out_fall;
                    const double capture_phase = capture_fall ? clock.get_fall_edge() : clock.get_rise_edge();
                    double capture = setup ? std::numeric_limits<double>::infinity() : -std::numeric_limits<double>::infinity();
                    for (int cycle : {-1, 0, 1}) {
                      const double event = capture_phase + cycle * period;
                      if (setup && (output_level ? event >= launch : event > launch))
                        capture = std::min(capture, event);
                      if (!setup && event <= launch)
                        capture = std::max(capture, event);
                    }
                    const double required = capture - sign * .3;
                    const double arrival = launch + sign * .2;
                    if (std::abs(path.get_required_time() - required) > 1e-8 || std::abs(path.get_path_delay() - arrival) > 1e-8
                        || std::abs(path.get_slack() - (setup ? required - arrival : arrival - required)) > 1e-8)
                      throw std::runtime_error("I/O result violates periodic capture-event ordering: duty=" + std::to_string(duty) + " input_level="
                                               + std::to_string(input_level) + " output_level=" + std::to_string(output_level) + " input_fall="
                                               + std::to_string(in_fall) + " output_fall=" + std::to_string(out_fall) + " setup=" + std::to_string(setup));
                  }
              if (count != 2)
                throw std::runtime_error("I/O matrix lost setup or hold path");
            }
}
