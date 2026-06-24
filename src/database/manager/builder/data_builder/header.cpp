// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************

#include "header.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace idb {
namespace data_binary {
namespace {

constexpr std::array<char, 8> kMagic = {'I', 'D', 'B', 'D', 'A', 'T', 'A', '\0'};
constexpr const char* kLayoutDir = "layout";
constexpr const char* kDesignDir = "design";

class BinaryWriter
{
 public:
  BinaryWriter(const std::filesystem::path& path, ArchiveSection section) : _path(path), _section(section)
  {
    std::filesystem::create_directories(path.parent_path());
    _out.open(path, std::ios::binary | std::ios::trunc);
    if (!_out) {
      throw std::runtime_error("open write file failed: " + path.string());
    }
    write_raw(kMagic.data(), kMagic.size());
    write(kArchiveVersion);
    write(static_cast<uint32_t>(_section));
  }

  template <typename T>
  void write(const T& value)
  {
    static_assert(std::is_trivially_copyable_v<T>, "binary pod write requires trivially copyable type");
    write_raw(reinterpret_cast<const char*>(&value), sizeof(T));
  }

  template <typename Enum>
  void write_enum(Enum value)
  {
    using Underlying = std::underlying_type_t<Enum>;
    write(static_cast<Underlying>(value));
  }

  void write_bool(bool value)
  {
    const uint8_t raw = value ? 1 : 0;
    write(raw);
  }

  void write_string(const std::string& value)
  {
    const uint64_t size = value.size();
    write(size);
    if (size > 0) {
      write_raw(value.data(), static_cast<std::streamsize>(size));
    }
  }

  bool good() const { return _out.good(); }

 private:
  void write_raw(const char* data, std::streamsize size)
  {
    _out.write(data, size);
    if (!_out) {
      throw std::runtime_error("write file failed: " + _path.string());
    }
  }

  std::filesystem::path _path;
  ArchiveSection _section;
  std::ofstream _out;
};

class BinaryReader
{
 public:
  BinaryReader(const std::filesystem::path& path, ArchiveSection expected_section) : _path(path)
  {
    _in.open(path, std::ios::binary);
    if (!_in) {
      throw std::runtime_error("open read file failed: " + path.string());
    }

    std::array<char, 8> magic{};
    read_raw(magic.data(), magic.size());
    if (magic != kMagic) {
      throw std::runtime_error("invalid idb data magic: " + path.string());
    }

    read(_version);
    if (!is_archive_version_supported(_version)) {
      throw std::runtime_error("idb data version mismatch: " + path.string() + " data_version=" + std::to_string(_version)
                               + " current_version=" + std::to_string(kArchiveVersion));
    }

    uint32_t section = 0;
    read(section);
    if (section != static_cast<uint32_t>(expected_section)) {
      throw std::runtime_error("unexpected idb data section: " + path.string());
    }
  }

  template <typename T>
  void read(T& value)
  {
    static_assert(std::is_trivially_copyable_v<T>, "binary pod read requires trivially copyable type");
    read_raw(reinterpret_cast<char*>(&value), sizeof(T));
  }

  template <typename Enum>
  Enum read_enum()
  {
    using Underlying = std::underlying_type_t<Enum>;
    Underlying raw{};
    read(raw);
    return static_cast<Enum>(raw);
  }

  bool read_bool()
  {
    uint8_t raw = 0;
    read(raw);
    return raw != 0;
  }

  std::string read_string()
  {
    uint64_t size = 0;
    read(size);
    std::string value;
    value.resize(static_cast<size_t>(size));
    if (size > 0) {
      read_raw(value.data(), static_cast<std::streamsize>(size));
    }
    return value;
  }

  uint32_t version() const { return _version; }

 private:
  void read_raw(char* data, std::streamsize size)
  {
    const auto offset = _in.tellg();
    _in.read(data, size);
    if (!_in) {
      const auto actual = _in.gcount();
      const auto file_size = std::filesystem::exists(_path) ? std::filesystem::file_size(_path) : uintmax_t{0};
      throw std::runtime_error("read file failed: " + _path.string() + " offset=" + std::to_string(static_cast<long long>(offset))
                               + " requested=" + std::to_string(size) + " actual=" + std::to_string(actual)
                               + " file_size=" + std::to_string(file_size));
    }
  }

  std::filesystem::path _path;
  std::ifstream _in;
  uint32_t _version = 0;
};

std::filesystem::path section_path(const std::string& folder, const char* group, const char* name)
{
  return std::filesystem::path(folder) / group / (std::string(name) + ".idb");
}

template <typename Func>
bool run_section(const char* name, Func&& func)
{
  try {
    func();
    return true;
  } catch (const std::exception& e) {
    std::cerr << "[IdbData] " << name << " failed: " << e.what() << std::endl;
    return false;
  }
}

bool run_parallel(std::vector<std::pair<const char*, std::function<void()>>> jobs, bool parallel)
{
  if (!parallel || jobs.size() <= 1) {
    bool ok = true;
    for (auto& [name, job] : jobs) {
      ok = run_section(name, job) && ok;
    }
    return ok;
  }

  std::vector<std::future<bool>> futures;
  futures.reserve(jobs.size());
  for (auto& [name, job] : jobs) {
    futures.emplace_back(std::async(std::launch::async, [name, job = std::move(job)]() { return run_section(name, job); }));
  }

  bool ok = true;
  for (auto& future : futures) {
    ok = future.get() && ok;
  }
  return ok;
}

std::string layer_name(IdbLayer* layer)
{
  return layer == nullptr ? std::string() : layer->get_name();
}

std::string via_name(IdbVia* via)
{
  return via == nullptr ? std::string() : via->get_name();
}

std::string cell_master_name(IdbCellMaster* master)
{
  return master == nullptr ? std::string() : master->get_name();
}

std::string site_name(IdbSite* site)
{
  return site == nullptr ? std::string() : site->get_name();
}

std::string rect_signature(IdbRect* rect)
{
  if (rect == nullptr) {
    return "null";
  }
  std::ostringstream oss;
  oss << rect->get_low_x() << ',' << rect->get_low_y() << ',' << rect->get_high_x() << ',' << rect->get_high_y();
  return oss.str();
}

std::string via_master_generate_signature(IdbViaMasterGenerate* generate)
{
  if (generate == nullptr) {
    return "null";
  }
  std::ostringstream oss;
  oss << generate->get_rule_name() << '|'
      << (generate->get_rule_generate() == nullptr ? std::string() : generate->get_rule_generate()->get_name()) << '|'
      << generate->get_cut_size_x() << ',' << generate->get_cut_size_y() << '|'
      << layer_name(generate->get_layer_bottom()) << ',' << layer_name(generate->get_layer_cut()) << ',' << layer_name(generate->get_layer_top())
      << '|' << generate->get_cut_spcing_x() << ',' << generate->get_cut_spcing_y() << '|'
      << generate->get_enclosure_bottom_x() << ',' << generate->get_enclosure_bottom_y() << ',' << generate->get_enclosure_top_x() << ','
      << generate->get_enclosure_top_y() << '|' << generate->get_cut_rows() << ',' << generate->get_cut_cols() << '|'
      << generate->get_original_offset_x() << ',' << generate->get_original_offset_y() << '|' << generate->get_offset_bottom_x() << ','
      << generate->get_offset_bottom_y() << ',' << generate->get_offset_top_x() << ',' << generate->get_offset_top_y() << '|';
  for (auto* rect : generate->get_cut_rect_list()) {
    oss << rect_signature(rect) << ';';
  }
  oss << '|' << rect_signature(generate->get_cut_bouding_rect()) << '|';
  oss << (generate->get_patttern() == nullptr ? std::string() : generate->get_patttern()->get_pattern_string());
  return oss.str();
}

std::string via_master_signature(IdbViaMaster* master)
{
  if (master == nullptr) {
    return "null";
  }
  std::ostringstream oss;
  oss << master->get_name() << '|' << master->is_default() << '|' << static_cast<int>(master->get_type()) << '|'
      << master->get_cut_rows() << ',' << master->get_cut_cols() << '|' << rect_signature(master->get_cut_rect()) << '|'
      << via_master_generate_signature(master->get_master_generate()) << '|';
  for (auto* fixed : master->get_master_fixed_list()) {
    oss << layer_name(fixed == nullptr ? nullptr : fixed->get_layer()) << ':';
    if (fixed != nullptr) {
      for (auto* rect : fixed->get_rect_list()) {
        oss << rect_signature(rect) << ',';
      }
    }
    oss << ';';
  }
  return oss.str();
}

IdbViaMaster* find_matching_via_master(IdbVias* known_vias, IdbVia* via)
{
  if (known_vias == nullptr || via == nullptr) {
    return nullptr;
  }

  auto* master = via->get_instance();
  if (master == nullptr) {
    return nullptr;
  }

  const auto signature = via_master_signature(master);
  if (!via->get_name().empty()) {
    auto* named_via = known_vias->find_via(via->get_name());
    auto* named_master = named_via == nullptr ? nullptr : named_via->get_instance();
    if (named_master != nullptr && via_master_signature(named_master) == signature) {
      return named_master;
    }
  }

  for (auto* candidate : known_vias->get_via_list()) {
    auto* candidate_master = candidate == nullptr ? nullptr : candidate->get_instance();
    if (candidate_master != nullptr && via_master_signature(candidate_master) == signature) {
      return candidate_master;
    }
  }
  return nullptr;
}

IdbVia* read_via(BinaryReader& reader, IdbLayers* layers, IdbViaRuleList* rules, IdbVias* known_vias = nullptr);

IdbLayer* find_layer(IdbLayers* layers, const std::string& name)
{
  return layers == nullptr || name.empty() ? nullptr : layers->find_layer(name);
}

IdbLayerRouting* find_routing_layer(IdbLayers* layers, const std::string& name)
{
  return dynamic_cast<IdbLayerRouting*>(find_layer(layers, name));
}

IdbLayerCut* find_cut_layer(IdbLayers* layers, const std::string& name)
{
  return dynamic_cast<IdbLayerCut*>(find_layer(layers, name));
}

void write_coord(BinaryWriter& writer, IdbCoordinate<int32_t>* coord)
{
  writer.write_bool(coord != nullptr);
  if (coord == nullptr) {
    return;
  }
  writer.write(coord->get_x());
  writer.write(coord->get_y());
}

IdbCoordinate<int32_t>* read_coord(BinaryReader& reader)
{
  if (!reader.read_bool()) {
    return nullptr;
  }
  int32_t x = 0;
  int32_t y = 0;
  reader.read(x);
  reader.read(y);
  return new IdbCoordinate<int32_t>(x, y);
}

void write_coord_value(BinaryWriter& writer, IdbCoordinate<int32_t> coord)
{
  writer.write(coord.get_x());
  writer.write(coord.get_y());
}

IdbCoordinate<int32_t> read_coord_value(BinaryReader& reader)
{
  int32_t x = 0;
  int32_t y = 0;
  reader.read(x);
  reader.read(y);
  return IdbCoordinate<int32_t>(x, y);
}

void write_rect(BinaryWriter& writer, IdbRect* rect)
{
  writer.write_bool(rect != nullptr);
  if (rect == nullptr) {
    return;
  }
  writer.write(rect->get_low_x());
  writer.write(rect->get_low_y());
  writer.write(rect->get_high_x());
  writer.write(rect->get_high_y());
}

IdbRect* read_rect(BinaryReader& reader)
{
  if (!reader.read_bool()) {
    return nullptr;
  }
  int32_t lx = 0;
  int32_t ly = 0;
  int32_t hx = 0;
  int32_t hy = 0;
  reader.read(lx);
  reader.read(ly);
  reader.read(hx);
  reader.read(hy);
  return new IdbRect(lx, ly, hx, hy);
}

void write_cut_enclosure(BinaryWriter& writer, IdbLayerCutEnclosure* enclosure);
IdbLayerCutEnclosure* read_cut_enclosure(BinaryReader& reader);

void write_units(BinaryWriter& writer, IdbUnits* units)
{
  writer.write_bool(units != nullptr);
  if (units == nullptr) {
    return;
  }
  writer.write(units->get_nanoseconds());
  writer.write(units->get_picofarads());
  writer.write(units->get_ohms());
  writer.write(units->get_milliwatts());
  writer.write(units->get_milliamps());
  writer.write(units->get_volts());
  writer.write(units->get_micron_dbu());
  writer.write(units->get_megahertz());
}

IdbUnits* read_units(BinaryReader& reader)
{
  if (!reader.read_bool()) {
    return nullptr;
  }
  auto* units = new IdbUnits();
  int32_t value = 0;
  reader.read(value);
  units->set_nanoseconds(value);
  reader.read(value);
  units->set_picofarads(value);
  reader.read(value);
  units->set_ohms(value);
  reader.read(value);
  units->set_milliwatts(value);
  reader.read(value);
  units->set_milliamps(value);
  reader.read(value);
  units->set_volts(value);
  reader.read(value);
  units->set_microns_dbu(value);
  reader.read(value);
  units->set_megahertz(value);
  return units;
}

void write_layer_shape(BinaryWriter& writer, IdbLayerShape* shape)
{
  writer.write_bool(shape != nullptr);
  if (shape == nullptr) {
    return;
  }
  writer.write_enum(shape->get_type());
  writer.write_string(layer_name(shape->get_layer()));
  auto& rects = shape->get_rect_list();
  writer.write(static_cast<uint64_t>(rects.size()));
  for (auto* rect : rects) {
    write_rect(writer, rect);
  }
}

IdbLayerShape* read_layer_shape(BinaryReader& reader, IdbLayers* layers)
{
  if (!reader.read_bool()) {
    return nullptr;
  }
  auto type = reader.read_enum<IdbLayerShapeType>();
  auto* shape = new IdbLayerShape(type);
  shape->set_layer(find_layer(layers, reader.read_string()));
  uint64_t rect_count = 0;
  reader.read(rect_count);
  for (uint64_t i = 0; i < rect_count; ++i) {
    if (auto* rect = read_rect(reader)) {
      shape->add_rect(rect);
    }
  }
  return shape;
}

void write_port(BinaryWriter& writer, IdbPort* port)
{
  writer.write_bool(port != nullptr);
  if (port == nullptr) {
    return;
  }
  writer.write_enum(port->get_port_class());
  writer.write_enum(port->get_orient());
  write_coord(writer, port->get_coordinate());
  write_coord(writer, port->get_io_average_coordinate());
  write_rect(writer, port->get_io_bounding_box());
  writer.write_enum(port->get_placement_status());

  auto& shapes = port->get_layer_shape();
  writer.write(static_cast<uint64_t>(shapes.size()));
  for (auto* shape : shapes) {
    write_layer_shape(writer, shape);
  }

  auto& vias = port->get_via_list();
  writer.write(static_cast<uint64_t>(vias.size()));
  for (auto* via : vias) {
    writer.write_string(via_name(via));
    write_coord(writer, via == nullptr ? nullptr : via->get_coordinate());
  }
}

IdbPort* read_port(BinaryReader& reader, IdbLayers* layers, IdbVias* vias)
{
  if (!reader.read_bool()) {
    return nullptr;
  }
  auto* port = new IdbPort();
  port->set_port_class(reader.read_enum<IdbPortClass>());
  port->set_orient(reader.read_enum<IdbOrient>());
  if (auto* coord = read_coord(reader)) {
    port->set_coordinate(coord->get_x(), coord->get_y());
    delete coord;
  }
  if (auto* coord = read_coord(reader)) {
    port->set_io_average_coordinate(coord->get_x(), coord->get_y());
    delete coord;
  }
  if (auto* rect = read_rect(reader)) {
    port->get_io_bounding_box()->set_rect(rect);
    delete rect;
  }
  port->set_placement_status(reader.read_enum<IdbPlacementStatus>());

  uint64_t shape_count = 0;
  reader.read(shape_count);
  for (uint64_t i = 0; i < shape_count; ++i) {
    if (auto* shape = read_layer_shape(reader, layers)) {
      port->add_layer_shape(shape);
    }
  }

  uint64_t via_count = 0;
  reader.read(via_count);
  for (uint64_t i = 0; i < via_count; ++i) {
    const auto name = reader.read_string();
    std::unique_ptr<IdbCoordinate<int32_t>> coord(read_coord(reader));
    IdbVia* src = vias == nullptr ? nullptr : vias->find_via(name);
    if (src != nullptr) {
      auto* via = src->clone();
      if (coord != nullptr) {
        via->set_coordinate(coord->get_x(), coord->get_y());
      }
      port->add_via(via);
    }
  }

  return port;
}

void write_term(BinaryWriter& writer, IdbTerm* term)
{
  writer.write_bool(term != nullptr);
  if (term == nullptr) {
    return;
  }
  writer.write_string(term->get_name());
  writer.write_enum(term->get_direction());
  writer.write_enum(term->get_type());
  writer.write_enum(term->get_shape());
  writer.write_enum(term->get_placement_status());
  write_coord_value(writer, term->get_average_position());
  write_rect(writer, term->get_bounding_box());
  writer.write_bool(term->is_port_exist());
  writer.write_bool(term->is_special_net());
  writer.write_bool(term->is_instance_pin());

  auto& ports = term->get_port_list();
  writer.write(static_cast<uint64_t>(ports.size()));
  for (auto* port : ports) {
    write_port(writer, port);
  }
}

IdbTerm* read_term(BinaryReader& reader, IdbCellMaster* master, IdbLayers* layers, IdbVias* vias)
{
  if (!reader.read_bool()) {
    return nullptr;
  }
  auto* term = new IdbTerm();
  term->set_name(reader.read_string());
  term->set_direction(reader.read_enum<IdbConnectDirection>());
  term->set_type(reader.read_enum<IdbConnectType>());
  term->set_shape(reader.read_enum<IdbTermShape>());
  term->set_placement_status(reader.read_enum<IdbPlacementStatus>());
  auto average = read_coord_value(reader);
  term->set_average_position(average.get_x(), average.get_y());
  if (auto* rect = read_rect(reader)) {
    term->set_bounding_box(rect->get_low_x(), rect->get_low_y(), rect->get_high_x(), rect->get_high_y());
    delete rect;
  }
  term->set_has_port(reader.read_bool());
  term->set_special(reader.read_bool());
  if (reader.read_bool()) {
    term->set_as_instance_pin();
  }
  term->set_cell_master(master);

  uint64_t port_count = 0;
  reader.read(port_count);
  for (uint64_t i = 0; i < port_count; ++i) {
    if (auto* port = read_port(reader, layers, vias)) {
      term->add_port(port);
    }
  }
  return term;
}

void write_obs(BinaryWriter& writer, IdbObs* obs)
{
  writer.write_bool(obs != nullptr);
  if (obs == nullptr) {
    return;
  }
  auto& layers = obs->get_obs_layer_list();
  writer.write(static_cast<uint64_t>(layers.size()));
  for (auto* obs_layer : layers) {
    write_layer_shape(writer, obs_layer == nullptr ? nullptr : obs_layer->get_shape());
  }
}

IdbObs* read_obs(BinaryReader& reader, IdbLayers* layers)
{
  if (!reader.read_bool()) {
    return nullptr;
  }
  auto* obs = new IdbObs();
  uint64_t layer_count = 0;
  reader.read(layer_count);
  for (uint64_t i = 0; i < layer_count; ++i) {
    auto* shape = read_layer_shape(reader, layers);
    if (shape != nullptr) {
      auto* obs_layer = new IdbObsLayer();
      obs_layer->set_shape(shape);
      obs->add_obs_layer(obs_layer);
    }
  }
  return obs;
}

void write_via_rule_generate(BinaryWriter& writer, IdbViaRuleGenerate* rule)
{
  writer.write_bool(rule != nullptr);
  if (rule == nullptr) {
    return;
  }
  writer.write_string(rule->get_name());
  writer.write_string(layer_name(rule->get_layer_bottom()));
  write_cut_enclosure(writer, rule->get_enclosure_bottom());
  writer.write_string(layer_name(rule->get_layer_cut()));
  write_rect(writer, rule->get_cut_rect());
  writer.write(rule->get_spacing_x());
  writer.write(rule->get_spacing_y());
  writer.write_string(layer_name(rule->get_layer_top()));
  write_cut_enclosure(writer, rule->get_enclosure_top());
}

IdbViaRuleGenerate* read_via_rule_generate(BinaryReader& reader, IdbLayers* layers)
{
  if (!reader.read_bool()) {
    return nullptr;
  }
  auto* rule = new IdbViaRuleGenerate();
  rule->set_name(reader.read_string());
  rule->set_layer_bottom(find_routing_layer(layers, reader.read_string()));
  rule->set_enclosure_bottom(read_cut_enclosure(reader));
  rule->set_layer_cut(find_cut_layer(layers, reader.read_string()));
  rule->set_cut_rect(read_rect(reader));
  int32_t spacing_x = 0;
  int32_t spacing_y = 0;
  reader.read(spacing_x);
  reader.read(spacing_y);
  rule->set_spacing(spacing_x, spacing_y);
  rule->set_layer_top(find_routing_layer(layers, reader.read_string()));
  rule->set_enclosure_top(read_cut_enclosure(reader));
  return rule;
}

void write_via_master_generate(BinaryWriter& writer, IdbViaMasterGenerate* generate)
{
  writer.write_bool(generate != nullptr);
  if (generate == nullptr) {
    return;
  }
  writer.write_string(generate->get_rule_name());
  writer.write_string(generate->get_rule_generate() == nullptr ? std::string() : generate->get_rule_generate()->get_name());
  writer.write(generate->get_cut_size_x());
  writer.write(generate->get_cut_size_y());
  writer.write_string(layer_name(generate->get_layer_bottom()));
  writer.write_string(layer_name(generate->get_layer_cut()));
  writer.write_string(layer_name(generate->get_layer_top()));
  writer.write(generate->get_cut_spcing_x());
  writer.write(generate->get_cut_spcing_y());
  writer.write(generate->get_enclosure_bottom_x());
  writer.write(generate->get_enclosure_bottom_y());
  writer.write(generate->get_enclosure_top_x());
  writer.write(generate->get_enclosure_top_y());
  writer.write(generate->get_cut_rows());
  writer.write(generate->get_cut_cols());
  writer.write(generate->get_original_offset_x());
  writer.write(generate->get_original_offset_y());
  writer.write(generate->get_offset_bottom_x());
  writer.write(generate->get_offset_bottom_y());
  writer.write(generate->get_offset_top_x());
  writer.write(generate->get_offset_top_y());
  auto& rects = generate->get_cut_rect_list();
  writer.write(static_cast<uint64_t>(rects.size()));
  for (auto* rect : rects) {
    write_rect(writer, rect);
  }
  write_rect(writer, generate->get_cut_bouding_rect());
  writer.write_bool(generate->get_patttern() != nullptr);
  if (generate->get_patttern() != nullptr) {
    writer.write_string(generate->get_patttern()->get_pattern_string());
  }
}

IdbViaMasterGenerate* read_via_master_generate(BinaryReader& reader, IdbLayers* layers, IdbViaRuleList* rules)
{
  if (!reader.read_bool()) {
    return nullptr;
  }
  auto* generate = new IdbViaMasterGenerate();
  generate->set_rule_name(reader.read_string());
  const auto rule_generate_name = reader.read_string();
  generate->set_rule_generate(rules == nullptr || rule_generate_name.empty() ? nullptr : rules->find_via_rule_generate(rule_generate_name));

  int32_t x = 0;
  int32_t y = 0;
  reader.read(x);
  reader.read(y);
  generate->set_cut_size(x, y);
  generate->set_layer_bottom(find_routing_layer(layers, reader.read_string()));
  generate->set_layer_cut(find_cut_layer(layers, reader.read_string()));
  generate->set_layer_top(find_routing_layer(layers, reader.read_string()));
  reader.read(x);
  reader.read(y);
  generate->set_cut_spacing(x, y);
  int32_t bx = 0;
  int32_t by = 0;
  int32_t tx = 0;
  int32_t ty = 0;
  reader.read(bx);
  reader.read(by);
  reader.read(tx);
  reader.read(ty);
  generate->set_enclosure_bottom(bx, by);
  generate->set_enclosure_top(tx, ty);
  int32_t rows = 0;
  int32_t cols = 0;
  reader.read(rows);
  reader.read(cols);
  generate->set_cut_row_col(rows, cols);
  reader.read(x);
  reader.read(y);
  generate->set_original(x, y);
  reader.read(bx);
  reader.read(by);
  reader.read(tx);
  reader.read(ty);
  generate->set_offset_bottom(bx, by);
  generate->set_offset_top(tx, ty);

  uint64_t rect_count = 0;
  reader.read(rect_count);
  for (uint64_t i = 0; i < rect_count; ++i) {
    std::unique_ptr<IdbRect> rect(read_rect(reader));
    if (rect != nullptr) {
      generate->add_cut_rect(rect->get_low_x(), rect->get_low_y(), rect->get_high_x(), rect->get_high_y());
    }
  }
  if (std::unique_ptr<IdbRect> rect(read_rect(reader)); rect != nullptr) {
    generate->set_cut_bouding_rect(rect->get_low_x(), rect->get_low_y(), rect->get_high_x(), rect->get_high_y());
  }
  if (reader.read_bool()) {
    generate->set_patttern(reader.read_string());
  }
  return generate;
}

void write_via_master(BinaryWriter& writer, IdbViaMaster* master)
{
  writer.write_bool(master != nullptr);
  if (master == nullptr) {
    return;
  }
  writer.write_string(master->get_name());
  writer.write_bool(master->is_default());
  writer.write_enum(master->get_type());
  writer.write(master->get_cut_rows());
  writer.write(master->get_cut_cols());
  write_rect(writer, master->get_cut_rect());
  write_via_master_generate(writer, master->get_master_generate());

  auto& fixed_list = master->get_master_fixed_list();
  writer.write(static_cast<uint64_t>(fixed_list.size()));
  for (auto* fixed : fixed_list) {
    writer.write_string(layer_name(fixed == nullptr ? nullptr : fixed->get_layer()));
    auto& rects = fixed->get_rect_list();
    writer.write(static_cast<uint64_t>(rects.size()));
    for (auto* rect : rects) {
      write_rect(writer, rect);
    }
  }
}

IdbViaMaster* read_via_master(BinaryReader& reader, IdbLayers* layers, IdbViaRuleList* rules)
{
  if (!reader.read_bool()) {
    return nullptr;
  }
  auto* master = new IdbViaMaster();
  master->set_name(reader.read_string());
  master->set_default(reader.read_bool());
  master->set_type(reader.read_enum<IdbViaMaster::IdbViaMasterType>());
  int32_t rows = 0;
  int32_t cols = 0;
  reader.read(rows);
  reader.read(cols);
  master->set_cut_row_col(rows, cols);
  if (std::unique_ptr<IdbRect> rect(read_rect(reader)); rect != nullptr) {
    master->set_cut_rect(rect->get_low_x(), rect->get_low_y(), rect->get_high_x(), rect->get_high_y());
  }
  master->set_master_generate(read_via_master_generate(reader, layers, rules));

  uint64_t fixed_count = 0;
  reader.read(fixed_count);
  for (uint64_t i = 0; i < fixed_count; ++i) {
    const auto name = reader.read_string();
    auto* fixed = master->add_fixed(name);
    fixed->set_layer(find_layer(layers, name));
    uint64_t rect_count = 0;
    reader.read(rect_count);
    for (uint64_t j = 0; j < rect_count; ++j) {
      std::unique_ptr<IdbRect> rect(read_rect(reader));
      if (rect != nullptr) {
        fixed->add_rect(rect->get_low_x(), rect->get_low_y(), rect->get_high_x(), rect->get_high_y());
      }
    }
  }
  master->set_via_shape();
  return master;
}

void write_via(BinaryWriter& writer, IdbVia* via)
{
  writer.write_bool(via != nullptr);
  if (via == nullptr) {
    return;
  }
  writer.write_string(via->get_name());
  write_coord(writer, via->get_coordinate());
  write_via_master(writer, via->get_instance());
}

IdbVia* read_via(BinaryReader& reader, IdbLayers* layers, IdbViaRuleList* rules, IdbVias* known_vias)
{
  if (!reader.read_bool()) {
    return nullptr;
  }
  auto* via = new IdbVia();
  via->set_name(reader.read_string());
  if (auto* coord = read_coord(reader)) {
    via->set_coordinate(coord);
  }
  via->set_instance(read_via_master(reader, layers, rules));
  if (auto* matched_master = find_matching_via_master(known_vias, via)) {
    via->set_instance_reference(matched_master);
  }
  return via;
}

void write_regular_wire_segment(BinaryWriter& writer, IdbRegularWireSegment* segment)
{
  writer.write_bool(segment != nullptr);
  if (segment == nullptr) {
    return;
  }
  writer.write_bool(segment->is_new_layer());
  writer.write_bool(segment->is_via());
  writer.write_bool(segment->is_rect());
  writer.write_string(segment->get_layer_name().empty() ? layer_name(segment->get_layer()) : segment->get_layer_name());
  write_rect(writer, segment->get_delta_rect());
  auto& points = segment->get_point_list();
  writer.write(static_cast<uint64_t>(points.size()));
  for (auto* point : points) {
    write_coord(writer, point);
  }
  auto vias = segment->get_via_list();
  writer.write(static_cast<uint64_t>(vias.size()));
  for (auto* via : vias) {
    write_via(writer, via);
  }
}

IdbRegularWireSegment* read_regular_wire_segment(BinaryReader& reader, IdbLayers* layers, IdbViaRuleList* rules, IdbVias* known_vias = nullptr)
{
  if (!reader.read_bool()) {
    return nullptr;
  }
  auto* segment = new IdbRegularWireSegment();
  segment->set_layer_status(reader.read_bool());
  segment->set_is_via(reader.read_bool());
  segment->set_is_rect(reader.read_bool());
  const auto name = reader.read_string();
  segment->set_layer_name(name);
  segment->set_layer(find_layer(layers, name));
  if (std::unique_ptr<IdbRect> rect(read_rect(reader)); rect != nullptr) {
    segment->set_delta_rect(rect->get_low_x(), rect->get_low_y(), rect->get_high_x(), rect->get_high_y());
  }
  uint64_t point_count = 0;
  reader.read(point_count);
  for (uint64_t i = 0; i < point_count; ++i) {
    std::unique_ptr<IdbCoordinate<int32_t>> point(read_coord(reader));
    if (point != nullptr) {
      segment->add_point(point->get_x(), point->get_y());
    }
  }
  uint64_t via_count = 0;
  reader.read(via_count);
  for (uint64_t i = 0; i < via_count; ++i) {
    if (auto* via = read_via(reader, layers, rules, known_vias)) {
      segment->set_via(via);
    }
  }
  return segment;
}

void write_regular_wire(BinaryWriter& writer, IdbRegularWire* wire)
{
  writer.write_bool(wire != nullptr);
  if (wire == nullptr) {
    return;
  }
  writer.write_enum(wire->get_wire_statement());
  writer.write_string(wire->get_shiled_name());
  auto& segments = wire->get_segment_list();
  writer.write(static_cast<uint64_t>(segments.size()));
  for (auto* segment : segments) {
    write_regular_wire_segment(writer, segment);
  }
}

IdbRegularWire* read_regular_wire(BinaryReader& reader, IdbLayers* layers, IdbViaRuleList* rules, IdbVias* known_vias = nullptr)
{
  if (!reader.read_bool()) {
    return nullptr;
  }
  auto* wire = new IdbRegularWire();
  wire->set_wire_state(reader.read_enum<IdbWiringStatement>());
  wire->set_shield_name(reader.read_string());
  uint64_t segment_count = 0;
  reader.read(segment_count);
  for (uint64_t i = 0; i < segment_count; ++i) {
    if (auto* segment = read_regular_wire_segment(reader, layers, rules, known_vias)) {
      wire->add_segment(segment);
    }
  }
  return wire;
}

void write_special_wire_segment(BinaryWriter& writer, IdbSpecialWireSegment* segment)
{
  writer.write_bool(segment != nullptr);
  if (segment == nullptr) {
    return;
  }
  writer.write_bool(segment->is_new_layer());
  writer.write_bool(segment->is_via());
  writer.write_bool(segment->is_rect());
  writer.write_string(layer_name(segment->get_layer()));
  writer.write(segment->get_route_width());
  writer.write_enum(segment->get_shape_type());
  writer.write(segment->get_style());
  write_rect(writer, segment->get_delta_rect());
  write_via(writer, segment->get_via());
  auto& points = segment->get_point_list();
  writer.write(static_cast<uint64_t>(points.size()));
  for (auto* point : points) {
    write_coord(writer, point);
  }
}

IdbSpecialWireSegment* read_special_wire_segment(BinaryReader& reader, IdbLayers* layers, IdbViaRuleList* rules, IdbVias* known_vias = nullptr)
{
  if (!reader.read_bool()) {
    return nullptr;
  }
  auto* segment = new IdbSpecialWireSegment();
  segment->set_layer_status(reader.read_bool());
  segment->set_is_via(reader.read_bool());
  segment->set_is_rect(reader.read_bool());
  segment->set_layer(find_layer(layers, reader.read_string()));
  int32_t value = 0;
  reader.read(value);
  segment->set_route_width(value);
  segment->set_shape_type(reader.read_enum<IdbWireShapeType>());
  reader.read(value);
  segment->set_style(value);
  if (std::unique_ptr<IdbRect> rect(read_rect(reader)); rect != nullptr) {
    segment->set_delta_rect(rect->get_low_x(), rect->get_low_y(), rect->get_high_x(), rect->get_high_y());
  }
  segment->set_via(read_via(reader, layers, rules, known_vias));
  uint64_t point_count = 0;
  reader.read(point_count);
  for (uint64_t i = 0; i < point_count; ++i) {
    std::unique_ptr<IdbCoordinate<int32_t>> point(read_coord(reader));
    if (point != nullptr) {
      segment->add_point(point->get_x(), point->get_y());
    }
  }
  segment->set_bounding_box();
  return segment;
}

void write_special_wire(BinaryWriter& writer, IdbSpecialWire* wire)
{
  writer.write_bool(wire != nullptr);
  if (wire == nullptr) {
    return;
  }
  writer.write_enum(wire->get_wire_state());
  writer.write_string(wire->get_shiled_name());
  auto& segments = wire->get_segment_list();
  writer.write(static_cast<uint64_t>(segments.size()));
  for (auto* segment : segments) {
    write_special_wire_segment(writer, segment);
  }
}

IdbSpecialWire* read_special_wire(BinaryReader& reader, IdbLayers* layers, IdbViaRuleList* rules, IdbVias* known_vias = nullptr)
{
  if (!reader.read_bool()) {
    return nullptr;
  }
  auto* wire = new IdbSpecialWire();
  wire->set_wire_state(reader.read_enum<IdbWiringStatement>());
  wire->set_shield_name(reader.read_string());
  uint64_t segment_count = 0;
  reader.read(segment_count);
  for (uint64_t i = 0; i < segment_count; ++i) {
    if (auto* segment = read_special_wire_segment(reader, layers, rules, known_vias)) {
      wire->add_segment(segment);
    }
  }
  return wire;
}

void write_layout_metadata(const std::string& folder, IdbLayout* layout)
{
  BinaryWriter writer(section_path(folder, kLayoutDir, "metadata"), ArchiveSection::kLayoutMetadata);
  writer.write(layout == nullptr ? int32_t{-1} : layout->get_munufacture_grid());
}

void read_layout_metadata(const std::string& folder, IdbLayout* layout)
{
  BinaryReader reader(section_path(folder, kLayoutDir, "metadata"), ArchiveSection::kLayoutMetadata);
  int32_t grid = -1;
  reader.read(grid);
  layout->set_manufacture_grid(grid);
}

void write_layout_units(const std::string& folder, IdbLayout* layout)
{
  BinaryWriter writer(section_path(folder, kLayoutDir, "units"), ArchiveSection::kLayoutUnits);
  write_units(writer, layout == nullptr ? nullptr : layout->get_units());
}

void read_layout_units(const std::string& folder, IdbLayout* layout)
{
  BinaryReader reader(section_path(folder, kLayoutDir, "units"), ArchiveSection::kLayoutUnits);
  layout->set_units(read_units(reader));
}

void write_layout_die(const std::string& folder, IdbLayout* layout)
{
  BinaryWriter writer(section_path(folder, kLayoutDir, "die"), ArchiveSection::kLayoutDie);
  auto* die = layout == nullptr ? nullptr : layout->get_die();
  writer.write_bool(die != nullptr);
  if (die == nullptr) {
    return;
  }
  auto& points = die->get_points();
  writer.write(static_cast<uint64_t>(points.size()));
  for (auto* point : points) {
    write_coord(writer, point);
  }
  write_rect(writer, die->get_bounding_box());
}

void read_layout_die(const std::string& folder, IdbLayout* layout)
{
  BinaryReader reader(section_path(folder, kLayoutDir, "die"), ArchiveSection::kLayoutDie);
  if (!reader.read_bool()) {
    return;
  }
  auto* die = layout->get_die();
  die->reset();
  uint64_t point_count = 0;
  reader.read(point_count);
  for (uint64_t i = 0; i < point_count; ++i) {
    if (auto* point = read_coord(reader)) {
      die->add_point(point);
    }
  }
  std::unique_ptr<IdbRect> rect(read_rect(reader));
  if (rect != nullptr) {
    die->IdbObject::set_bounding_box(rect->get_low_x(), rect->get_low_y(), rect->get_high_x(), rect->get_high_y());
  } else {
    die->set_bounding_box();
  }
}

void write_layer_spacing(BinaryWriter& writer, IdbLayerSpacing* spacing)
{
  writer.write_bool(spacing != nullptr);
  if (spacing == nullptr) {
    return;
  }
  writer.write_enum(spacing->get_spacing_type());
  writer.write(spacing->get_min_spacing());
  writer.write(spacing->get_min_width());
  writer.write(spacing->get_max_width());
}

IdbLayerSpacing* read_layer_spacing(BinaryReader& reader)
{
  if (!reader.read_bool()) {
    return nullptr;
  }
  auto* spacing = new IdbLayerSpacing();
  spacing->set_spacing_type(reader.read_enum<IdbLayerSpacingType>());
  int32_t value = 0;
  reader.read(value);
  spacing->set_min_spacing(value);
  reader.read(value);
  spacing->set_min_width(value);
  reader.read(value);
  spacing->set_max_width(value);
  return spacing;
}

void write_cut_enclosure(BinaryWriter& writer, IdbLayerCutEnclosure* enclosure)
{
  writer.write_bool(enclosure != nullptr);
  if (enclosure == nullptr) {
    return;
  }
  writer.write(enclosure->get_overhang_1());
  writer.write(enclosure->get_overhang_2());
  auto rect = enclosure->get_rect();
  writer.write(rect.get_low_x());
  writer.write(rect.get_low_y());
  writer.write(rect.get_high_x());
  writer.write(rect.get_high_y());
}

IdbLayerCutEnclosure* read_cut_enclosure(BinaryReader& reader)
{
  if (!reader.read_bool()) {
    return nullptr;
  }
  auto* enclosure = new IdbLayerCutEnclosure();
  int32_t value = 0;
  reader.read(value);
  enclosure->set_overhang_1(value);
  reader.read(value);
  enclosure->set_overhang_2(value);
  int32_t lx = 0;
  int32_t ly = 0;
  int32_t hx = 0;
  int32_t hy = 0;
  reader.read(lx);
  reader.read(ly);
  reader.read(hx);
  reader.read(hy);
  enclosure->set_rect(IdbRect(lx, ly, hx, hy));
  return enclosure;
}

void write_layout_layers(const std::string& folder, IdbLayout* layout)
{
  BinaryWriter writer(section_path(folder, kLayoutDir, "layers"), ArchiveSection::kLayoutLayers);
  auto* layers = layout == nullptr ? nullptr : layout->get_layers();
  auto& list = layers->get_layers();
  writer.write(static_cast<uint64_t>(list.size()));
  for (auto* layer : list) {
    writer.write_enum(layer->get_type());
    writer.write_string(layer->get_name());
    writer.write(layer->get_id());
    writer.write(layer->get_order());

    if (layer->get_type() == IdbLayerType::kLayerRouting) {
      auto* routing = dynamic_cast<IdbLayerRouting*>(layer);
      writer.write(routing->get_width());
      writer.write(routing->get_min_width());
      writer.write(routing->get_max_width());
      writer.write_enum(routing->get_pitch().type);
      writer.write(routing->get_pitch().orient_x);
      writer.write(routing->get_pitch().orient_y);
      writer.write_enum(routing->get_offset().type);
      writer.write(routing->get_offset().orient_x);
      writer.write(routing->get_offset().orient_y);
      writer.write_enum(routing->get_direction());
      writer.write(routing->get_wire_extension());
      writer.write(routing->get_thickness());
      writer.write(routing->get_height());
      writer.write(routing->get_area());
      writer.write(routing->get_resistance());
      writer.write(routing->get_capacitance());
      writer.write(routing->get_edge_capacitance());
      writer.write(routing->get_min_density());
      writer.write(routing->get_max_density());
      writer.write(routing->get_density_check_length());
      writer.write(routing->get_density_check_width());
      writer.write(routing->get_density_check_step());
      writer.write(routing->get_min_cut_num());
      writer.write(routing->get_min_cut_width());
      writer.write(routing->get_power_segment_width());

      auto* spacing_list = routing->get_spacing_list();
      auto& spacings = spacing_list->get_spacing_list();
      writer.write(static_cast<uint64_t>(spacings.size()));
      for (auto* spacing : spacings) {
        write_layer_spacing(writer, spacing);
      }

      auto* area_list = routing->get_min_enclose_area_list();
      auto& areas = area_list->get_min_area_list();
      writer.write(static_cast<uint64_t>(areas.size()));
      for (auto area : areas) {
        writer.write(area._area);
        writer.write(area._width);
      }

      auto& notch = routing->get_spacing_notchlength();
      writer.write(notch.get_notch_length());
      writer.write(notch.get_min_spacing());
    } else if (layer->get_type() == IdbLayerType::kLayerCut) {
      auto* cut = dynamic_cast<IdbLayerCut*>(layer);
      writer.write(cut->get_width());
      auto spacings = cut->get_spacings();
      writer.write(static_cast<uint64_t>(spacings.size()));
      for (auto* spacing : spacings) {
        writer.write_bool(spacing != nullptr);
        if (spacing == nullptr) {
          continue;
        }
        writer.write(spacing->get_spacing());
        auto adjacent = spacing->get_adjacent_cuts();
        writer.write_bool(adjacent.has_value());
        if (adjacent.has_value()) {
          writer.write(adjacent->get_adjacent_cuts());
          writer.write(adjacent->get_cut_within());
        }
      }

      auto* array_spacing = cut->get_array_spacing();
      writer.write_bool(array_spacing != nullptr);
      if (array_spacing != nullptr) {
        writer.write_bool(array_spacing->is_long_array());
        writer.write(array_spacing->get_cut_spacing());
        auto& array_cuts = array_spacing->get_array_cut_list();
        writer.write(static_cast<uint64_t>(array_cuts.size()));
        for (auto cut_item : array_cuts) {
          writer.write(cut_item._array_cut);
          writer.write(cut_item._array_spacing);
        }
      }
      write_cut_enclosure(writer, cut->get_enclosure_below());
      write_cut_enclosure(writer, cut->get_enclosure_above());
    } else if (layer->get_type() == IdbLayerType::kLayerMasterslice) {
      auto* masterslice = dynamic_cast<IdbLayerMasterslice*>(layer);
      writer.write_string(masterslice == nullptr ? std::string() : masterslice->get_lef58_type());
    } else if (layer->get_type() == IdbLayerType::kLayerImplant) {
      auto* implant = dynamic_cast<IdbLayerImplant*>(layer);
      writer.write(implant == nullptr ? int32_t{0} : implant->get_min_width());
    }
  }
}

void read_layout_layers(const std::string& folder, IdbLayout* layout)
{
  BinaryReader reader(section_path(folder, kLayoutDir, "layers"), ArchiveSection::kLayoutLayers);
  auto* layers = layout->get_layers();
  layers->reset_layers();
  layers->get_routing_layers().clear();
  layers->get_cut_layers().clear();
  uint64_t layer_count = 0;
  reader.read(layer_count);
  for (uint64_t i = 0; i < layer_count; ++i) {
    const auto type = reader.read_enum<IdbLayerType>();
    const auto name = reader.read_string();
    int8_t id = 0;
    uint8_t order = 0;
    reader.read(id);
    reader.read(order);

    IdbLayer* layer = nullptr;
    if (type == IdbLayerType::kLayerRouting) {
      auto* routing = new IdbLayerRouting();
      int32_t value = 0;
      reader.read(value);
      routing->set_width(value);
      reader.read(value);
      routing->set_min_width(value);
      reader.read(value);
      routing->set_max_width(value);
      IdbLayerOrientValue orient_value{};
      orient_value.type = reader.read_enum<IdbLayerOrientType>();
      reader.read(orient_value.orient_x);
      reader.read(orient_value.orient_y);
      routing->set_pitch(orient_value);
      orient_value.type = reader.read_enum<IdbLayerOrientType>();
      reader.read(orient_value.orient_x);
      reader.read(orient_value.orient_y);
      routing->set_offset(orient_value);
      routing->set_direction(reader.read_enum<IdbLayerDirection>());
      reader.read(value);
      routing->set_wire_extension(value);
      reader.read(value);
      routing->set_thickness(value);
      reader.read(value);
      routing->set_height(value);
      reader.read(value);
      routing->set_area(value);
      double double_value = 0.0;
      reader.read(double_value);
      routing->set_resistance(double_value);
      reader.read(double_value);
      routing->set_capacitance(double_value);
      reader.read(double_value);
      routing->set_edge_capacitance(double_value);
      reader.read(double_value);
      routing->set_min_density(double_value);
      reader.read(double_value);
      routing->set_max_density(double_value);
      reader.read(value);
      routing->set_density_check_length(value);
      reader.read(value);
      routing->set_density_check_width(value);
      reader.read(value);
      routing->set_density_check_step(value);
      reader.read(value);
      routing->set_min_cut_num(value);
      reader.read(value);
      routing->set_min_cut_width(value);
      reader.read(value);
      routing->set_power_segment_width(value);

      auto* spacing_list = new IdbLayerSpacingList();
      uint64_t spacing_count = 0;
      reader.read(spacing_count);
      for (uint64_t j = 0; j < spacing_count; ++j) {
        spacing_list->add_spacing(read_layer_spacing(reader));
      }
      routing->set_spacing_list(spacing_list);

      auto* area_list = new IdbMinEncloseAreaList();
      uint64_t area_count = 0;
      reader.read(area_count);
      for (uint64_t j = 0; j < area_count; ++j) {
        int32_t area = 0;
        int32_t width = 0;
        reader.read(area);
        reader.read(width);
        area_list->add_min_area(area, width);
      }
      routing->set_min_enclose_area_list(area_list);

      int32_t notch_length = 0;
      int32_t notch_spacing = 0;
      reader.read(notch_length);
      reader.read(notch_spacing);
      routing->get_spacing_notchlength().set_notch_length(notch_length);
      routing->get_spacing_notchlength().set_min_spacing(notch_spacing);
      layers->add_routing_layer(routing);
      layer = routing;
    } else if (type == IdbLayerType::kLayerCut) {
      auto* cut = new IdbLayerCut();
      int32_t width = 0;
      reader.read(width);
      cut->set_width(width);
      uint64_t spacing_count = 0;
      reader.read(spacing_count);
      for (uint64_t j = 0; j < spacing_count; ++j) {
        if (!reader.read_bool()) {
          continue;
        }
        int32_t spacing_value = 0;
        reader.read(spacing_value);
        auto* spacing = new IdbLayerCutSpacing(spacing_value);
        if (reader.read_bool()) {
          int32_t adjacent_cuts = 0;
          int32_t cut_within = 0;
          reader.read(adjacent_cuts);
          reader.read(cut_within);
          spacing->set_adjacent_cuts(IdbLayerCutSpacing::AdjacentCuts(adjacent_cuts, cut_within));
        }
        cut->add_spacing(spacing);
      }
      if (reader.read_bool()) {
        auto* array_spacing = new IdbLayerCutArraySpacing();
        array_spacing->set_long_array(reader.read_bool());
        int32_t cut_spacing = 0;
        reader.read(cut_spacing);
        array_spacing->set_cut_spacing(cut_spacing);
        uint64_t array_count = 0;
        reader.read(array_count);
        array_spacing->set_array_cut_num(static_cast<int32_t>(array_count));
        for (uint64_t j = 0; j < array_count; ++j) {
          int32_t array_cut = 0;
          int32_t array_spacing_value = 0;
          reader.read(array_cut);
          reader.read(array_spacing_value);
          array_spacing->set_array_value(static_cast<int32_t>(j), array_cut, array_spacing_value);
        }
        cut->set_array_spacing(array_spacing);
      }
      cut->set_enclosure_below(read_cut_enclosure(reader));
      cut->set_enclosure_above(read_cut_enclosure(reader));
      layers->add_cut_layer(cut);
      layer = cut;
    } else if (type == IdbLayerType::kLayerMasterslice) {
      auto* masterslice = new IdbLayerMasterslice();
      auto lef58_type = reader.read_string();
      masterslice->set_lef58_type(std::move(lef58_type));
      layer = masterslice;
    } else if (type == IdbLayerType::kLayerImplant) {
      auto* implant = new IdbLayerImplant();
      int32_t min_width = 0;
      reader.read(min_width);
      implant->set_min_width(min_width);
      layer = implant;
    } else if (type == IdbLayerType::kLayerOverlap) {
      layer = new IdbLayerOverlap();
    } else {
      layer = new IdbLayer();
    }

    layer->set_type(type);
    layer->set_name(name);
    layer->set_id(id);
    layer->set_order(order);
    layers->get_layers().push_back(layer);
  }
}

void write_layout_sites(const std::string& folder, IdbLayout* layout)
{
  BinaryWriter writer(section_path(folder, kLayoutDir, "sites"), ArchiveSection::kLayoutSites);
  auto* sites = layout == nullptr ? nullptr : layout->get_sites();
  auto& list = sites->get_site_list();
  writer.write(static_cast<uint64_t>(list.size()));
  for (auto* site : list) {
    writer.write_string(site->get_name());
    writer.write(site->get_width());
    writer.write(site->get_height());
    writer.write_bool(site->is_overlap());
    writer.write_enum(site->get_site_class());
    writer.write_enum(site->get_symmetry());
    writer.write_enum(site->get_orient());
  }
}

void read_layout_sites(const std::string& folder, IdbLayout* layout)
{
  BinaryReader reader(section_path(folder, kLayoutDir, "sites"), ArchiveSection::kLayoutSites);
  auto* sites = layout->get_sites();
  sites->reset();
  uint64_t site_count = 0;
  reader.read(site_count);
  for (uint64_t i = 0; i < site_count; ++i) {
    auto* site = new IdbSite();
    site->set_name(reader.read_string());
    int32_t value = 0;
    reader.read(value);
    site->set_width(value);
    reader.read(value);
    site->set_height(value);
    site->set_occupied(reader.read_bool());
    site->set_class(reader.read_enum<IdbSiteClass>());
    site->set_symmetry(reader.read_enum<IdbSymmetry>());
    site->set_orient(reader.read_enum<IdbOrient>());
    sites->add_site_list(site);
    if (site->is_core_site()) {
      sites->set_core_site(site);
    } else if (site->is_corner_site()) {
      sites->set_corener_site(site);
    } else if (site->is_pad_site()) {
      sites->set_io_site(site);
    }
  }
}

void write_layout_rows(const std::string& folder, IdbLayout* layout)
{
  BinaryWriter writer(section_path(folder, kLayoutDir, "rows"), ArchiveSection::kLayoutRows);
  auto* rows = layout == nullptr ? nullptr : layout->get_rows();
  auto& list = rows->get_row_list();
  writer.write(static_cast<uint64_t>(list.size()));
  for (auto* row : list) {
    writer.write_string(row->get_name());
    writer.write_string(site_name(row->get_site()));
    write_coord(writer, row->get_original_coordinate());
    writer.write(row->get_row_num_x());
    writer.write(row->get_row_num_y());
    writer.write(row->get_step_x());
    writer.write(row->get_step_y());
    writer.write_enum(row->get_orient());
  }
}

void read_layout_rows(const std::string& folder, IdbLayout* layout)
{
  BinaryReader reader(section_path(folder, kLayoutDir, "rows"), ArchiveSection::kLayoutRows);
  auto* rows = layout->get_rows();
  rows->reset();
  uint64_t row_count = 0;
  reader.read(row_count);
  for (uint64_t i = 0; i < row_count; ++i) {
    const auto row_name = reader.read_string();
    const auto site_name_value = reader.read_string();
    std::unique_ptr<IdbCoordinate<int32_t>> origin(read_coord(reader));
    int32_t row_num_x = 0;
    int32_t row_num_y = 0;
    int32_t step_x = 0;
    int32_t step_y = 0;
    reader.read(row_num_x);
    reader.read(row_num_y);
    reader.read(step_x);
    reader.read(step_y);
    auto orient = reader.read_enum<IdbOrient>();
    auto* site = layout->get_sites()->find_site(site_name_value);
    if (site != nullptr && origin != nullptr) {
      rows->createRow(row_name, site, origin->get_x(), origin->get_y(), orient, row_num_x, row_num_y, step_x, step_y);
    }
  }
}

void write_layout_gcell_grid(const std::string& folder, IdbLayout* layout)
{
  BinaryWriter writer(section_path(folder, kLayoutDir, "gcell_grid"), ArchiveSection::kLayoutGCellGrid);
  auto* grids = layout == nullptr ? nullptr : layout->get_gcell_grid_list();
  auto& list = grids->get_gcell_grid_list();
  writer.write(static_cast<uint64_t>(list.size()));
  for (auto* grid : list) {
    writer.write_enum(grid->get_direction());
    writer.write(grid->get_start());
    writer.write(grid->get_num());
    writer.write(grid->get_space());
  }
}

void read_layout_gcell_grid(const std::string& folder, IdbLayout* layout)
{
  BinaryReader reader(section_path(folder, kLayoutDir, "gcell_grid"), ArchiveSection::kLayoutGCellGrid);
  auto* grids = layout->get_gcell_grid_list();
  grids->clear();
  uint64_t grid_count = 0;
  reader.read(grid_count);
  for (uint64_t i = 0; i < grid_count; ++i) {
    auto* grid = new IdbGCellGrid();
    grid->set_direction(reader.read_enum<IdbTrackDirection>());
    int32_t value = 0;
    reader.read(value);
    grid->set_start(value);
    reader.read(value);
    grid->set_num(value);
    reader.read(value);
    grid->set_space(value);
    grids->add_gcell_grid(grid);
  }
}

void write_layout_track_grid(const std::string& folder, IdbLayout* layout)
{
  BinaryWriter writer(section_path(folder, kLayoutDir, "track_grid"), ArchiveSection::kLayoutTrackGrid);
  auto* grids = layout == nullptr ? nullptr : layout->get_track_grid_list();
  auto& list = grids->get_track_grid_list();
  writer.write(static_cast<uint64_t>(list.size()));
  for (auto* grid : list) {
    auto* track = grid->get_track();
    writer.write_bool(track != nullptr);
    if (track != nullptr) {
      writer.write(track->get_start());
      writer.write_enum(track->get_direction());
      writer.write(track->get_pitch());
      writer.write(track->get_width());
    }
    writer.write(grid->get_track_num());
    auto layers = grid->get_layer_list();
    writer.write(static_cast<uint64_t>(layers.size()));
    for (auto* layer : layers) {
      writer.write_string(layer_name(layer));
    }
  }
}

void read_layout_track_grid(const std::string& folder, IdbLayout* layout)
{
  BinaryReader reader(section_path(folder, kLayoutDir, "track_grid"), ArchiveSection::kLayoutTrackGrid);
  auto* grids = layout->get_track_grid_list();
  grids->reset();
  uint64_t grid_count = 0;
  reader.read(grid_count);
  for (uint64_t i = 0; i < grid_count; ++i) {
    auto* grid = new IdbTrackGrid();
    if (reader.read_bool()) {
      auto* track = new IdbTrack();
      uint32_t value = 0;
      reader.read(value);
      track->set_start(value);
      track->set_direction(reader.read_enum<IdbTrackDirection>());
      reader.read(value);
      track->set_pitch(value);
      reader.read(value);
      track->set_width(value);
      grid->set_track(track);
    }
    uint32_t track_num = 0;
    reader.read(track_num);
    grid->set_track_number(track_num);
    uint64_t layer_count = 0;
    reader.read(layer_count);
    for (uint64_t j = 0; j < layer_count; ++j) {
      auto* layer = find_layer(layout->get_layers(), reader.read_string());
      if (layer != nullptr) {
        grid->add_layer_list(layer);
        if (auto* routing = dynamic_cast<IdbLayerRouting*>(layer)) {
          routing->add_track_grid(grid);
        }
      }
    }
    grids->add_track_grid(grid);
  }
}

void write_layout_cell_masters(const std::string& folder, IdbLayout* layout)
{
  BinaryWriter writer(section_path(folder, kLayoutDir, "cell_masters"), ArchiveSection::kLayoutCellMasters);
  auto* masters = layout == nullptr ? nullptr : layout->get_cell_master_list();
  auto& list = masters->get_cell_master();
  writer.write(static_cast<uint64_t>(list.size()));
  for (auto* master : list) {
    writer.write_string(master->get_name());
    writer.write_enum(master->get_type());
    writer.write_bool(master->is_symmetry_x());
    writer.write_bool(master->is_symmetry_y());
    writer.write_bool(master->is_symmetry_R90());
    writer.write_string(site_name(master->get_site()));
    writer.write(master->get_origin_x());
    writer.write(master->get_origin_y());
    writer.write(master->get_width());
    writer.write(master->get_height());

    auto terms = master->get_term_list();
    writer.write(static_cast<uint64_t>(terms.size()));
    for (auto* term : terms) {
      write_term(writer, term);
    }

    auto obs_list = master->get_obs_list();
    writer.write(static_cast<uint64_t>(obs_list.size()));
    for (auto* obs : obs_list) {
      write_obs(writer, obs);
    }
  }
}

void read_layout_cell_masters(const std::string& folder, IdbLayout* layout)
{
  BinaryReader reader(section_path(folder, kLayoutDir, "cell_masters"), ArchiveSection::kLayoutCellMasters);
  auto* masters = layout->get_cell_master_list();
  masters->reset_cell_master();
  uint64_t master_count = 0;
  reader.read(master_count);
  for (uint64_t i = 0; i < master_count; ++i) {
    std::string master_name;
    try {
      master_name = reader.read_string();
      auto* master = masters->set_cell_master(master_name);
      master->set_type(reader.read_enum<CellMasterType>());
      master->set_symmetry_x(reader.read_bool());
      master->set_symmetry_y(reader.read_bool());
      master->set_symmetry_R90(reader.read_bool());
      master->set_site(layout->get_sites()->find_site(reader.read_string()));
      int64_t origin = 0;
      reader.read(origin);
      master->set_origin_x(origin);
      reader.read(origin);
      master->set_origin_y(origin);
      uint32_t size = 0;
      reader.read(size);
      master->set_width(size);
      reader.read(size);
      master->set_height(size);

      uint64_t term_count = 0;
      reader.read(term_count);
      for (uint64_t j = 0; j < term_count; ++j) {
        try {
          if (auto* term = read_term(reader, master, layout->get_layers(), layout->get_via_list())) {
            master->add_term(term);
          }
        } catch (const std::exception& e) {
          throw std::runtime_error("master=" + master_name + " term_index=" + std::to_string(j) + "/" + std::to_string(term_count)
                                   + ": " + e.what());
        }
      }

      uint64_t obs_count = 0;
      reader.read(obs_count);
      for (uint64_t j = 0; j < obs_count; ++j) {
        try {
          if (auto* obs = read_obs(reader, layout->get_layers())) {
            master->add_obs(obs);
          }
        } catch (const std::exception& e) {
          throw std::runtime_error("master=" + master_name + " obs_index=" + std::to_string(j) + "/" + std::to_string(obs_count)
                                   + ": " + e.what());
        }
      }
    } catch (const std::exception& e) {
      throw std::runtime_error("cell_master_index=" + std::to_string(i) + "/" + std::to_string(master_count) + ": " + e.what());
    }
  }
}

void write_layout_via_rules(const std::string& folder, IdbLayout* layout)
{
  BinaryWriter writer(section_path(folder, kLayoutDir, "via_rules"), ArchiveSection::kLayoutViaRules);
  auto* rules = layout == nullptr ? nullptr : layout->get_via_rule_list();
  auto& list = rules->get_rule_list();
  writer.write(static_cast<uint64_t>(list.size()));
  for (auto* rule : list) {
    write_via_rule_generate(writer, rule);
  }
}

void read_layout_via_rules(const std::string& folder, IdbLayout* layout)
{
  BinaryReader reader(section_path(folder, kLayoutDir, "via_rules"), ArchiveSection::kLayoutViaRules);
  auto* rules = layout->get_via_rule_list();
  rules->reset();
  uint64_t rule_count = 0;
  reader.read(rule_count);
  for (uint64_t i = 0; i < rule_count; ++i) {
    if (auto* rule = read_via_rule_generate(reader, layout->get_layers())) {
      rules->add_via_rule_generate(rule);
    }
  }
}

void write_layout_vias(const std::string& folder, IdbLayout* layout)
{
  BinaryWriter writer(section_path(folder, kLayoutDir, "vias"), ArchiveSection::kLayoutVias);
  auto* vias = layout == nullptr ? nullptr : layout->get_via_list();
  auto& list = vias->get_via_list();
  writer.write(static_cast<uint64_t>(list.size()));
  for (auto* via : list) {
    write_via(writer, via);
  }
}

void read_layout_vias(const std::string& folder, IdbLayout* layout)
{
  BinaryReader reader(section_path(folder, kLayoutDir, "vias"), ArchiveSection::kLayoutVias);
  auto* vias = layout->get_via_list();
  vias->reset();
  uint64_t via_count = 0;
  reader.read(via_count);
  vias->init_via_list(static_cast<int32_t>(via_count));
  for (uint64_t i = 0; i < via_count; ++i) {
    if (auto* via = read_via(reader, layout->get_layers(), layout->get_via_rule_list())) {
      vias->add_via(via);
    }
  }
}

IdbInstance* find_instance(IdbDesign* design, const std::string& name)
{
  return design == nullptr || name.empty() ? nullptr : design->get_instance_list()->find_instance(name);
}

IdbPin* find_io_pin(IdbDesign* design, const std::string& name)
{
  return design == nullptr || name.empty() ? nullptr : design->get_io_pin_list()->find_pin(name);
}

IdbPin* find_instance_pin(IdbDesign* design, const std::string& instance_name, const std::string& pin_name)
{
  auto* instance = find_instance(design, instance_name);
  return instance == nullptr ? nullptr : instance->get_pin(pin_name);
}

void write_pin_ref(BinaryWriter& writer, IdbPin* pin)
{
  writer.write_bool(pin != nullptr);
  if (pin == nullptr) {
    return;
  }
  writer.write_bool(pin->is_io_pin());
  writer.write_string(pin->get_instance() == nullptr ? std::string() : pin->get_instance()->get_name());
  writer.write_string(pin->get_pin_name());
}

IdbPin* read_pin_ref(BinaryReader& reader, IdbDesign* design)
{
  if (!reader.read_bool()) {
    return nullptr;
  }
  const bool is_io = reader.read_bool();
  const auto instance_name = reader.read_string();
  const auto pin_name = reader.read_string();
  return is_io ? find_io_pin(design, pin_name) : find_instance_pin(design, instance_name, pin_name);
}

void write_design_metadata(const std::string& folder, IdbDesign* design)
{
  BinaryWriter writer(section_path(folder, kDesignDir, "metadata"), ArchiveSection::kDesignMetadata);
  writer.write_string(design == nullptr ? std::string() : design->get_version());
  writer.write_string(design == nullptr ? std::string() : design->get_design_name());
  write_units(writer, design == nullptr ? nullptr : design->get_units());
  auto* bus_bit_chars = design == nullptr ? nullptr : design->get_bus_bit_chars();
  writer.write_bool(bus_bit_chars != nullptr);
  if (bus_bit_chars != nullptr) {
    writer.write(bus_bit_chars->getLeftDelimiter());
    writer.write(bus_bit_chars->getRightDelimiter());
  }
}

void read_design_metadata(const std::string& folder, IdbDesign* design)
{
  BinaryReader reader(section_path(folder, kDesignDir, "metadata"), ArchiveSection::kDesignMetadata);
  design->set_version(reader.read_string());
  design->set_design_name(reader.read_string());
  design->set_units(read_units(reader));
  if (reader.read_bool()) {
    char left = '[';
    char right = ']';
    reader.read(left);
    reader.read(right);
    design->get_bus_bit_chars()->setLeftDelimiter(left);
    design->get_bus_bit_chars()->setRightDelimter(right);
  }
}

void write_design_instances(const std::string& folder, IdbDesign* design)
{
  BinaryWriter writer(section_path(folder, kDesignDir, "instances"), ArchiveSection::kDesignInstances);
  auto* instances = design == nullptr ? nullptr : design->get_instance_list();
  auto& list = instances->get_instance_list();
  writer.write(static_cast<uint64_t>(list.size()));
  for (auto* instance : list) {
    writer.write_string(instance->get_name());
    writer.write_string(cell_master_name(instance->get_cell_master()));
    writer.write_enum(instance->get_type());
    writer.write_enum(instance->get_status());
    write_coord(writer, instance->get_coordinate());
    writer.write_enum(instance->get_orient());
    writer.write(instance->get_weight());
    writer.write_string(instance->get_region() == nullptr ? std::string() : instance->get_region()->get_name());

    writer.write_bool(instance->has_halo());
    if (instance->has_halo()) {
      auto* halo = instance->get_halo();
      writer.write(halo->get_extend_lef());
      writer.write(halo->get_extend_right());
      writer.write(halo->get_extend_top());
      writer.write(halo->get_extend_bottom());
      writer.write_bool(halo->is_soft());
    }

    writer.write_bool(instance->has_route_halo());
    if (instance->has_route_halo()) {
      auto* halo = instance->get_route_halo();
      writer.write(halo->get_route_distance());
      writer.write_string(layer_name(halo->get_layer_bottom()));
      writer.write_string(layer_name(halo->get_layer_top()));
    }

    auto& obs_shapes = instance->get_obs_box_list();
    writer.write(static_cast<uint64_t>(obs_shapes.size()));
    for (auto* shape : obs_shapes) {
      write_layer_shape(writer, shape);
    }
  }
}

void read_design_instances(const std::string& folder, IdbDesign* design, IdbLayout* layout)
{
  BinaryReader reader(section_path(folder, kDesignDir, "instances"), ArchiveSection::kDesignInstances);
  auto* instances = design->get_instance_list();
  instances->reset();
  uint64_t instance_count = 0;
  reader.read(instance_count);
  instances->init(static_cast<int32_t>(instance_count));
  for (uint64_t i = 0; i < instance_count; ++i) {
    const std::string instance_name = reader.read_string();
    const std::string master_name = reader.read_string();
    auto* master = layout->get_cell_master_list()->find_cell_master(master_name);
    auto* instance = master != nullptr ? design->createInstance(instance_name, master_name) : nullptr;
    if (instance == nullptr) {
      throw std::runtime_error("read design instance failed: instance=" + instance_name + ", master=" + master_name);
    }
    instance->set_type(reader.read_enum<IdbInstanceType>());
    instance->set_status(reader.read_enum<IdbPlacementStatus>());
    if (std::unique_ptr<IdbCoordinate<int32_t>> coord(read_coord(reader)); coord != nullptr) {
      instance->set_coodinate(*coord, false);
    }
    instance->set_orient(reader.read_enum<IdbOrient>(), false);
    int32_t weight = 0;
    reader.read(weight);
    instance->set_weight(weight);
    const auto region_name = reader.read_string();
    if (!region_name.empty()) {
      instance->set_region(design->get_region_list()->add_region(region_name));
    }

    if (reader.read_bool()) {
      auto* halo = instance->set_halo();
      int32_t value = 0;
      reader.read(value);
      halo->set_extend_lef(value);
      reader.read(value);
      halo->set_extend_right(value);
      reader.read(value);
      halo->set_extend_top(value);
      reader.read(value);
      halo->set_extend_bottom(value);
      halo->set_soft(reader.read_bool());
    }

    if (reader.read_bool()) {
      auto* halo = instance->set_route_halo();
      int32_t value = 0;
      reader.read(value);
      halo->set_route_distance(value);
      halo->set_layer_bottom(find_layer(layout->get_layers(), reader.read_string()));
      halo->set_layer_top(find_layer(layout->get_layers(), reader.read_string()));
    }

    uint64_t obs_count = 0;
    reader.read(obs_count);
    auto& obs_shapes = instance->get_obs_box_list();
    for (uint64_t j = 0; j < obs_count; ++j) {
      if (auto* shape = read_layer_shape(reader, layout->get_layers())) {
        obs_shapes.emplace_back(shape);
      }
    }

    if (master != nullptr) {
      instance->set_bounding_box();
      instance->set_pin_list_coodinate();
      instance->set_halo_coodinate();
    }
  }
}

void write_design_io_pins(const std::string& folder, IdbDesign* design)
{
  BinaryWriter writer(section_path(folder, kDesignDir, "io_pins"), ArchiveSection::kDesignIoPins);
  auto* pins = design == nullptr ? nullptr : design->get_io_pin_list();
  auto& list = pins->get_pin_list();
  writer.write(static_cast<uint64_t>(list.size()));
  for (auto* pin : list) {
    writer.write_string(pin->get_pin_name());
    writer.write_string(pin->get_net_name());
    writer.write_enum(pin->get_orient());
    write_coord(writer, pin->get_average_coordinate());
    write_coord(writer, pin->get_location());
    write_coord(writer, pin->get_grid_coordinate());
    write_rect(writer, pin->get_bounding_box());
    write_term(writer, pin->get_term());
    auto& shapes = pin->get_port_box_list();
    writer.write(static_cast<uint64_t>(shapes.size()));
    for (auto* shape : shapes) {
      write_layer_shape(writer, shape);
    }
  }
}

void read_design_io_pins(const std::string& folder, IdbDesign* design, IdbLayout* layout)
{
  BinaryReader reader(section_path(folder, kDesignDir, "io_pins"), ArchiveSection::kDesignIoPins);
  auto* pins = design->get_io_pin_list();
  pins->reset();
  uint64_t pin_count = 0;
  reader.read(pin_count);
  pins->init(static_cast<int32_t>(pin_count));
  for (uint64_t i = 0; i < pin_count; ++i) {
    const auto pin_name = reader.read_string();
    auto* pin = design->createOrFindIoPin(pin_name, IdbCreatePolicy::kErrorIfExists);
    if (pin == nullptr) {
      throw std::runtime_error("read design io pin failed: pin=" + pin_name);
    }
    pin->set_net_name(reader.read_string());
    pin->set_orient(reader.read_enum<IdbOrient>());
    std::unique_ptr<IdbCoordinate<int32_t>> average(read_coord(reader));
    std::unique_ptr<IdbCoordinate<int32_t>> location(read_coord(reader));
    std::unique_ptr<IdbCoordinate<int32_t>> grid(read_coord(reader));
    if (location != nullptr) {
      pin->set_location(location->get_x(), location->get_y());
    }
    if (average != nullptr) {
      pin->set_average_coordinate(average->get_x(), average->get_y());
    }
    if (grid != nullptr && !pin->is_io_pin()) {
      pin->set_grid_coordinate(grid->get_x(), grid->get_y());
    }
    std::unique_ptr<IdbRect> bounding_box;
    if (reader.version() >= 2) {
      bounding_box.reset(read_rect(reader));
    }
    pin->set_term(read_term(reader, nullptr, layout->get_layers(), layout->get_via_list()));
    uint64_t shape_count = 0;
    reader.read(shape_count);
    auto& shapes = pin->get_port_box_list();
    for (uint64_t j = 0; j < shape_count; ++j) {
      if (auto* shape = read_layer_shape(reader, layout->get_layers())) {
        shapes.emplace_back(shape);
      }
    }
    if (bounding_box != nullptr) {
      pin->IdbObject::set_bounding_box(bounding_box->get_low_x(), bounding_box->get_low_y(), bounding_box->get_high_x(), bounding_box->get_high_y());
    } else {
      pin->set_bounding_box();
    }
  }
}

void write_design_vias(const std::string& folder, IdbDesign* design)
{
  BinaryWriter writer(section_path(folder, kDesignDir, "vias"), ArchiveSection::kDesignVias);
  auto* vias = design == nullptr ? nullptr : design->get_via_list();
  auto& list = vias->get_via_list();
  writer.write(static_cast<uint64_t>(list.size()));
  for (auto* via : list) {
    write_via(writer, via);
  }
}

void read_design_vias(const std::string& folder, IdbDesign* design, IdbLayout* layout)
{
  BinaryReader reader(section_path(folder, kDesignDir, "vias"), ArchiveSection::kDesignVias);
  auto* vias = design->get_via_list();
  vias->reset();
  uint64_t via_count = 0;
  reader.read(via_count);
  vias->init_via_list(static_cast<int32_t>(via_count));
  for (uint64_t i = 0; i < via_count; ++i) {
    if (auto* via = read_via(reader, layout->get_layers(), layout->get_via_rule_list())) {
      vias->add_via(via);
    }
  }
}

void write_design_nets(const std::string& folder, IdbDesign* design)
{
  BinaryWriter writer(section_path(folder, kDesignDir, "nets"), ArchiveSection::kDesignNets);
  auto* nets = design == nullptr ? nullptr : design->get_net_list();
  auto& list = nets->get_net_list();
  writer.write(static_cast<uint64_t>(list.size()));
  for (auto* net : list) {
    writer.write_string(net->get_net_name());
    writer.write_enum(net->get_connect_type());
    writer.write_enum(net->get_source_type());
    writer.write(net->get_weight());
    writer.write_string(net->get_original_net_name());
    writer.write(net->get_xtalk());
    writer.write_bool(net->is_fix_bump());
    writer.write(net->get_frequency());
    write_coord(writer, net->get_average_coordinate());

    auto& io_pins = net->get_io_pins()->get_pin_list();
    writer.write(static_cast<uint64_t>(io_pins.size()));
    for (auto* pin : io_pins) {
      write_pin_ref(writer, pin);
    }
    auto& inst_pins = net->get_instance_pin_list()->get_pin_list();
    writer.write(static_cast<uint64_t>(inst_pins.size()));
    for (auto* pin : inst_pins) {
      write_pin_ref(writer, pin);
    }

    auto& wires = net->get_wire_list()->get_wire_list();
    writer.write(static_cast<uint64_t>(wires.size()));
    for (auto* wire : wires) {
      write_regular_wire(writer, wire);
    }
  }
}

void read_design_nets(const std::string& folder, IdbDesign* design, IdbLayout* layout)
{
  BinaryReader reader(section_path(folder, kDesignDir, "nets"), ArchiveSection::kDesignNets);
  auto* nets = design->get_net_list();
  nets->clear_wire_list();
  uint64_t net_count = 0;
  reader.read(net_count);
  nets->init(static_cast<int32_t>(net_count));
  for (uint64_t i = 0; i < net_count; ++i) {
    std::string net_name_value;
    try {
      net_name_value = reader.read_string();
      auto* net = design->createOrFindNet(net_name_value);
      net->set_connect_type(reader.read_enum<IdbConnectType>());
      net->set_source_type(IdbEnum::GetInstance()->get_instance_property()->get_type_str(reader.read_enum<IdbInstanceType>()));
      int32_t value = 0;
      reader.read(value);
      net->set_weight(value);
      net->set_original_net_name(reader.read_string());
      reader.read(value);
      net->set_xtalk(value);
      net->set_fix_bump(reader.read_bool());
      double frequency = 0.0;
      reader.read(frequency);
      net->set_frequency(frequency);
      net->set_average_coordinate(read_coord(reader));

      uint64_t pin_count = 0;
      reader.read(pin_count);
      for (uint64_t j = 0; j < pin_count; ++j) {
        if (auto* pin = read_pin_ref(reader, design)) {
          design->connectPinToNet(pin, net);
        }
      }
      reader.read(pin_count);
      for (uint64_t j = 0; j < pin_count; ++j) {
        if (auto* pin = read_pin_ref(reader, design)) {
          design->connectPinToNet(pin, net);
        }
      }
      uint64_t wire_count = 0;
      reader.read(wire_count);
      for (uint64_t j = 0; j < wire_count; ++j) {
        try {
          if (auto* wire = read_regular_wire(reader, layout->get_layers(), layout->get_via_rule_list(), design->get_via_list())) {
            net->get_wire_list()->add_wire(wire);
          }
        } catch (const std::exception& e) {
          throw std::runtime_error("net=" + net_name_value + " wire_index=" + std::to_string(j) + "/" + std::to_string(wire_count)
                                   + ": " + e.what());
        }
      }
    } catch (const std::exception& e) {
      throw std::runtime_error("net_index=" + std::to_string(i) + "/" + std::to_string(net_count) + ": " + e.what());
    }
  }
}

void write_design_special_nets(const std::string& folder, IdbDesign* design)
{
  BinaryWriter writer(section_path(folder, kDesignDir, "special_nets"), ArchiveSection::kDesignSpecialNets);
  auto* nets = design == nullptr ? nullptr : design->get_special_net_list();
  auto& list = nets->get_net_list();
  writer.write(static_cast<uint64_t>(list.size()));
  for (auto* net : list) {
    writer.write_string(net->get_net_name());
    writer.write_enum(net->get_connect_type());
    writer.write_enum(net->get_source_type());
    writer.write_string(net->get_original_net_name());
    writer.write(net->get_weight());

    auto& pin_names = net->get_pin_string_list();
    writer.write(static_cast<uint64_t>(pin_names.size()));
    for (auto& pin_name : pin_names) {
      writer.write_string(pin_name);
    }

    auto& io_pins = net->get_io_pin_list()->get_pin_list();
    writer.write(static_cast<uint64_t>(io_pins.size()));
    for (auto* pin : io_pins) {
      write_pin_ref(writer, pin);
    }
    auto& inst_pins = net->get_instance_pin_list()->get_pin_list();
    writer.write(static_cast<uint64_t>(inst_pins.size()));
    for (auto* pin : inst_pins) {
      write_pin_ref(writer, pin);
    }

    auto& wires = net->get_wire_list()->get_wire_list();
    writer.write(static_cast<uint64_t>(wires.size()));
    for (auto* wire : wires) {
      write_special_wire(writer, wire);
    }
  }
}

void read_design_special_nets(const std::string& folder, IdbDesign* design, IdbLayout* layout)
{
  BinaryReader reader(section_path(folder, kDesignDir, "special_nets"), ArchiveSection::kDesignSpecialNets);
  auto* nets = design->get_special_net_list();
  uint64_t net_count = 0;
  reader.read(net_count);
  nets->resize(static_cast<size_t>(net_count));
  for (uint64_t i = 0; i < net_count; ++i) {
    auto* net = design->createOrFindSpecialNet(reader.read_string());
    net->set_connect_type(reader.read_enum<IdbConnectType>());
    net->set_source_type(IdbEnum::GetInstance()->get_instance_property()->get_type_str(reader.read_enum<IdbInstanceType>()));
    net->set_original_net_name(reader.read_string());
    int32_t weight = 0;
    reader.read(weight);
    net->set_weight(weight);

    uint64_t count = 0;
    reader.read(count);
    for (uint64_t j = 0; j < count; ++j) {
      net->add_pin_string(reader.read_string());
    }

    reader.read(count);
    for (uint64_t j = 0; j < count; ++j) {
      if (auto* pin = read_pin_ref(reader, design)) {
        design->connectPinToSpecialNet(pin, net);
      }
    }
    reader.read(count);
    for (uint64_t j = 0; j < count; ++j) {
      if (auto* pin = read_pin_ref(reader, design)) {
        design->connectPinToSpecialNet(pin, net);
      }
    }

    reader.read(count);
    for (uint64_t j = 0; j < count; ++j) {
      if (auto* wire = read_special_wire(reader, layout->get_layers(), layout->get_via_rule_list(), design->get_via_list())) {
        net->get_wire_list()->add_wire(wire);
      }
    }
  }
}

void write_design_blockages(const std::string& folder, IdbDesign* design)
{
  BinaryWriter writer(section_path(folder, kDesignDir, "blockages"), ArchiveSection::kDesignBlockages);
  auto list = design == nullptr ? std::vector<IdbBlockage*>{} : design->get_blockage_list()->get_blockage_list();
  writer.write(static_cast<uint64_t>(list.size()));
  for (auto* blockage : list) {
    writer.write_enum(blockage->get_type());
    writer.write_string(blockage->get_instance_name());
    writer.write_bool(blockage->is_pushdown());
    if (blockage->is_routing_blockage()) {
      auto* routing = dynamic_cast<IdbRoutingBlockage*>(blockage);
      writer.write_string(routing->get_layer_name().empty() ? layer_name(routing->get_layer()) : routing->get_layer_name());
      writer.write_bool(routing->is_slots());
      writer.write_bool(routing->is_fills());
      writer.write_bool(routing->is_except_pgnet());
      writer.write(routing->get_min_spacing());
      writer.write(routing->get_effective_width());
    } else if (blockage->is_palcement_blockage()) {
      auto* placement = dynamic_cast<IdbPlacementBlockage*>(blockage);
      writer.write_bool(placement->is_soft());
      writer.write_bool(placement->is_partial());
      writer.write(placement->get_max_density());
    }
    auto rects = blockage->get_rect_list();
    writer.write(static_cast<uint64_t>(rects.size()));
    for (auto* rect : rects) {
      write_rect(writer, rect);
    }
  }
}

void read_design_blockages(const std::string& folder, IdbDesign* design, IdbLayout* layout)
{
  BinaryReader reader(section_path(folder, kDesignDir, "blockages"), ArchiveSection::kDesignBlockages);
  auto* blockages = design->get_blockage_list();
  blockages->reset();
  uint64_t blockage_count = 0;
  reader.read(blockage_count);
  for (uint64_t i = 0; i < blockage_count; ++i) {
    const auto type = reader.read_enum<IdbBlockage::IdbBlockageType>();
    const auto instance_name = reader.read_string();
    const bool pushdown = reader.read_bool();
    IdbBlockage* blockage = nullptr;
    if (type == IdbBlockage::kRoutingBlockage) {
      const auto name = reader.read_string();
      auto* routing = blockages->add_blockage_routing(name);
      routing->set_layer(find_layer(layout->get_layers(), name));
      routing->set_slots(reader.read_bool());
      routing->set_fills(reader.read_bool());
      routing->set_except_pgnet(reader.read_bool());
      int32_t value = 0;
      reader.read(value);
      routing->set_min_spacing(value);
      reader.read(value);
      routing->set_effective_width(value);
      blockage = routing;
    } else {
      auto* placement = blockages->add_blockage_placement();
      placement->set_soft(reader.read_bool());
      placement->set_fills(reader.read_bool());
      double density = 0.0;
      reader.read(density);
      placement->set_max_density(density);
      blockage = placement;
    }
    blockage->set_instance_name(instance_name);
    blockage->set_instance(find_instance(design, instance_name));
    blockage->set_pushdown(pushdown);
    uint64_t rect_count = 0;
    reader.read(rect_count);
    for (uint64_t j = 0; j < rect_count; ++j) {
      std::unique_ptr<IdbRect> rect(read_rect(reader));
      if (rect != nullptr) {
        blockage->add_rect(rect->get_low_x(), rect->get_low_y(), rect->get_high_x(), rect->get_high_y());
      }
    }
  }
}

void write_design_regions(const std::string& folder, IdbDesign* design)
{
  BinaryWriter writer(section_path(folder, kDesignDir, "regions"), ArchiveSection::kDesignRegions);
  auto& list = design->get_region_list()->get_region_list();
  writer.write(static_cast<uint64_t>(list.size()));
  for (auto* region : list) {
    writer.write_string(region->get_name());
    writer.write_enum(region->get_type());
    auto& boundaries = region->get_boundary();
    writer.write(static_cast<uint64_t>(boundaries.size()));
    for (auto* rect : boundaries) {
      write_rect(writer, rect);
    }
    auto& instances = region->get_instance_list();
    writer.write(static_cast<uint64_t>(instances.size()));
    for (auto* instance : instances) {
      writer.write_string(instance == nullptr ? std::string() : instance->get_name());
    }
  }
}

void read_design_regions(const std::string& folder, IdbDesign* design)
{
  BinaryReader reader(section_path(folder, kDesignDir, "regions"), ArchiveSection::kDesignRegions);
  uint64_t region_count = 0;
  reader.read(region_count);
  for (uint64_t i = 0; i < region_count; ++i) {
    auto* region = design->get_region_list()->add_region(reader.read_string());
    region->clear_boundary();
    region->get_boundary().clear();
    region->get_instance_list().clear();
    region->set_type(reader.read_enum<IdbRegionType>());
    uint64_t count = 0;
    reader.read(count);
    for (uint64_t j = 0; j < count; ++j) {
      std::unique_ptr<IdbRect> rect(read_rect(reader));
      if (rect != nullptr) {
        region->add_boundary(rect->get_low_x(), rect->get_low_y(), rect->get_high_x(), rect->get_high_y());
      }
    }
    reader.read(count);
    for (uint64_t j = 0; j < count; ++j) {
      if (auto* instance = find_instance(design, reader.read_string())) {
        region->add_instance(instance);
        instance->set_region(region);
      }
    }
  }
}

void write_design_slots(const std::string& folder, IdbDesign* design)
{
  BinaryWriter writer(section_path(folder, kDesignDir, "slots"), ArchiveSection::kDesignSlots);
  auto& list = design->get_slot_list()->get_slot_list();
  writer.write(static_cast<uint64_t>(list.size()));
  for (auto* slot : list) {
    writer.write_string(slot->get_layer_name().empty() ? layer_name(slot->get_layer()) : slot->get_layer_name());
    auto& rects = slot->get_rect_list();
    writer.write(static_cast<uint64_t>(rects.size()));
    for (auto* rect : rects) {
      write_rect(writer, rect);
    }
  }
}

void read_design_slots(const std::string& folder, IdbDesign* design, IdbLayout* layout)
{
  BinaryReader reader(section_path(folder, kDesignDir, "slots"), ArchiveSection::kDesignSlots);
  auto* slots = design->get_slot_list();
  slots->reset();
  uint64_t slot_count = 0;
  reader.read(slot_count);
  for (uint64_t i = 0; i < slot_count; ++i) {
    const auto layer_name_value = reader.read_string();
    auto* slot = slots->add_slot();
    slot->set_layer_name(layer_name_value);
    slot->set_layer(find_layer(layout->get_layers(), layer_name_value));
    uint64_t rect_count = 0;
    reader.read(rect_count);
    for (uint64_t j = 0; j < rect_count; ++j) {
      std::unique_ptr<IdbRect> rect(read_rect(reader));
      if (rect != nullptr) {
        slot->add_rect(rect->get_low_x(), rect->get_low_y(), rect->get_high_x(), rect->get_high_y());
      }
    }
  }
}

void write_design_groups(const std::string& folder, IdbDesign* design)
{
  BinaryWriter writer(section_path(folder, kDesignDir, "groups"), ArchiveSection::kDesignGroups);
  auto& list = design->get_group_list()->get_group_list();
  writer.write(static_cast<uint64_t>(list.size()));
  for (auto* group : list) {
    writer.write_string(group->get_group_name());
    writer.write_string(group->get_region() == nullptr ? std::string() : group->get_region()->get_name());
    auto& instances = group->get_instance_list()->get_instance_list();
    writer.write(static_cast<uint64_t>(instances.size()));
    for (auto* instance : instances) {
      writer.write_string(instance == nullptr ? std::string() : instance->get_name());
    }
  }
}

void read_design_groups(const std::string& folder, IdbDesign* design)
{
  BinaryReader reader(section_path(folder, kDesignDir, "groups"), ArchiveSection::kDesignGroups);
  auto* groups = design->get_group_list();
  groups->reset();
  uint64_t group_count = 0;
  reader.read(group_count);
  for (uint64_t i = 0; i < group_count; ++i) {
    auto* group = groups->add_group(reader.read_string());
    const auto region_name = reader.read_string();
    group->set_region(design->get_region_list()->find_region(region_name));
    uint64_t instance_count = 0;
    reader.read(instance_count);
    for (uint64_t j = 0; j < instance_count; ++j) {
      if (auto* instance = find_instance(design, reader.read_string())) {
        group->add_instance(instance);
      }
    }
  }
}

void write_design_fills(const std::string& folder, IdbDesign* design)
{
  BinaryWriter writer(section_path(folder, kDesignDir, "fills"), ArchiveSection::kDesignFills);
  auto& list = design->get_fill_list()->get_fill_list();
  writer.write(static_cast<uint64_t>(list.size()));
  for (auto* fill : list) {
    writer.write_enum(fill->get_type());
    if (fill->get_type() == IdbFill::kLayer) {
      auto* layer_fill = fill->get_layer();
      writer.write_string(layer_name(layer_fill == nullptr ? nullptr : layer_fill->get_layer()));
      auto empty_rects = std::vector<IdbRect*>{};
      auto& rects = layer_fill == nullptr ? empty_rects : layer_fill->get_rect_list();
      writer.write(static_cast<uint64_t>(rects.size()));
      for (auto* rect : rects) {
        write_rect(writer, rect);
      }
    } else if (fill->get_type() == IdbFill::kVia) {
      auto* via_fill = fill->get_via();
      writer.write_string(via_name(via_fill == nullptr ? nullptr : via_fill->get_via()));
      auto empty_coords = std::vector<IdbCoordinate<int32_t>*>{};
      auto& coords = via_fill == nullptr ? empty_coords : via_fill->get_coordinate_list();
      writer.write(static_cast<uint64_t>(coords.size()));
      for (auto* coord : coords) {
        write_coord(writer, coord);
      }
    }
  }
}

void read_design_fills(const std::string& folder, IdbDesign* design, IdbLayout* layout)
{
  BinaryReader reader(section_path(folder, kDesignDir, "fills"), ArchiveSection::kDesignFills);
  auto* fills = design->get_fill_list();
  fills->reset();
  uint64_t fill_count = 0;
  reader.read(fill_count);
  for (uint64_t i = 0; i < fill_count; ++i) {
    const auto type = reader.read_enum<IdbFill::IdbFillType>();
    if (type == IdbFill::kLayer) {
      auto* fill = fills->add_fill_layer(find_layer(layout->get_layers(), reader.read_string()));
      uint64_t rect_count = 0;
      reader.read(rect_count);
      for (uint64_t j = 0; j < rect_count; ++j) {
        std::unique_ptr<IdbRect> rect(read_rect(reader));
        if (rect != nullptr) {
          fill->add_rect(rect->get_low_x(), rect->get_low_y(), rect->get_high_x(), rect->get_high_y());
        }
      }
    } else if (type == IdbFill::kVia) {
      const auto name = reader.read_string();
      IdbVia* via = design->get_via_list()->find_via(name);
      if (via == nullptr) {
        via = layout->get_via_list()->find_via(name);
      }
      auto* fill = fills->add_fill_via(via == nullptr ? nullptr : via->clone());
      uint64_t coord_count = 0;
      reader.read(coord_count);
      for (uint64_t j = 0; j < coord_count; ++j) {
        std::unique_ptr<IdbCoordinate<int32_t>> coord(read_coord(reader));
        if (coord != nullptr) {
          fill->add_coordinate(coord->get_x(), coord->get_y());
        }
      }
    }
  }
}

}  // namespace

bool write_layout(const std::string& folder, IdbLayout* layout, bool parallel)
{
  if (layout == nullptr) {
    std::cerr << "[IdbData] layout is null" << std::endl;
    return false;
  }
  std::filesystem::create_directories(std::filesystem::path(folder) / kLayoutDir);
  return run_parallel({
                          {"layout metadata", [=]() { write_layout_metadata(folder, layout); }},
                          {"layout units", [=]() { write_layout_units(folder, layout); }},
                          {"layout die", [=]() { write_layout_die(folder, layout); }},
                          {"layout layers", [=]() { write_layout_layers(folder, layout); }},
                          {"layout sites", [=]() { write_layout_sites(folder, layout); }},
                          {"layout rows", [=]() { write_layout_rows(folder, layout); }},
                          {"layout gcell grid", [=]() { write_layout_gcell_grid(folder, layout); }},
                          {"layout track grid", [=]() { write_layout_track_grid(folder, layout); }},
                          {"layout cell masters", [=]() { write_layout_cell_masters(folder, layout); }},
                          {"layout via rules", [=]() { write_layout_via_rules(folder, layout); }},
                          {"layout vias", [=]() { write_layout_vias(folder, layout); }},
                      },
                      parallel);
}

bool read_layout_into(const std::string& folder, IdbLayout* layout, bool parallel)
{
  if (layout == nullptr) {
    std::cerr << "[IdbData] layout is null" << std::endl;
    return false;
  }

  bool ok = run_parallel({
                             {"layout metadata", [&]() { read_layout_metadata(folder, layout); }},
                             {"layout units", [&]() { read_layout_units(folder, layout); }},
                             {"layout die", [&]() { read_layout_die(folder, layout); }},
                             {"layout layers", [&]() { read_layout_layers(folder, layout); }},
                             {"layout sites", [&]() { read_layout_sites(folder, layout); }},
                             {"layout gcell grid", [&]() { read_layout_gcell_grid(folder, layout); }},
                         },
                         parallel);
  if (!ok) {
    return false;
  }

  ok = run_parallel({
                        {"layout rows", [&]() { read_layout_rows(folder, layout); }},
                        {"layout track grid", [&]() { read_layout_track_grid(folder, layout); }},
                        {"layout via rules", [&]() { read_layout_via_rules(folder, layout); }},
                    },
                    parallel);
  if (!ok) {
    return false;
  }

  ok = run_section("layout vias", [&]() { read_layout_vias(folder, layout); });
  if (!ok) {
    return false;
  }

  return run_section("layout cell masters", [&]() { read_layout_cell_masters(folder, layout); });
}

std::unique_ptr<IdbLayout> read_layout(const std::string& folder, bool parallel)
{
  auto layout = std::make_unique<IdbLayout>();
  if (!read_layout_into(folder, layout.get(), parallel)) {
    return nullptr;
  }
  return layout;
}

bool write_design(const std::string& folder, IdbDesign* design, bool parallel)
{
  if (design == nullptr) {
    std::cerr << "[IdbData] design is null" << std::endl;
    return false;
  }
  std::filesystem::create_directories(std::filesystem::path(folder) / kDesignDir);
  return run_parallel({
                          {"design metadata", [=]() { write_design_metadata(folder, design); }},
                          {"design instances", [=]() { write_design_instances(folder, design); }},
                          {"design io pins", [=]() { write_design_io_pins(folder, design); }},
                          {"design vias", [=]() { write_design_vias(folder, design); }},
                          {"design nets", [=]() { write_design_nets(folder, design); }},
                          {"design special nets", [=]() { write_design_special_nets(folder, design); }},
                          {"design blockages", [=]() { write_design_blockages(folder, design); }},
                          {"design regions", [=]() { write_design_regions(folder, design); }},
                          {"design slots", [=]() { write_design_slots(folder, design); }},
                          {"design groups", [=]() { write_design_groups(folder, design); }},
                          {"design fills", [=]() { write_design_fills(folder, design); }},
                      },
                      parallel);
}

bool read_design_into(const std::string& folder, IdbDesign* design, IdbLayout* layout, bool parallel)
{
  if (design == nullptr || layout == nullptr) {
    std::cerr << "[IdbData] design or layout is null" << std::endl;
    return false;
  }

  bool ok = run_section("design metadata", [&]() { read_design_metadata(folder, design); });
  if (!ok) {
    return false;
  }

  ok = run_parallel({
                        {"design instances", [&]() { read_design_instances(folder, design, layout); }},
                        {"design io pins", [&]() { read_design_io_pins(folder, design, layout); }},
                        {"design vias", [&]() { read_design_vias(folder, design, layout); }},
                    },
                    parallel);
  if (!ok) {
    return false;
  }

  ok = run_parallel({
                        {"design regions", [&]() { read_design_regions(folder, design); }},
                        {"design slots", [&]() { read_design_slots(folder, design, layout); }},
                        {"design blockages", [&]() { read_design_blockages(folder, design, layout); }},
                        {"design fills", [&]() { read_design_fills(folder, design, layout); }},
                    },
                    parallel);
  if (!ok) {
    return false;
  }

  ok = run_section("design groups", [&]() { read_design_groups(folder, design); });
  if (!ok) {
    return false;
  }

  ok = run_section("design nets", [&]() { read_design_nets(folder, design, layout); });
  if (!ok) {
    return false;
  }
  return run_section("design special nets", [&]() { read_design_special_nets(folder, design, layout); });
}

std::unique_ptr<IdbDesign> read_design(const std::string& folder, IdbLayout* layout, bool parallel)
{
  auto design = std::make_unique<IdbDesign>(layout);
  if (!read_design_into(folder, design.get(), layout, parallel)) {
    return nullptr;
  }
  return design;
}

}  // namespace data_binary
}  // namespace idb
