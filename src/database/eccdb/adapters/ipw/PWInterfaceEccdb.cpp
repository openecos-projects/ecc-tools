// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2026 Peng Cheng Laboratory

#include "PWInterfaceEccdb.h"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "DataManager.hpp"
#include "Instance.hpp"
#include "Net.hpp"
#include "Pin.hpp"
#include "PinDirection.hpp"
#include "service/context/DatabaseContext.h"
#include "service/geometry/PhysicalDesignService.h"
#include "spef/SpefParser.hh"
#include "storage/design/DesignStore.h"
#include "storage/library/LibraryStore.h"

namespace ipw {
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

// DEF identifiers stay verbatim in EccDB. Apply paired SPEF spelling only
// when the parsed SPEF provides concrete evidence for the matching object.
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

class SpefPowerNameIndex
{
 public:
  explicit SpefPowerNameIndex(const spef::SpefReader* reader)
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
    const auto decoded_name = decodeSpefEscapes(raw_name);
    if (contains(names, decoded_name)) {
      return decoded_name;
    }
    return std::string{raw_name};
  }

  std::unordered_set<std::string> _net_names;
  std::unordered_set<std::string> _pin_names;
};

void registerPowerIdentifier(std::unordered_map<std::string, std::string>& raw_names,
                             const std::string_view raw_name,
                             const std::string_view power_name,
                             const std::string_view object_kind)
{
  const auto [found, inserted] = raw_names.try_emplace(std::string{power_name}, raw_name);
  if (!inserted) {
    throw std::runtime_error("Native iPW cannot project " + std::string{object_kind} + " '" + found->second + "' and '"
                             + std::string{raw_name} + "': both project to '" + std::string{power_name} + "'");
  }
}

void appendUnique(Net& net, const std::string_view pin_name)
{
  auto& pins = net.get_pin_name_list();
  for (const auto& existing : pins) {
    if (existing == pin_name) {
      return;
    }
  }
  pins.emplace_back(pin_name);
}

}  // namespace

namespace detail {

void populateDatabaseFromEccdb(PWInterface& interface, eccdb::DatabaseContext& context, const NativePowerInputs& inputs)
{
  if (context.technology() == nullptr || context.library() == nullptr) {
    throw std::runtime_error("Native iPW requires EccDB Design, Tech, and Library stores.");
  }

  const eccdb::PhysicalDesignService physical{context};
  const auto& design = context.design();
  const auto& netlist = design.netlistStorage();
  const auto& library = *context.library();
  auto& database = PWDM.getDatabase();
  database.get_instance_map().clear();
  database.get_pin_map().clear();
  database.get_net_map().clear();
  database.get_parasitic_net_map().clear();

  if (physical.hasDesignInfo()) {
    database.set_design_name(design.globalStorage().info().name);
  }

  const SpefPowerNameIndex spef_names{inputs.spef_reader};
  std::unordered_map<std::string, std::string> instance_names;
  std::unordered_map<std::string, std::string> pin_names;
  std::unordered_map<std::string, std::string> net_names;
  std::unordered_map<uint64_t, std::string> instance_pin_names;
  std::unordered_map<uint64_t, std::string> io_pin_names;

  physical.forEachInstance([&](const auto instance_id, const auto& source, const auto instance_pins) {
    const auto& master = library.cellMasterStorage().cellMaster(source.master);
    bool has_exact_spef_pin = false;
    bool has_decoded_spef_pin = false;
    for (const auto pin_id : instance_pins) {
      const auto& source_pin = netlist.instanceTerm(pin_id);
      const auto& term = library.masterTermStorage().masterTerm(source_pin.master_term);
      if (!isSignal(term.use)) {
        continue;
      }
      const auto match = spef_names.instancePin(source.name, term.name);
      has_exact_spef_pin |= match == SpefSpellingMatch::kExact;
      has_decoded_spef_pin |= match == SpefSpellingMatch::kDecoded;
    }

    const auto decoded_instance_name = decodeSpefEscapes(source.name);
    if (has_exact_spef_pin && has_decoded_spef_pin && decoded_instance_name != source.name) {
      throw std::runtime_error("Native iPW cannot project instance '" + source.name
                               + "': its SPEF connections use both raw and decoded spellings");
    }
    const auto instance_name = has_decoded_spef_pin ? decoded_instance_name : source.name;
    registerPowerIdentifier(instance_names, source.name, instance_name, "instance");

    Instance target;
    target.set_instance_id(static_cast<uint64_t>(instance_id.packed()));
    target.set_instance_name(instance_name);
    target.set_cell_name(master.name);
    target.set_is_io_cell(isIoCell(master.type));
    database.get_instance_map().try_emplace(instance_name, std::move(target));

    for (const auto pin_id : instance_pins) {
      const auto& source_pin = netlist.instanceTerm(pin_id);
      const auto& term = library.masterTermStorage().masterTerm(source_pin.master_term);
      if (!isSignal(term.use)) {
        continue;
      }
      const auto match = spef_names.instancePin(source.name, term.name);
      const auto pin_name = match == SpefSpellingMatch::kDecoded ? decodeSpefEscapes(term.name) : term.name;
      const auto full_name = instance_name + ":" + pin_name;
      const auto raw_full_name = source.name + ":" + term.name;
      registerPowerIdentifier(pin_names, raw_full_name, full_name, "pin");

      Pin target_pin;
      target_pin.set_pin_name(pin_name);
      target_pin.set_instance_name(instance_name);
      target_pin.set_direction(toDirection(term.direction));
      database.get_pin_map().try_emplace(full_name, std::move(target_pin));
      instance_pin_names.emplace(static_cast<uint64_t>(pin_id.packed()), full_name);
    }
  });

  netlist.forEachIoPin([&](const auto pin_id, const auto& source) {
    if (!isSignal(source.use)) {
      return;
    }
    const auto pin_name = spef_names.ioPinName(source.name);
    registerPowerIdentifier(pin_names, source.name, pin_name, "block terminal");

    Pin target;
    target.set_pin_name(pin_name);
    target.set_direction(toDirection(source.direction));
    target.set_is_port(true);
    database.get_pin_map().try_emplace(pin_name, std::move(target));
    io_pin_names.emplace(static_cast<uint64_t>(pin_id.packed()), pin_name);
  });

  physical.forEachRegularNet([&](const auto net_id, const auto& source) {
    if (!isSignal(source.use)) {
      return;
    }
    const auto net_name = spef_names.netName(source.name);
    registerPowerIdentifier(net_names, source.name, net_name, "net");

    Net target;
    // Keep the iDB adapter's observable order: block ports first, then
    // instance pins. iPW's graph builder uses this order for multi-driver
    // resolution, so PhysicalDesignService's generic instance-first visitor
    // is not appropriate at this boundary.
    for (const auto pin_id : netlist.ioPins(net_id)) {
      const auto name = io_pin_names.find(static_cast<uint64_t>(pin_id.packed()));
      if (name != io_pin_names.end()) {
        appendUnique(target, name->second);
      }
    }
    for (const auto pin_id : netlist.instancePins(net_id)) {
      const auto name = instance_pin_names.find(static_cast<uint64_t>(pin_id.packed()));
      if (name != instance_pin_names.end()) {
        appendUnique(target, name->second);
      }
    }
    database.get_net_map().try_emplace(net_name, std::move(target));
  });

  // These helpers select NativePowerInputs while this wrapper is active and
  // never dereference dmInst in the external-source branch.
  interface.wrapPowerIndex();
  interface.wrapTimingLibrary();
  interface.wrapParasiticLibrary();
  interface.wrapVcdActivity();
}

}  // namespace detail

}  // namespace ipw

namespace eccdb::adapters {

void bindPwSource(ipw::PWInterface& interface, DatabaseContext* context, ipw::NativePowerInputs inputs)
{
  if (context == nullptr) {
    interface.setDatabaseWrapper({});
    interface.setPowerInputs({});
    return;
  }

  interface.setPowerInputs(std::move(inputs));
  interface.setDatabaseWrapper([&interface, context](const ipw::NativePowerInputs& active_inputs) {
    ipw::detail::populateDatabaseFromEccdb(interface, *context, active_inputs);
  });
}

}  // namespace eccdb::adapters
