// ***************************************************************************************
// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2026 Peng Cheng Laboratory
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
// See the Mulan PSL v2 for more details.
// ***************************************************************************************

#include "STAInterface.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include "DataManager.hpp"
#include "Instance.hpp"
#include "Net.hpp"
#include "Pin.hpp"
#include "PinDirection.hpp"
#include "spef/SpefParser.hh"
#include "service/context/DatabaseContext.h"
#include "service/geometry/PhysicalDesignService.h"
#include "storage/design/DesignStore.h"
#include "storage/library/LibraryStore.h"

namespace ista {
namespace {

[[nodiscard]] bool isSignal(const eccdb::DesignSignalUse use)
{
  return use != eccdb::DesignSignalUse::kAnalog && use != eccdb::DesignSignalUse::kPower && use != eccdb::DesignSignalUse::kGround;
}

[[nodiscard]] bool isSignal(const eccdb::LibraryMasterTermUse use)
{
  return use != eccdb::LibraryMasterTermUse::kAnalog && use != eccdb::LibraryMasterTermUse::kPower
         && use != eccdb::LibraryMasterTermUse::kGround;
}

[[nodiscard]] PinDirection toDirection(const eccdb::LibraryMasterTermDirection direction)
{
  switch (direction) {
    case eccdb::LibraryMasterTermDirection::kInput:
      return PinDirection::kInput;
    case eccdb::LibraryMasterTermDirection::kOutput:
    case eccdb::LibraryMasterTermDirection::kOutputTriState:
      return PinDirection::kOutput;
    case eccdb::LibraryMasterTermDirection::kInOut:
    case eccdb::LibraryMasterTermDirection::kFeedThru:
      return PinDirection::kInout;
    default:
      return PinDirection::kNone;
  }
}

[[nodiscard]] PinDirection toDirection(const eccdb::DesignIoPinDirection direction)
{
  switch (direction) {
    case eccdb::DesignIoPinDirection::kInput:
      return PinDirection::kInput;
    case eccdb::DesignIoPinDirection::kOutput:
      return PinDirection::kOutput;
    case eccdb::DesignIoPinDirection::kInOut:
    case eccdb::DesignIoPinDirection::kFeedThru:
      return PinDirection::kInout;
    default:
      return PinDirection::kNone;
  }
}

[[nodiscard]] bool isIoCell(const eccdb::LibraryCellMasterType type)
{
  return type >= eccdb::LibraryCellMasterType::kPad && type <= eccdb::LibraryCellMasterType::kPadAreaIo;
}

// EccDB preserves DEF identifiers verbatim so that DEF export can round-trip
// the input. This decodes the paired SPEF/DEF escape spelling, but callers
// must only use its result when the expanded SPEF actually contains it. A
// Verilog/API identifier can legitimately contain a literal backslash.
[[nodiscard]] std::string decodeSpefEscapes(const std::string_view name)
{
  std::string result;
  result.reserve(name.size());
  for (std::size_t index = 0; index < name.size(); ++index) {
    if (name[index] == '\\' && index + 1u < name.size()) {
      ++index;
    }
    result.push_back(name[index]);
  }
  return result;
}

enum class SpefSpellingMatch : uint8_t
{
  kExact,
  kDecoded,
  kUnmatched
};

// The parser has already expanded NAME_MAP entries when this index is built.
// Keep the lookup domain-specific: a net spelling is evidence only for a net,
// and a connection/port spelling is evidence only for a timing pin. This
// avoids globally applying DEF escape syntax to semantic names supplied by an
// unrelated input source.
class SpefTimingNameIndex
{
 public:
  explicit SpefTimingNameIndex(const spef::SpefReader* reader)
  {
    if (reader == nullptr || reader->getSpefFile() == nullptr) {
      return;
    }

    const auto& exchange = *reader->getSpefFile();
    for (const auto& port : exchange.ports) {
      _pin_names.insert(port.name);
    }
    for (const auto& net : exchange.nets) {
      _net_names.insert(net.name);
      for (const auto& connection : net.conns) {
        _pin_names.insert(connection.pin_port_name);
      }
    }
  }

  [[nodiscard]] std::string netName(const std::string_view raw_name) const { return projectAtomic(raw_name, _net_names); }

  [[nodiscard]] std::string ioPinName(const std::string_view raw_name) const { return projectAtomic(raw_name, _pin_names); }

  [[nodiscard]] SpefSpellingMatch instancePin(const std::string_view raw_instance, const std::string_view raw_term) const
  {
    if (_pin_names.empty()) {
      return SpefSpellingMatch::kUnmatched;
    }

    if (contains(_pin_names, qualifiedName(raw_instance, raw_term, ':'))
        || contains(_pin_names, qualifiedName(raw_instance, raw_term, '/'))) {
      return SpefSpellingMatch::kExact;
    }

    const auto decoded_instance = decodeSpefEscapes(raw_instance);
    const auto decoded_term = decodeSpefEscapes(raw_term);
    if (contains(_pin_names, qualifiedName(decoded_instance, decoded_term, ':'))
        || contains(_pin_names, qualifiedName(decoded_instance, decoded_term, '/'))) {
      return SpefSpellingMatch::kDecoded;
    }
    return SpefSpellingMatch::kUnmatched;
  }

 private:
  [[nodiscard]] static bool contains(const std::unordered_set<std::string>& names, const std::string_view name)
  {
    return names.contains(std::string{name});
  }

  [[nodiscard]] static std::string qualifiedName(const std::string_view instance, const std::string_view term, const char separator)
  {
    std::string result;
    result.reserve(instance.size() + 1u + term.size());
    result.append(instance);
    result.push_back(separator);
    result.append(term);
    return result;
  }

  [[nodiscard]] std::string projectAtomic(const std::string_view raw_name, const std::unordered_set<std::string>& names) const
  {
    if (names.empty() || contains(names, raw_name)) {
      return std::string{raw_name};
    }
    auto decoded_name = decodeSpefEscapes(raw_name);
    if (contains(names, decoded_name)) {
      return decoded_name;
    }
    return std::string{raw_name};
  }

  std::unordered_set<std::string> _net_names;
  std::unordered_set<std::string> _pin_names;
};

void registerStaIdentifier(std::unordered_map<std::string, std::string>& raw_names,
                           const std::string_view raw_name,
                           const std::string_view timing_name,
                           const std::string_view object_kind)
{
  const auto [found, inserted] = raw_names.try_emplace(std::string{timing_name}, raw_name);
  if (!inserted) {
    throw std::runtime_error("Native iSTA cannot project " + std::string{object_kind} + " '" + found->second + "' and '"
                             + std::string{raw_name} + "': both project to '" + std::string{timing_name} + "'");
  }
}

struct BoundsAccumulator
{
  bool has_value = false;
  eccdb::Rect value;

  void add(const eccdb::Rect rectangle)
  {
    value = has_value ? value.united(rectangle) : rectangle;
    has_value = true;
  }

  void add(std::span<const eccdb::Point> points)
  {
    if (points.empty()) return;
    eccdb::Rect rectangle{.ll_x = points.front().x, .ll_y = points.front().y, .ur_x = points.front().x, .ur_y = points.front().y};
    for (const auto point : points) {
      rectangle.ll_x = std::min(rectangle.ll_x, point.x);
      rectangle.ll_y = std::min(rectangle.ll_y, point.y);
      rectangle.ur_x = std::max(rectangle.ur_x, point.x);
      rectangle.ur_y = std::max(rectangle.ur_y, point.y);
    }
    add(rectangle);
  }
};

void setLocation(Pin& pin, const BoundsAccumulator& bounds, const eccdb::Point fallback)
{
  if (bounds.has_value) {
    pin.set_x((static_cast<double>(bounds.value.ll_x) + static_cast<double>(bounds.value.ur_x)) / 2.0);
    pin.set_y((static_cast<double>(bounds.value.ll_y) + static_cast<double>(bounds.value.ur_y)) / 2.0);
  } else {
    pin.set_x(fallback.x);
    pin.set_y(fallback.y);
  }
}

}  // namespace

void STAInterface::wrapDatabaseFromEccdb()
{
  if (_database_context == nullptr || _database_context->technology() == nullptr || _database_context->library() == nullptr) {
    STALOG.error(Loc::current(), "Native iSTA requires EccDB Design, Tech, and Library stores.");
    return;
  }

  const eccdb::PhysicalDesignService physical{*_database_context};
  const auto& design = _database_context->design();
  const auto& netlist = design.netlistStorage();
  const auto& library = *_database_context->library();
  auto& database = STADM.getDatabase();
  database.get_instance_map().clear();
  database.get_pin_map().clear();
  database.get_net_map().clear();
  database.get_arc_list().clear();
  database.get_outgoing_arc_list_map().clear();
  database.get_incoming_arc_list_map().clear();
  database.get_start_point_list().clear();
  database.get_end_point_list().clear();
  database.get_timing_order_list().clear();
  database.get_timing_point_map().clear();
  database.get_timing_path_group_list().clear();

  const SpefTimingNameIndex spef_names{_native_timing_inputs.spef_reader};
  std::unordered_map<std::string, std::string> instance_names;
  std::unordered_map<std::string, std::string> pin_names;
  std::unordered_map<std::string, std::string> net_names;
  std::unordered_map<uint64_t, std::string> instance_pin_names;
  std::unordered_map<uint64_t, std::string> io_pin_names;

  if (physical.hasDesignInfo()) {
    database.set_design_name(design.globalStorage().info().name);
  }
  wrapConstraintFilePath();

  netlist.forEachInstance([&](const auto instance_id, const auto& instance, const auto instance_pins) {
    const auto& master = library.cellMasterStorage().cellMaster(instance.master);
    bool has_exact_spef_pin = false;
    bool has_decoded_spef_pin = false;
    for (const auto pin_id : instance_pins) {
      const auto& instance_pin = netlist.instancePin(pin_id);
      const auto& term = library.masterTermStorage().masterTerm(instance_pin.master_term);
      if (!isSignal(term.use)) {
        continue;
      }
      const auto match = spef_names.instancePin(instance.name, term.name);
      has_exact_spef_pin |= match == SpefSpellingMatch::kExact;
      has_decoded_spef_pin |= match == SpefSpellingMatch::kDecoded;
    }
    const auto decoded_instance_name = decodeSpefEscapes(instance.name);
    if (has_exact_spef_pin && has_decoded_spef_pin && decoded_instance_name != instance.name) {
      throw std::runtime_error("Native iSTA cannot project instance '" + instance.name
                               + "': its SPEF connections use both raw and decoded spellings");
    }

    const auto instance_name = has_decoded_spef_pin ? decoded_instance_name : instance.name;
    registerStaIdentifier(instance_names, instance.name, instance_name, "instance");
    Instance target;
    target.set_instance_id(static_cast<uint64_t>(instance_id.packed()));
    target.set_instance_name(instance_name);
    target.set_cell_name(master.name);
    target.set_is_io_cell(isIoCell(master.type));
    database.get_instance_map().try_emplace(instance_name, std::move(target));

    for (const auto pin_id : instance_pins) {
      const auto& instance_pin = netlist.instancePin(pin_id);
      const auto& term = library.masterTermStorage().masterTerm(instance_pin.master_term);
      if (!isSignal(term.use)) {
        continue;
      }
      const auto match = spef_names.instancePin(instance.name, term.name);
      const auto pin_name = match == SpefSpellingMatch::kDecoded ? decodeSpefEscapes(term.name) : term.name;
      const auto full_name = instance_name + ":" + pin_name;
      const auto raw_full_name = instance.name + ":" + term.name;
      registerStaIdentifier(pin_names, raw_full_name, full_name, "pin");
      Pin target_pin;
      target_pin.set_pin_name(pin_name);
      target_pin.set_full_name(full_name);
      target_pin.set_instance_name(instance_name);
      target_pin.set_direction(toDirection(term.direction));
      BoundsAccumulator bounds;
      if (physical.isPlaced(instance_id)) {
        physical.forEachPlacedInstancePinGeometry(
            pin_id, {.rectangle = [&](const auto& rectangle) { bounds.add(rectangle.rectangle); },
                     .polygon = [&](const auto& polygon) { bounds.add(polygon.points); }});
      }
      setLocation(target_pin, bounds, instance.origin);
      database.get_pin_map().try_emplace(full_name, std::move(target_pin));
      instance_pin_names.emplace(static_cast<uint64_t>(pin_id.packed()), full_name);
    }
  });

  netlist.forEachIoPin([&](const auto pin_id, const auto& pin) {
    if (!isSignal(pin.use)) {
      return;
    }
    const auto pin_name = spef_names.ioPinName(pin.name);
    registerStaIdentifier(pin_names, pin.name, pin_name, "block terminal");
    Pin target;
    target.set_pin_name(pin_name);
    target.set_full_name(pin_name);
    target.set_direction(toDirection(pin.direction));
    target.set_is_port(true);
    BoundsAccumulator bounds;
    physical.forEachIoPinGeometry(pin_id, {.rectangle = [&](const auto& rectangle) { bounds.add(rectangle.rectangle); },
                                            .polygon = [&](const auto& polygon) { bounds.add(polygon.points); }});
    setLocation(target, bounds, {});
    database.get_pin_map().try_emplace(pin_name, std::move(target));
    io_pin_names.emplace(static_cast<uint64_t>(pin_id.packed()), pin_name);
  });

  physical.forEachRegularNet([&](const auto net_id, const auto& source) {
    if (!isSignal(source.use)) {
      return;
    }
    const auto net_name = spef_names.netName(source.name);
    registerStaIdentifier(net_names, source.name, net_name, "net");
    Net target;
    target.set_net_name(net_name);
    physical.forEachNetPin(net_id,
                           {.instance_pin = [&](const auto pin_id, const auto&, const auto&, const auto&) {
                              const auto name = instance_pin_names.find(static_cast<uint64_t>(pin_id.packed()));
                              if (name != instance_pin_names.end() && database.get_pin_map().count(name->second) != 0u) {
                                target.get_pin_name_list().push_back(name->second);
                              }
                            },
                            .io_pin = [&](const auto pin_id, const auto&) {
                              const auto name = io_pin_names.find(static_cast<uint64_t>(pin_id.packed()));
                              if (name != io_pin_names.end() && database.get_pin_map().count(name->second) != 0u) {
                                target.get_pin_name_list().push_back(name->second);
                              }
                            }});
    database.get_net_map().try_emplace(net_name, std::move(target));
  });

  // These use only NativeTimingInputs when an EccDB source is active. They
  // never dereference dmInst in the native branch.
  wrapTimingLibrary();
  wrapParasiticLibrary();
  wrapVcdActivity();
}

}  // namespace ista
