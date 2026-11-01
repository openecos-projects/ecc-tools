// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

//
// Shared parser-excluded logical-netlist fixture loader for the OpenDB/EccDB
// construction and lookup benchmarks.  DbOnlyFixture intentionally contains
// no placement, BTerms, geometry, routing, or other physical data.

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <numeric>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include <boost/json.hpp>

struct DbOnlyFixture
{
  struct Master
  {
    std::string name;
    std::vector<std::string> terms;
  };

  struct Instance
  {
    std::string name;
    std::size_t master_index;
  };

  struct Connection
  {
    std::size_t net_index;
    std::size_t instance_index;
    std::size_t term_index;
  };

  std::vector<Master> masters;
  std::vector<Instance> instances;
  std::vector<std::string> nets;
  std::vector<Connection> connections;

  static DbOnlyFixture load(const std::filesystem::path& path);
};

// The caller supplies a fixed seed, so every benchmark has reproducible
// non-sequential instance, net, and connection probes.
inline std::vector<std::size_t> shuffledIndices(std::size_t count, uint64_t seed)
{
  std::vector<std::size_t> indices(count);
  std::iota(indices.begin(), indices.end(), std::size_t{0});
  std::mt19937_64 generator(seed);
  std::shuffle(indices.begin(), indices.end(), generator);
  return indices;
}

namespace db_only_fixture_detail {

[[noreturn]] inline void fail(std::string_view message)
{
  throw std::runtime_error("DbOnlyFixture: " + std::string(message));
}

inline boost::json::string_view jsonStringView(std::string_view value)
{
  return {value.data(), value.size()};
}

inline const boost::json::value& member(const boost::json::object& object, std::string_view name)
{
  const auto iterator = object.find(jsonStringView(name));
  if (iterator == object.end()) fail("missing required key '" + std::string(name) + "'");
  return iterator->value();
}

inline const boost::json::object& objectValue(const boost::json::value& value, std::string_view context)
{
  if (!value.is_object()) fail(std::string(context) + " must be an object");
  return value.as_object();
}

inline const boost::json::array& arrayValue(const boost::json::value& value, std::string_view context)
{
  if (!value.is_array()) fail(std::string(context) + " must be an array");
  return value.as_array();
}

inline std::string stringValue(const boost::json::value& value, std::string_view context)
{
  if (!value.is_string()) fail(std::string(context) + " must be a string");
  const auto& input = value.as_string();
  std::string result(input.data(), input.size());
  if (result.empty() || result.find('\0') != std::string::npos) {
    fail(std::string(context) + " must be a non-empty string without NUL bytes");
  }
  return result;
}

inline std::size_t indexValue(const boost::json::value& value, std::string_view context)
{
  uint64_t result = 0;
  if (value.is_uint64()) {
    result = value.as_uint64();
  } else if (value.is_int64() && value.as_int64() >= 0) {
    result = static_cast<uint64_t>(value.as_int64());
  } else {
    fail(std::string(context) + " must be a non-negative integer");
  }
  if (result > std::numeric_limits<std::size_t>::max()) {
    fail(std::string(context) + " does not fit in size_t");
  }
  return static_cast<std::size_t>(result);
}

inline void validateScope(const boost::json::value& value)
{
  const auto& scope = objectValue(value, "scope");
  static_cast<void>(stringValue(member(scope, "masters"), "scope.masters"));
  static_cast<void>(stringValue(member(scope, "nets"), "scope.nets"));
  static_cast<void>(stringValue(member(scope, "connections"), "scope.connections"));
  const auto& excluded = arrayValue(member(scope, "excluded"), "scope.excluded");
  if (excluded.empty()) fail("scope.excluded must not be empty");
  for (const auto& excluded_value : excluded) {
    static_cast<void>(stringValue(excluded_value, "scope.excluded entry"));
  }
}

inline void validate(const DbOnlyFixture& fixture)
{
  std::size_t term_count = 0;
  std::set<std::string> master_names;
  std::string previous_master_name;
  bool have_previous_master = false;
  std::vector<bool> referenced_masters(fixture.masters.size(), false);
  for (const auto& master : fixture.masters) {
    if (master.name.empty() || master.name.find('\0') != std::string::npos) fail("master name is invalid");
    if (have_previous_master && !(previous_master_name < master.name)) {
      fail("masters must be strictly sorted by name");
    }
    if (!master_names.insert(master.name).second) fail("duplicate master name");
    previous_master_name = master.name;
    have_previous_master = true;

    std::set<std::string> term_names;
    for (const auto& term : master.terms) {
      if (term.empty() || term.find('\0') != std::string::npos) fail("master term name is invalid");
      if (!term_names.insert(term).second) fail("duplicate master term name");
    }
    if (master.terms.size() > std::numeric_limits<std::size_t>::max() - term_count) {
      fail("master term count overflows size_t");
    }
    term_count += master.terms.size();
  }

  std::set<std::string> instance_names;
  std::string previous_instance_name;
  bool have_previous_instance = false;
  for (const auto& instance : fixture.instances) {
    if (instance.name.empty() || instance.name.find('\0') != std::string::npos) fail("instance name is invalid");
    if (have_previous_instance && !(previous_instance_name < instance.name)) {
      fail("instances must be strictly sorted by name");
    }
    if (!instance_names.insert(instance.name).second) fail("duplicate instance name");
    if (instance.master_index >= fixture.masters.size()) fail("instance has a dangling master index");
    referenced_masters[instance.master_index] = true;
    previous_instance_name = instance.name;
    have_previous_instance = true;
  }
  for (const bool referenced : referenced_masters) {
    if (!referenced) fail("fixture contains a master not referenced by an instance");
  }

  std::set<std::string> net_names;
  std::string previous_net_name;
  bool have_previous_net = false;
  for (const auto& net : fixture.nets) {
    if (net.empty() || net.find('\0') != std::string::npos) fail("net name is invalid");
    if (have_previous_net && !(previous_net_name < net)) fail("nets must be strictly sorted by name");
    if (!net_names.insert(net).second) fail("duplicate net name");
    previous_net_name = net;
    have_previous_net = true;
  }

  std::set<std::pair<std::size_t, std::size_t>> connected_terms;
  std::tuple<std::size_t, std::size_t, std::size_t> previous_connection;
  bool have_previous_connection = false;
  for (const auto& connection : fixture.connections) {
    if (connection.net_index >= fixture.nets.size() || connection.instance_index >= fixture.instances.size()) {
      fail("connection has a dangling net or instance index");
    }
    const auto master_index = fixture.instances[connection.instance_index].master_index;
    if (connection.term_index >= fixture.masters[master_index].terms.size()) {
      fail("connection has a dangling master-term index");
    }
    const auto current_connection = std::tie(connection.net_index, connection.instance_index, connection.term_index);
    if (have_previous_connection && !(previous_connection < current_connection)) {
      fail("connections must be strictly sorted by integer tuple");
    }
    if (!connected_terms.emplace(connection.instance_index, connection.term_index).second) {
      fail("an instance term is connected to more than one fixture net");
    }
    previous_connection = current_connection;
    have_previous_connection = true;
  }
}

inline void validateCounts(const boost::json::value& value, const DbOnlyFixture& fixture)
{
  const auto& counts = objectValue(value, "counts");
  std::size_t term_count = 0;
  for (const auto& master : fixture.masters) term_count += master.terms.size();
  const std::pair<std::string_view, std::size_t> expected[] = {
      {"masters", fixture.masters.size()},
      {"master_terms", term_count},
      {"instances", fixture.instances.size()},
      {"nets", fixture.nets.size()},
      {"connections", fixture.connections.size()},
  };
  for (const auto& [name, expected_value] : expected) {
    if (indexValue(member(counts, name), "counts." + std::string(name)) != expected_value) {
      fail("counts." + std::string(name) + " does not match the fixture collections");
    }
  }
}

}  // namespace db_only_fixture_detail

inline DbOnlyFixture DbOnlyFixture::load(const std::filesystem::path& path)
{
  if (!std::filesystem::is_regular_file(path)) {
    throw std::runtime_error("DbOnlyFixture: fixture is not a regular file: " + path.string());
  }
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("DbOnlyFixture: failed to open fixture: " + path.string());
  const std::string payload((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  if (input.bad()) throw std::runtime_error("DbOnlyFixture: failed to read fixture: " + path.string());

  boost::json::value document;
  try {
    document = boost::json::parse(payload);
  } catch (const std::exception& error) {
    throw std::runtime_error("DbOnlyFixture: invalid JSON in " + path.string() + ": " + error.what());
  }
  const auto& root = db_only_fixture_detail::objectValue(document, "root");
  if (db_only_fixture_detail::stringValue(db_only_fixture_detail::member(root, "schema"), "schema")
      != "eccdb_odb_logical_v1") {
    db_only_fixture_detail::fail("unsupported schema");
  }
  db_only_fixture_detail::validateScope(db_only_fixture_detail::member(root, "scope"));

  DbOnlyFixture fixture;
  const auto& masters = db_only_fixture_detail::arrayValue(db_only_fixture_detail::member(root, "masters"), "masters");
  fixture.masters.reserve(masters.size());
  for (std::size_t index = 0; index < masters.size(); ++index) {
    const auto& master = db_only_fixture_detail::objectValue(masters[index], "master");
    Master parsed_master{
        .name = db_only_fixture_detail::stringValue(db_only_fixture_detail::member(master, "name"), "master.name"),
        .terms = {}};
    const auto& terms = db_only_fixture_detail::arrayValue(db_only_fixture_detail::member(master, "terms"), "master.terms");
    parsed_master.terms.reserve(terms.size());
    for (const auto& term : terms) {
      parsed_master.terms.push_back(db_only_fixture_detail::stringValue(term, "master.term"));
    }
    fixture.masters.push_back(std::move(parsed_master));
  }

  const auto& instances = db_only_fixture_detail::arrayValue(db_only_fixture_detail::member(root, "instances"), "instances");
  fixture.instances.reserve(instances.size());
  for (const auto& instance : instances) {
    const auto& values = db_only_fixture_detail::arrayValue(instance, "instance");
    if (values.size() != 2) db_only_fixture_detail::fail("instance must be [name, master_index]");
    fixture.instances.push_back(
        {.name = db_only_fixture_detail::stringValue(values[0], "instance.name"),
         .master_index = db_only_fixture_detail::indexValue(values[1], "instance.master_index")});
  }

  const auto& nets = db_only_fixture_detail::arrayValue(db_only_fixture_detail::member(root, "nets"), "nets");
  fixture.nets.reserve(nets.size());
  for (const auto& net : nets) fixture.nets.push_back(db_only_fixture_detail::stringValue(net, "net"));

  const auto& connections =
      db_only_fixture_detail::arrayValue(db_only_fixture_detail::member(root, "connections"), "connections");
  fixture.connections.reserve(connections.size());
  for (const auto& connection : connections) {
    const auto& values = db_only_fixture_detail::arrayValue(connection, "connection");
    if (values.size() != 3) db_only_fixture_detail::fail("connection must be [net_index, instance_index, term_index]");
    fixture.connections.push_back(
        {.net_index = db_only_fixture_detail::indexValue(values[0], "connection.net_index"),
         .instance_index = db_only_fixture_detail::indexValue(values[1], "connection.instance_index"),
         .term_index = db_only_fixture_detail::indexValue(values[2], "connection.term_index")});
  }

  db_only_fixture_detail::validate(fixture);
  db_only_fixture_detail::validateCounts(db_only_fixture_detail::member(root, "counts"), fixture);
  return fixture;
}
