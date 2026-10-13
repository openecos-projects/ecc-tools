// ***************************************************************************************
// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2026 Peng Cheng Laboratory
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
// ***************************************************************************************

#include "SpefParser.hh"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(const bool condition, const std::string& message)
{
  if (!condition) {
    throw std::runtime_error(message);
  }
}

class TemporarySpef
{
 public:
  explicit TemporarySpef(const std::string& contents)
      : _path(std::filesystem::temp_directory_path() / "spef_name_expansion_test.spef")
  {
    std::ofstream output(_path);
    require(static_cast<bool>(output), "failed to create SPEF fixture");
    output << contents;
  }

  ~TemporarySpef()
  {
    std::error_code ignored;
    std::filesystem::remove(_path, ignored);
  }

  [[nodiscard]] const std::filesystem::path& path() const noexcept { return _path; }

 private:
  std::filesystem::path _path;
};

void testPairwiseEscapeDecoding()
{
  require(spef::removeEscapes(R"(bus\[0\])") == "bus[0]", "escaped brackets were not decoded");
  require(spef::removeEscapes(R"(foo\\bar)") == R"(foo\bar)", "literal escaped slash was lost");
  require(spef::removeEscapes(R"(dangling\)") == R"(dangling\)", "trailing unmatched slash was lost");
}

void testParserExpandsNameMapNamesAndSuffixes()
{
  TemporarySpef fixture{R"(*SPEF "IEEE 1481-1998"
*NAME_MAP
*1 bus\[0\]
*2 inst\\leaf
*3 dangling\
*D_NET *1 0.0
*CONN
*I *2:PIN I
*N *1:node\[0\]
*CAP
1 *1:node\[0\] 0.1
*RES
1 *1:node\[0\] *2:PIN 2.0
*D_NET *3 0.0
*CONN
*P *3 I
*END
)"};

  spef::SpefReader reader;
  require(reader.read(fixture.path().string()), "failed to parse SPEF fixture");
  reader.expandName();

  const auto* exchange = reader.getSpefFile();
  require(exchange != nullptr, "SPEF reader returned no exchange");
  require(exchange->nets.size() == 2u, "fixture must produce two nets");

  const auto& mapped = exchange->nets[0];
  require(mapped.name == "bus[0]", "NAME_MAP D_NET base name was not pairwise decoded");
  require(mapped.conns.size() == 2u, "mapped net connection count differs");
  require(mapped.conns[0].pin_port_name == R"(inst\leaf:PIN)", "NAME_MAP instance name lost its literal slash");
  require(mapped.conns[1].pin_port_name == "bus[0]:node[0]", "NAME_MAP suffix was not pairwise decoded");
  require(mapped.caps.size() == 1u && mapped.caps[0].node1 == "bus[0]:node[0]",
          "CAP node name was not expanded consistently");
  require(mapped.ress.size() == 1u && mapped.ress[0].node1 == "bus[0]:node[0]"
              && mapped.ress[0].node2 == R"(inst\leaf:PIN)",
          "RES node names were not expanded consistently");

  const auto& trailing = exchange->nets[1];
  require(trailing.name == R"(dangling\)", "NAME_MAP trailing unmatched slash was lost");
  require(trailing.conns.size() == 1u && trailing.conns[0].pin_port_name == R"(dangling\)",
          "NAME_MAP trailing unmatched slash did not propagate to connection");
}

void testParserExpandsDirectEscapedNamesWithoutNameMap()
{
  TemporarySpef fixture{R"(*SPEF "IEEE 1481-1998"
*D_NET direct\\leaf\[0\] 0.0
*CONN
*P direct\\leaf\[0\] I
*N direct\\leaf\[0\]:node\[0\]
*CAP
1 direct\\leaf\[0\]:node\[0\] 0.1
*RES
1 direct\\leaf\[0\]:node\[0\] direct\\leaf\[0\] 2.0
*END
)"};

  spef::SpefReader reader;
  require(reader.read(fixture.path().string()), "failed to parse direct-name SPEF fixture");
  reader.expandName();

  const auto* exchange = reader.getSpefFile();
  require(exchange != nullptr && exchange->nets.size() == 1u, "direct-name fixture must produce one net");
  const auto& direct = exchange->nets.front();
  constexpr auto kSemanticName = R"(direct\leaf[0])";
  constexpr auto kSemanticNode = R"(direct\leaf[0]:node[0])";
  require(direct.name == kSemanticName, "direct D_NET escaped name was not pairwise decoded");
  require(direct.conns.size() == 2u && direct.conns[0].pin_port_name == kSemanticName
              && direct.conns[1].pin_port_name == kSemanticNode,
          "direct CONN escaped names were not pairwise decoded");
  require(direct.caps.size() == 1u && direct.caps[0].node1 == kSemanticNode,
          "direct CAP escaped node name was not pairwise decoded");
  require(direct.ress.size() == 1u && direct.ress[0].node1 == kSemanticNode && direct.ress[0].node2 == kSemanticName,
          "direct RES escaped node names were not pairwise decoded");
}

}  // namespace

int main()
{
  try {
    testPairwiseEscapeDecoding();
    testParserExpandsNameMapNamesAndSuffixes();
    testParserExpandsDirectEscapedNamesWithoutNameMap();
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
