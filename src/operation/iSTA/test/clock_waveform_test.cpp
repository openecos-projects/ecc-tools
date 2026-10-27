// SPDX-License-Identifier: MulanPSL-2.0
#include <cmath>
#include <stdexcept>

#include "DelayCalculator.hpp"
#include "TimingAnalyzer.hpp"
using namespace ista;

int main()
{
  // Four-edge PT fixture {0,2,5,7}; second variant makes the second rising
  // edge the setup winner, proving path dominance preserves edge identity.
  const double setup[3][4] = {{5, 2, 3, 5}, {4, 2, 2, 4}, {4, 2, 2, 4}};
  const double hold[3][4] = {{0, -3, -2, 0}, {0, -2, -2, 0}, {0, -2, -2, 0}};
  for (int variant : {0, 1, 2})
    for (int from : {0, 1})
      for (int to : {0, 1}) {
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
        clock.set_rise_edge(0);
        clock.set_fall_edge(2);
        clock.set_waveform(variant == 2 ? std::vector<double>{0, 2, 4, 8} : variant ? std::vector<double>{0, 2, 6, 8} : std::vector<double>{0, 2, 5, 7});
        for (auto mode : {AnalysisType::kMin, AnalysisType::kMax}) {
          TimingIoDelay in, out;
          for (auto* delay : {&in, &out}) {
            delay->set_clock_name("V");
            delay->set_analysis_type(mode);
            delay->set_trans_type(TransType::kRise);
          }
          in.set_clock_trans_type(from ? TransType::kFall : TransType::kRise);
          out.set_clock_trans_type(to ? TransType::kFall : TransType::kRise);
          in.set_delay(.2);
          out.set_delay(.3);
          db.sdc().get_port_constraint_map()["in"].set_input_delays({in}, true);
          db.sdc().get_port_constraint_map()["out"].set_output_delays({out}, true);
        }
        db.sdc().get_port_constraint_map()["in"].set_input_transition(.05);
        DelayCalculator delay(db, config);
        TimingAnalyzer(db, delay).analyze();
        for (const auto* point : {"in", "out"})
          if (std::abs(db.graph().points().at(point).get_slack() - (setup[variant][from * 2 + to] - .5)) > 1e-8)
            throw std::runtime_error("graph required summary selected arrival rather than worst slack");
        int count = 0;
        for (const auto& group : db.pathGroups())
          for (const auto& [endpoint, paths] : group.endpointPaths())
            for (const auto& path : paths) {
              if (endpoint != "out" || path.get_trans_type() != TransType::kRise)
                continue;
              ++count;
              const bool maximum = path.get_analysis_type() == AnalysisType::kMax;
              const double expected = maximum ? setup[variant][from * 2 + to] - .5 : .5 - hold[variant][from * 2 + to];
              if (std::abs(path.get_slack() - expected) > 1e-8)
                throw std::runtime_error("multi-pulse timing collapsed distinct edges");
              if (variant == 1 && from == 0 && to == 0 && maximum
                  && (std::abs(path.get_launch_time() - 6) > 1e-8 || std::abs(path.get_capture_time() - 10) > 1e-8))
                throw std::runtime_error("worst path lost its second launch edge identity");
            }
        if (count != 2)
          throw std::runtime_error("multi-pulse test lost setup/hold paths");
      }
}
