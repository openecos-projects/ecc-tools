// SPDX-License-Identifier: MulanPSL-2.0
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "LayerTable.hpp"
#include "LayoutData.hpp"
#include "SPEFSanitizer.hpp"
#include "SpefParser.hh"
#include "TopoPool.hpp"
#include "builder/PlotSpefModelBuilder.hh"
#include "config/PlotSpefConfig.hh"
#include "model/PlotSpefModel.hh"
#include "reader/SpefReader.hh"
#include "select/PlotSpefSelect.hh"
#include "topology/SpefTopologyBuilder.hh"

namespace {

void require(bool condition, const std::string& message)
{
  if (!condition) {
    throw std::runtime_error(message);
  }
}

const std::string kHeader = R"(*SPEF "IEEE 1481-1999"
*DESIGN "spef_regression"
*DATE "2026-11-25"
*VENDOR "iEDA"
*PROGRAM "iRCX"
*VERSION "test"
*DESIGN_FLOW "PIN_CAP NONE"
*DIVIDER /
*DELIMITER :
*BUS_DELIMITER [ ]
*T_UNIT 1 NS
*C_UNIT 1 FF
*R_UNIT 1 OHM
*L_UNIT 1 HENRY
)";

class Fixture
{
 public:
  Fixture()
  {
    auto pattern = (std::filesystem::temp_directory_path() / "ircx-spef-XXXXXX").string();
    require(mkdtemp(pattern.data()) != nullptr, "cannot create SPEF fixture directory");
    _directory = pattern;
  }
  ~Fixture()
  {
    std::error_code error;
    std::filesystem::remove_all(_directory, error);
  }
  auto write(const std::string& body) const -> std::filesystem::path
  {
    const auto path = _directory / "test.spef";
    std::ofstream out(path);
    out.exceptions(std::ios::failbit | std::ios::badbit);
    out << kHeader << body;
    return path;
  }
  static auto read(const std::filesystem::path& path) -> std::string
  {
    std::ifstream in(path);
    require(in.is_open(), "cannot read SPEF fixture");
    return {std::istreambuf_iterator<char>(in), {}};
  }
  auto sanitize(const std::string& body) const -> std::string
  {
    const auto path = write(body);
    require(ircx::sanitizeSPEFFile(path), "SPEF sanitization failed");
    const auto result = read(path);
    require(ircx::sanitizeSPEFFile(path), "second SPEF sanitization failed");
    require(read(path) == result, "SPEF sanitization is not idempotent");
    return result;
  }

 private:
  std::filesystem::path _directory;
};

void checkDecode()
{
  // IEEE 1481-1999 9.3.3.2: consume the escape prefix, not the escaped character.
  require(spef::removeEscapes(R"(n\\x)") == R"(n\x)", "decoder lost a literal backslash");
  require(spef::removeEscapes(R"(n\\\$x)") == R"(n\$x)", "decoder mishandled an odd backslash run");
  require(spef::removeEscapes(R"(\A\_\$\\)") == R"(A_$\)", "decoder mishandled consecutive escape pairs");
  spef::Exchange exchange("test.spef");
  exchange.index_to_name_map[1] = "inst";
  require(spef::expandName(exchange, R"(*1:p\\x)") == R"(inst:p\x)", "indexed pin suffix lost a backslash");

  Fixture fixture;
  const auto path = fixture.write(R"(*D_NET n\\x 1
*CONN
*I inst:p\\x I
*CAP
1 inst:p\\x 1
*END
)");
  spef::SpefReader reader;
  require(reader.read(path.string()), "cannot parse escaped names");
  reader.expandName();
  const auto& net = reader.getSpefFile()->nets.at(0);
  require(net.name == R"(n\x)" && net.conns.at(0).pin_port_name == R"(inst:p\x)"
              && net.caps.at(0).node1 == R"(inst:p\x)",
          "SPEF reader changed name identity");
  ircx::compare_spef::SpefReader compare_reader;
  ircx::compare_spef::Data data;
  require(compare_reader.read(path.string(), data), "compare reader rejected escaped names");
  require(data.nets.at(0).name == R"(n\x)" && data.nets.at(0).pins.at(0).name == R"(inst:p\x)",
          "compare reader changed name identity");
}

void checkNames()
{
  // Literal punctuation and structural bus delimiters have different roles (9.3.1.2 / 9.3.3.1).
  const std::vector<std::pair<std::string, std::string>> cases{
      {"sig-1", R"(sig\-1)"}, {"a@b", R"(a\@b)"}, {"bus[0]_reg_p", R"(bus\[0\]_reg_p)"},
      {"bus[0]", "bus[0]"}, {"u[1]/q[2]", "u[1]/q[2]"}, {"u.foo[3]", R"(u\.foo[3])"},
      {R"(a\/[0])", R"(a\/[0])"},
      {"bus[-1]", R"(bus\[\-1\])"}, {"a[0][1]", R"(a\[0\][1])"},
      {R"(bus\[0])", R"(bus\[0\])"}, {R"(bus[0\])", R"(bus\[0\])"},
      {"pin:1", R"(pin\:1)"}, {"sig_$x", R"(sig_\$x)"}, {R"(pre\$escaped)", R"(pre\$escaped)"},
      {R"(n\\x)", R"(n\\x)"}, {R"(tail\)", R"(tail\\)"},
      {R"(a!#%&`()*+,.;<=>?@^'{|}~"z)", R"(a\!\#\%\&\`\(\)\*\+\,\.\;\<\=\>\?\@\^\'\{\|\}\~\"z)"},
  };
  for (const auto& [input, expected] : cases) {
    require(ircx::escapeSPEFName(input) == expected, "incorrect SPEF escaping for " + input);
    require(ircx::escapeSPEFName(expected) == expected, "SPEF escaping is not idempotent for " + input);
  }
}

void checkComments()
{
  Fixture fixture;
  const std::vector<std::string> bodies{
      "*NAME_MAP\n/* legal $ comment */\n*1 n\n*D_NET *1 0\n*END\n",
      "*NAME_MAP /* comment */\n/* block starts\n*END\n*CAP\n// $ still a block comment\n*/ *1 n\n*D_NET *1 0\n*END\n",
      "*PORTS\np I\n*D_NET n 0\n*CONN\n/* comment */ *P p I\n*END\n",
      "*PORTS\np I\n*D_NET n 0\n*CONN /* starts\n*END\n*/ *P p I\n*END\n",
  };
  for (const auto& body : bodies) {
    require(fixture.sanitize(body) == kHeader + body, "sanitizer changed a valid commented section");
  }
  const std::string escaped = fixture.sanitize(
      "*NAME_MAP\n/* $keep\n*END\n*/ *1 n$x // $geometry=1\n*D_NET *1 0\n*END\n");
  require(escaped == kHeader + "*NAME_MAP\n/* $keep\n*END\n*/ *1 n\\$x // $geometry=1\n*D_NET *1 0\n*END\n",
          "sanitizer modified comment text or missed code after a block comment");
}

void checkEmptySections()
{
  Fixture fixture;
  auto result = fixture.sanitize("*D_NET n 0\n*CAP\n*RES\n*END\n");
  require(result.find("*CAP") == std::string::npos && result.find("*RES") == std::string::npos,
          "sanitizer kept empty CAP/RES sections");
  const std::string nonempty = "*D_NET n 1\n*CAP /* cap */\n/* value */ 1 n:1 1\n*RES\n// resistance\n1 n:1 n:2 2\n*END\n";
  require(fixture.sanitize(nonempty) == kHeader + nonempty, "sanitizer removed nonempty CAP/RES records");
  result = fixture.sanitize("*NAME_MAP /* map starts\nmap ends */\n*PORTS // ports\n*D_NET n 0\n"
                            "*CONN /* conn */\n*N n:1 *C 0 0 /* node starts\nnode ends */\n"
                            "*CAP /* cap starts\ncap ends */\n*RES // res\n*END\n");
  for (const auto* keyword : {"*NAME_MAP", "*PORTS", "*CONN", "*N n:1", "*CAP", "*RES"}) {
    require(result.find(keyword) == std::string::npos, "empty commented section retained: " + std::string(keyword));
  }
  for (const auto* comment : {"/* map starts\nmap ends */", "// ports", "/* conn */", "/* node starts\nnode ends */",
                            "/* cap starts\ncap ends */", "// res"}) {
    require(result.find(comment) != std::string::npos, "removing an empty section damaged a comment");
  }
}

void checkConsumers(bool topology_only)
{
  Fixture fixture;
  const auto path = fixture.write(R"(*D_NET n\\x 0
*CONN
*I n\\x:1 I *C 0 0 // $layer=1
*I n\\x:2 O *C 1 0 // $layer=1
*RES
1 n\\x:1 n\\x:2 2 // $layer=1 $llx=0 $lly=0 $urx=1 $ury=0.1
*END
)");
  if (!topology_only) {
    spef::SpefReader reader;
    require(reader.read(path.string()), "cannot parse consumer fixture");
    reader.expandName();
    ircx::plot_spef::Config config;
    config.spef_file = path.string();
    config.net_name = R"(n\\x)";
    config.output_resistance = true;
    const auto model = ircx::plot_spef::ModelBuilder().build(*reader.getSpefFile(), config);
    require(model.nets.at(0).resistors.at(0).has_box, "plot geometry scan decoded a name twice");
    const auto visible = ircx::plot_spef::makeVisibleObjects(model, *reader.getSpefFile(), config);
    require(visible.netVisible(0) && visible.nets.at(0).resistorVisible(0), "plot filter decoded a name twice");
    return;
  }

  ircx::LayoutData layout;
  layout.set_dbu_per_micron(1000);
  ircx::Net net;
  net.set_net_idx(0);
  net.set_net_name(R"(n\\x)");
  layout.get_net_list().push_back(net);
  ircx::LayerTable layers;
  layers.register_design_layer(1, "M1");
  ircx::TopoPool topology;
  require(ircx::run_rcx_from_topology::SpefTopologyBuilder(topology).build(layout, layers, path.string(), true),
          "topology import decoded an expanded net name twice");
  require(topology.get_net_node_list(0).size() == 2 && topology.get_net_edge_list(0).size() == 1,
          "topology import lost escaped-name nodes or edges");
}

}  // namespace

int main(int argc, char** argv)
{
  try {
    const std::string scenario = argc > 1 ? argv[1] : "decode";
    if (scenario == "decode") {
      checkDecode();
    } else if (scenario == "names") {
      checkNames();
    } else if (scenario == "comments") {
      checkComments();
    } else if (scenario == "empty_sections") {
      checkEmptySections();
    } else if (scenario == "plot_names") {
      checkConsumers(false);
    } else if (scenario == "topology_names") {
      checkConsumers(true);
    } else {
      throw std::runtime_error("unknown SPEF scenario: " + scenario);
    }
    std::cout << "PASS: " << scenario << '\n';
    return EXIT_SUCCESS;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
