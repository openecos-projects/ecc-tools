// SPDX-License-Identifier: MulanPSL-2.0
// Compare saved backend checkpoints through a shared parser. The iFP physical
// and electrical contract is the gate; lossless represented differences remain
// diagnostic evidence rather than private-ID or DEF-text comparisons.
#include <gtest/gtest.h>
#include <json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <vector>

#include "DesignSemanticSnapshot.h"
#include "IfpSemanticSnapshot.h"
#include "StructuredDesignSemanticDiffer.h"
#include "def/DefDesignImporter.h"
#include "lef/LefLibraryImporter.h"
#include "lef/LefTechImporter.h"

using Json = nlohmann::json;
using namespace eccdb;

static Json sectionDiff(const std::vector<std::string>& lhs, const std::vector<std::string>& rhs)
{
  std::vector<std::string> only_left, only_right;
  std::set_difference(lhs.begin(), lhs.end(), rhs.begin(), rhs.end(), std::back_inserter(only_left));
  std::set_difference(rhs.begin(), rhs.end(), lhs.begin(), lhs.end(), std::back_inserter(only_right));
  Json result{{"equal", lhs == rhs}, {"left_count", lhs.size()}, {"right_count", rhs.size()},
              {"left_only_count", only_left.size()}, {"right_only_count", only_right.size()}};
  // A PG-net key can contain hundreds of thousands of shapes. Compare the
  // complete key above; bound only diagnostic previews, never the comparison.
  const auto samples = [](const std::vector<std::string>& keys) {
    Json values = Json::array();
    for (std::size_t index = 0; index < std::min<std::size_t>(keys.size(), 10u); ++index) {
      const auto& key = keys[index];
      if (key.size() <= 2000u) {
        values.push_back(key);
      } else {
        values.push_back({{"key_preview", key.substr(0, 2000u)}, {"full_key_bytes", key.size()}, {"truncated", true}});
      }
    }
    return values;
  };
  result["left_only_sample"] = samples(only_left);
  result["right_only_sample"] = samples(only_right);
  return result;
}

int main(int argc, char** argv)
{
  if (argc != 5) {
    std::cerr << "Usage: semantic-probe manifest.json left.def right.def report.json\n";
    return 2;
  }
  Json report{{"status", "running"}, {"left", argv[2]}, {"right", argv[3]},
              {"comparison", "shared EccDB importer, canonical iFP semantics; neither side is ground truth"},
              {"primary_scope", {"die/core", "rows/tracks/gcells", "instances/placement", "terminal PORT groups",
                                 "net connectivity/attributes", "route primitives", "expanded generated via geometry"}},
              {"limitation", "Saved DEF comparison does not prove identical adapter work-model inputs or DRC legality."}};
  try {
    std::ifstream input(argv[1]);
    const Json manifest = Json::parse(input);
    const auto base = std::filesystem::absolute(argv[1]).parent_path();
    std::vector<std::filesystem::path> lefs;
    for (const auto& entry : manifest.at("lef_files")) {
      std::filesystem::path path = entry.get<std::string>();
      lefs.push_back(path.is_absolute() ? path : base / path);
    }
    TechStore technology;
    LefTechImporter tech_importer(technology);
    tech_importer.import(lefs);
    LibraryStore library(technology.techRegistry());
    LefLibraryImporter library_importer(technology, library);
    library_importer.import(lefs);
    DesignStore lhs(technology.techRegistry(), library.libraryRegistry());
    DesignStore rhs(technology.techRegistry(), library.libraryRegistry());
    DefDesignImporter left_importer(lhs), right_importer(rhs);
    left_importer.import(argv[2]);
    right_importer.import(argv[3]);
    const auto diagnostics = [](const auto& importer) {
      Json result = Json::array();
      for (const auto& item : importer.diagnostics()) {
        result.push_back({{"statement", item.statement}, {"count", item.occurrence_count}});
      }
      return result;
    };
    report["diagnostics"] = {{"library", diagnostics(library_importer)},
                              {"left_def", diagnostics(left_importer)}, {"right_def", diagnostics(right_importer)}};
    const auto a = test::makeDesignSemanticSnapshot(lhs, false);
    const auto b = test::makeDesignSemanticSnapshot(rhs, false);
    // Counts include representation details such as Wire container count. They
    // are evidence, not a substitute for canonical primitive comparison.
    report["component_counts"] = {{"left", a.component_counts}, {"right", b.component_counts},
                                  {"equal", a.component_counts == b.component_counts}};
    report["global"] = {{"equal", a.global == b.global}, {"left", a.global}, {"right", b.global}};
    Json sections = Json::object();
    const auto add = [&](const char* name, const auto& left, const auto& right) { sections[name] = sectionDiff(left, right); };
    add("rows", a.rows, b.rows);
    add("tracks", a.track_grids, b.track_grids);
    add("gcells", a.gcell_grids, b.gcell_grids);
    add("instances", a.instances, b.instances);
    add("instance_pins", a.instance_pins, b.instance_pins);
    add("io_pins", a.io_pins, b.io_pins);
    add("vias", a.vias, b.vias);
    add("non_default_rules", a.non_default_rules, b.non_default_rules);
    add("regions", a.regions, b.regions);
    add("groups", a.groups, b.groups);
    add("blockages", a.blockages, b.blockages);
    add("fills", a.fills, b.fills);
    report["sections"] = sections;

    std::set<std::pair<bool, std::string>> left_names, right_names;
    Json mismatch_sample = Json::array();
    std::size_t mismatches = 0;
    const auto& left_storage = lhs.netlistStorage();
    const auto& right_storage = rhs.netlistStorage();
    for (const auto id : left_storage.nets()) {
      const auto& net = left_storage.net(id);
      const bool special = left_storage.isSpecialNet(id);
      left_names.emplace(special, net.name);
      const auto other = special ? right_storage.findSpecialNet(net.name) : right_storage.findRegularNet(net.name);
      if (!other) continue;
      const auto x = test::structured::canonicalNet(lhs, id);
      const auto y = test::structured::canonicalNet(rhs, other);
      if (x != y) {
        ++mismatches;
        if (mismatch_sample.size() < 20) {
          mismatch_sample.push_back({{"net", net.name}, {"special", special},
                                     {"first_difference", test::canonicalNetMismatch(x, y)}});
        }
      }
    }
    for (const auto id : right_storage.nets()) {
      right_names.emplace(right_storage.isSpecialNet(id), right_storage.net(id).name);
    }
    std::vector<std::pair<bool, std::string>> left_only, right_only;
    std::set_difference(left_names.begin(), left_names.end(), right_names.begin(), right_names.end(), std::back_inserter(left_only));
    std::set_difference(right_names.begin(), right_names.end(), left_names.begin(), left_names.end(), std::back_inserter(right_only));
    const auto left_missing_count = left_only.size(), right_missing_count = right_only.size();
    if (left_only.size() > 20) left_only.resize(20);
    if (right_only.size() > 20) right_only.resize(20);
    const bool nets_equal = mismatches == 0 && left_missing_count == 0 && right_missing_count == 0;
    report["nets"] = {{"equal", nets_equal}, {"left_count", left_names.size()}, {"right_count", right_names.size()},
                      {"common_net_mismatches", mismatches}, {"mismatch_sample", mismatch_sample},
                      {"left_only_count", left_missing_count}, {"right_only_count", right_missing_count},
                      {"left_only_sample", left_only}, {"right_only_sample", right_only}};
    bool equal = nets_equal && a.global == b.global;
    for (const auto& section : sections) equal = equal && section.at("equal").get<bool>();
    report["represented_semantics_equal"] = equal;
    // iFP's physical equivalence is a different contract from a lossless DEF
    // record comparison. Reuse the existing canonical keys, while resolving
    // generated Via aliases by their complete geometry and preserving PORT
    // groups. Raw representation differences above remain diagnostic evidence.
    const auto ifp_a = test::makeIfpSemanticSnapshot(technology, library, lhs);
    const auto ifp_b = test::makeIfpSemanticSnapshot(technology, library, rhs);
    Json ifp_sections = Json::object();
    const auto ifp_add = [&](const char* name, const auto& left, const auto& right) {
      ifp_sections[name] = sectionDiff(left, right);
    };
    ifp_add("rows", ifp_a.rows, ifp_b.rows);
    ifp_add("tracks", ifp_a.track_grids, ifp_b.track_grids);
    ifp_add("gcells", ifp_a.gcell_grids, ifp_b.gcell_grids);
    ifp_add("instances", ifp_a.instances, ifp_b.instances);
    ifp_add("instance_pins", ifp_a.instance_pins, ifp_b.instance_pins);
    ifp_add("io_pins", ifp_a.io_pins, ifp_b.io_pins);
    ifp_add("static_vias", ifp_a.vias, ifp_b.vias);
    ifp_add("nets", ifp_a.nets, ifp_b.nets);
    bool ifp_equal = ifp_a.complete && ifp_b.complete && ifp_a.global == ifp_b.global && ifp_a.core == ifp_b.core;
    for (const auto& section : ifp_sections) ifp_equal = ifp_equal && section.at("equal").get<bool>();
    report["ifp_semantics"] = {
        {"left_complete", ifp_a.complete}, {"right_complete", ifp_b.complete},
        {"left_unsupported", ifp_a.unsupported}, {"right_unsupported", ifp_b.unsupported},
        {"global_equal", ifp_a.global == ifp_b.global}, {"core_equal", ifp_a.core == ifp_b.core},
        {"sections", std::move(ifp_sections)}};
    report["ifp_semantics_equal"] = ifp_equal;
    report["status"] = "completed";
    std::ofstream(argv[4]) << report.dump(2) << '\n';
    std::cout << Json{{"status", "completed"}, {"represented_semantics_equal", equal},
                     {"ifp_semantics_equal", ifp_equal}, {"net_mismatches", mismatches}}.dump() << '\n';
    return ifp_equal ? 0 : 1;
  } catch (const std::exception& error) {
    report["status"] = "failed";
    report["error"] = error.what();
    std::ofstream(argv[4]) << report.dump(2) << '\n';
    std::cerr << report.dump(2) << '\n';
    return 2;
  }
}
