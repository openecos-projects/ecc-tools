// SPDX-License-Identifier: MulanPSL-2.0
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

#include "RcTreeAnalyzer.hpp"

using namespace ista;
namespace {
void near(double actual, double expected, const char* message)
{
  if (!std::isfinite(actual) || std::abs(actual - expected) > 1e-10 * std::max(1., std::abs(expected)))
    throw std::runtime_error(message);
}
void resistor(ParasiticNet& net, const std::string& from, const std::string& to, double resistance)
{
  ParasiticResistor edge;
  edge.set_source_node(from);
  edge.set_sink_node(to);
  edge.set_resistance(resistance);
  net.get_resistor_list().push_back(edge);
}
void cycleAndReroot()
{
  Database db;
  PinCapacitanceCalculator pins(db);
  RcTreeAnalyzer rc(db, pins);
  ParasiticNet net;
  net.set_net_name("tree");
  net.get_node_map()["r"].set_capacitance(.1);
  net.get_node_map()["a"].set_capacitance(.2);
  net.get_node_map()["b"].set_capacitance(.3);
  net.get_node_map()["unconnected"].set_capacitance(9.);
  resistor(net, "r", "a", 100.);
  resistor(net, "a", "b", 200.);
  // A loop and a parallel edge must keep the same first-edge DFS tree.
  resistor(net, "r", "b", 999.);
  resistor(net, "r", "a", 777.);
  for (auto mode : {AnalysisType::kMax, AnalysisType::kMin}) {
    for (auto edge : {TransType::kRise, TransType::kFall}) {
      std::string root = "r";
      rc.analyze(net, root, mode, edge);
      near(rc.load("tree", mode, edge, "r"), .6, "root subtree load");
      near(rc.load("tree", mode, edge, "a"), .5, "child subtree load");
      near(rc.delay("tree", mode, edge, "a").value(), .05, "Elmore a");
      near(rc.delay("tree", mode, edge, "b").value(), .11, "Elmore b");
      // C*delay: b=.033, a=.043; beta(a)=.0043, beta(b)=.0109.
      near(rc.impulse("tree", mode, edge, "a").value(), .0061, "second moment a");
      near(rc.impulse("tree", mode, edge, "b").value(), .0097, "second moment b");
      if (rc.delay("tree", mode, edge, "unconnected") || rc.impulse("absent", mode, edge, "r"))
        throw std::runtime_error("missing RC value became valid");
      std::vector<std::string> names;
      std::vector<int32_t> parents;
      std::vector<double> resistances, caps;
      rc.buildTree(net, root, mode, edge, names, parents, resistances, caps);
      if (names != std::vector<std::string>{"r", "a", "b"} || parents != std::vector<int32_t>{-1, 0, 1})
        throw std::runtime_error("rooted traversal changed");
      near(resistances.at(2), .2, "Arnoldi resistance units");
      near(caps.at(2), .3, "Arnoldi capacitance");
      root = "b";
      rc.analyze(net, root, mode, edge);
      near(rc.load("tree", mode, edge, "b"), .6, "reroot load");
      near(rc.delay("tree", mode, edge, "r").value(), .07, "reroot delay");
      root = "r";
      rc.analyze(net, root, mode, edge);
      near(rc.delay("tree", mode, edge, "b").value(), .11, "restored root delay");
    }
  }
  net.get_node_map()["b"].set_capacitance(.6);
  rc.clear();
  std::string root = "r";
  rc.analyze(net, root, AnalysisType::kMax, TransType::kRise);
  near(rc.load("tree", AnalysisType::kMax, TransType::kRise, root), .9, "clear did not refresh changed capacitance");
  near(rc.delay("tree", AnalysisType::kMax, TransType::kRise, "b").value(), .2, "changed capacitance delay");
}
void orderedBranches()
{
  Database db;
  PinCapacitanceCalculator pins(db);
  RcTreeAnalyzer rc(db, pins);
  ParasiticNet net;
  net.set_net_name("branches");
  net.get_node_map()["a"].set_capacitance(1e16);
  net.get_node_map()["b"].set_capacitance(1.);
  net.get_node_map()["c"].set_capacitance(1.);
  resistor(net, "r", "a", 1e-16);
  resistor(net, "r", "b", 100.);
  resistor(net, "r", "c", 200.);
  std::string root = "r";
  rc.analyze(net, root, AnalysisType::kMax, TransType::kRise);
  // Original recursive DFS accumulates children in resistor-list order. Adding
  // the two small branches before the large branch changes the final double.
  if (rc.load("branches", AnalysisType::kMax, TransType::kRise, root) != ((0. + 1e16) + 1.) + 1.)
    throw std::runtime_error("sibling summation order changed");
  near(rc.delay("branches", AnalysisType::kMax, TransType::kRise, "b").value(), .1, "branch b delay");
  near(rc.delay("branches", AnalysisType::kMax, TransType::kRise, "c").value(), .2, "branch c delay");
  std::vector<std::string> names;
  std::vector<int32_t> parents;
  std::vector<double> resistance, capacitance;
  rc.buildTree(net, root, AnalysisType::kMax, TransType::kRise, names, parents, resistance, capacitance);
  if (names != std::vector<std::string>{"r", "a", "b", "c"} || parents != std::vector<int32_t>{-1, 0, 0, 0})
    throw std::runtime_error("branch traversal order changed");
}
void deepTree()
{
  Database db;
  PinCapacitanceCalculator pins(db);
  RcTreeAnalyzer rc(db, pins);
  ParasiticNet net;
  net.set_net_name("deep");
  constexpr int count = 50000;
  std::string previous;
  for (int index = 0; index < count; ++index) {
    const auto name = std::to_string(index);
    net.get_node_map()[name].set_capacitance(.001);
    if (index)
      resistor(net, previous, name, 1.);
    previous = name;
  }
  std::string root = "0";
  rc.analyze(net, root, AnalysisType::kMax, TransType::kRise);
  near(rc.load("deep", AnalysisType::kMax, TransType::kRise, root), 50., "deep tree load");
  near(rc.delay("deep", AnalysisType::kMax, TransType::kRise, previous).value(), (count - 1.) * count / 2. * 1e-6, "deep tree Elmore delay");
}
}  // namespace
int main(int argc, char** argv)
{
  if (argc > 1 && std::string(argv[1]) == "deep")
    deepTree();
  else {
    cycleAndReroot();
    orderedBranches();
  }
}
