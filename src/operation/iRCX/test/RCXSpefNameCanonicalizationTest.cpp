// ***************************************************************************************
// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2026 Peng Cheng Laboratory
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
// ***************************************************************************************

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>

#include "LayerTable.hpp"
#include "LayoutData.hpp"
#include "Net.hpp"
#include "Pin.hpp"
#include "SpefParser.hh"
#include "TopoPool.hpp"
#include "builder/PlotSpefModelBuilder.hh"
#include "config/PlotSpefConfig.hh"
#include "model/PlotSpefModel.hh"
#include "reader/SpefReader.hh"
#include "select/PlotSpefSelect.hh"
#include "topology/SpefTopologyBuilder.hh"

namespace {

constexpr auto kMappedNetRaw = R"(mapped\\net)";
constexpr auto kMappedNet = R"(mapped\net)";
constexpr auto kMappedPinRaw = R"(mapped\\net:PIN\\A)";
constexpr auto kMappedPin = R"(mapped\net:PIN\A)";
constexpr auto kMappedNode = R"(mapped\net:node\B)";

constexpr auto kDirectNetRaw = R"(direct\\net\[0\])";
constexpr auto kDirectNet = R"(direct\net[0])";
constexpr auto kDirectPinRaw = R"(direct\\net\[0\]:PIN\\A)";
constexpr auto kDirectPin = R"(direct\net[0]:PIN\A)";
constexpr auto kDirectNode = R"(direct\net[0]:node\B)";

void require(const bool condition, const std::string& message)
{
  if (!condition) {
    throw std::runtime_error(message);
  }
}

class TemporarySpef
{
 public:
  TemporarySpef()
      : _path(std::filesystem::temp_directory_path() / "ircx_spef_name_canonicalization_test.spef")
  {
    std::ofstream output(_path);
    require(static_cast<bool>(output), "failed to create SPEF fixture");
    output << R"(*SPEF "IEEE 1481-1998"
*NAME_MAP
*1 mapped\\net
*D_NET *1 0.0
*CONN
*P *1:PIN\\A I *C 0 0 *LAYER 1 // $llx=0 $lly=0 $urx=4 $ury=4
*N *1:node\\B *C 10 0 *LAYER 1 // $llx=8 $lly=0 $urx=12 $ury=4
*CAP
1 *1:PIN\\A 0.1
*RES
1 *1:PIN\\A *1:node\\B 2.0
*END
*D_NET direct\\net\[0\] 0.0
*CONN
*P direct\\net\[0\]:PIN\\A I *C 20 0 *LAYER 1 // $llx=20 $lly=0 $urx=24 $ury=4
*N direct\\net\[0\]:node\\B *C 30 0 *LAYER 1 // $llx=28 $lly=0 $urx=32 $ury=4
*CAP
1 direct\\net\[0\]:PIN\\A 0.2
*RES
1 direct\\net\[0\]:PIN\\A direct\\net\[0\]:node\\B 3.0
*END
)";
  }

  ~TemporarySpef()
  {
    std::error_code ignored;
    std::filesystem::remove(_path, ignored);
  }

  [[nodiscard]] std::string string() const { return _path.string(); }

 private:
  std::filesystem::path _path;
};

const ircx::compare_spef::Net& requireCompareNet(const ircx::compare_spef::Data& data, const std::string& name)
{
  const auto* net = data.findNet(name);
  require(net != nullptr, "missing compare_spef net: " + name);
  return *net;
}

const ircx::plot_spef::Net& requirePlotNet(const ircx::plot_spef::Model& model, const std::string& name)
{
  const auto it = std::find_if(model.nets.begin(), model.nets.end(), [&name](const auto& net) { return net.name == name; });
  require(it != model.nets.end(), "missing plot_spef net: " + name);
  return *it;
}

std::size_t plotNetIndex(const ircx::plot_spef::Model& model, const std::string& name)
{
  for (std::size_t index = 0; index < model.nets.size(); ++index) {
    if (model.nets[index].name == name) {
      return index;
    }
  }
  throw std::runtime_error("missing plot_spef net index: " + name);
}

void testCompareSpefReadsSemanticNames(const std::string& path)
{
  ircx::compare_spef::Data data;
  const ircx::compare_spef::SpefReader reader;
  require(reader.read(path, data), "compare_spef failed to read fixture");

  const auto& mapped = requireCompareNet(data, kMappedNet);
  require(mapped.pins.size() == 2u, "mapped compare_spef pin count differs");
  require(mapped.pins.front().name == kMappedPin, "mapped NAME_MAP suffix lost its literal slash");
  require(mapped.resistors.size() == 1u && mapped.resistors.front().node1 == kMappedPin && mapped.resistors.front().node2 == kMappedNode,
          "mapped compare_spef resistor names are not semantic");

  const auto& direct = requireCompareNet(data, kDirectNet);
  require(direct.pins.size() == 2u, "direct compare_spef pin count differs");
  require(direct.pins.front().name == kDirectPin, "direct escaped pin lost its literal slash");
  require(direct.resistors.size() == 1u && direct.resistors.front().node1 == kDirectPin && direct.resistors.front().node2 == kDirectNode,
          "direct compare_spef resistor names are not semantic");
}

void testPlotSpefUsesOneEscapePass(const std::string& path)
{
  spef::SpefReader reader;
  require(reader.read(path), "plot_spef failed to parse fixture");
  reader.expandName();
  const auto* exchange = reader.getSpefFile();
  require(exchange != nullptr, "plot_spef parser returned no exchange");

  ircx::plot_spef::Config config;
  config.spef_file = path;
  config.dbu = 1;
  const ircx::plot_spef::Model model = ircx::plot_spef::ModelBuilder{}.build(*exchange, config);

  const auto& mapped = requirePlotNet(model, kMappedNet);
  const auto* mapped_pin = ircx::plot_spef::findNode(mapped, kMappedPin);
  require(mapped_pin != nullptr && mapped_pin->has_box, "raw NAME_MAP scan token did not match semantic mapped node");
  require(mapped.resistors.size() == 1u && mapped.resistors.front().node1 == kMappedPin && mapped.resistors.front().node2 == kMappedNode,
          "plot_spef mapped node suffix lost its literal slash");

  const auto& direct = requirePlotNet(model, kDirectNet);
  const auto* direct_pin = ircx::plot_spef::findNode(direct, kDirectPin);
  require(direct_pin != nullptr && direct_pin->has_box, "raw direct scan token did not match semantic direct node");
  require(direct.resistors.size() == 1u && direct.resistors.front().node1 == kDirectPin && direct.resistors.front().node2 == kDirectNode,
          "plot_spef direct node suffix lost its literal slash");

  auto mapped_config = config;
  mapped_config.net_name = "*1";
  const auto mapped_visibility = ircx::plot_spef::makeVisibleObjects(model, *exchange, mapped_config);
  require(mapped_visibility.netVisible(plotNetIndex(model, kMappedNet)), "plot_spef NAME_MAP net filter double-decoded literal slash");
  require(!mapped_visibility.netVisible(plotNetIndex(model, kDirectNet)), "plot_spef NAME_MAP net filter selected the wrong net");

  auto direct_config = config;
  direct_config.net_name = kDirectNetRaw;
  const auto direct_visibility = ircx::plot_spef::makeVisibleObjects(model, *exchange, direct_config);
  require(direct_visibility.netVisible(plotNetIndex(model, kDirectNet)), "plot_spef direct net filter double-decoded literal slash");
  require(!direct_visibility.netVisible(plotNetIndex(model, kMappedNet)), "plot_spef direct net filter selected the wrong net");

  auto edge_config = config;
  edge_config.edge_name = "*1:1";
  const auto edge_visibility = ircx::plot_spef::makeVisibleObjects(model, *exchange, edge_config);
  const auto mapped_index = plotNetIndex(model, kMappedNet);
  require(edge_visibility.netVisible(mapped_index) && edge_visibility.nets[mapped_index].resistorTarget(0),
          "plot_spef edge filter double-decoded mapped literal slash");
}

ircx::Net makeLayoutNet(const std::string& raw_net_name, const std::string& raw_pin_name)
{
  ircx::Net net;
  net.set_net_name(raw_net_name);
  ircx::Pin pin;
  pin.set_pin_name(raw_pin_name);
  net.get_pin_list().push_back(std::move(pin));
  return net;
}

bool hasPinName(std::span<ircx::TopoNode> nodes, const std::string& name)
{
  return std::any_of(nodes.begin(), nodes.end(), [&name](ircx::TopoNode& node) { return node.get_pin_name() == name; });
}

void testTopologyMatchesRawLayoutNames(const std::string& path)
{
  ircx::LayoutData layout;
  layout.set_dbu_per_micron(1);
  layout.get_net_list().push_back(makeLayoutNet(kMappedNetRaw, kMappedPinRaw));
  layout.get_net_list().push_back(makeLayoutNet(kDirectNetRaw, kDirectPinRaw));

  ircx::LayerTable layer_table;
  layer_table.register_design_layer(1, "M1");

  ircx::TopoPool topo_pool;
  ircx::run_rcx_from_topology::SpefTopologyBuilder builder(topo_pool);
  require(builder.build(layout, layer_table, path, true), "topology builder did not match raw layout names with expanded SPEF names");
  require(topo_pool.get_net_node_list(0).size() == 2u && topo_pool.get_net_edge_list(0).size() == 1u,
          "topology builder lost mapped escaped net topology");
  require(topo_pool.get_net_node_list(1).size() == 2u && topo_pool.get_net_edge_list(1).size() == 1u,
          "topology builder lost direct escaped net topology");
  require(hasPinName(topo_pool.get_net_node_list(0), kMappedPinRaw), "topology builder did not preserve mapped raw pin identity");
  require(hasPinName(topo_pool.get_net_node_list(1), kDirectPinRaw), "topology builder did not preserve direct raw pin identity");
}

}  // namespace

int main()
{
  try {
    const TemporarySpef fixture;
    testCompareSpefReadsSemanticNames(fixture.string());
    testPlotSpefUsesOneEscapePass(fixture.string());
    testTopologyMatchesRawLayoutNames(fixture.string());
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
