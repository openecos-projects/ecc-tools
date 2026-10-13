#include "PyPlaceDB.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <vector>

#include "IdbCellMaster.h"
#include "IdbDesign.h"
#include "IdbInstance.h"
#include "IdbTerm.h"

namespace python_interface {
namespace py = pybind11;

namespace {

py::dict make_summary()
{
  py::dict summary;
  summary["ok"] = false;
  summary["accepted_count"] = 0;
  summary["rejected_count"] = 0;
  summary["failed_count"] = 0;
  summary["committed_instance_names"] = py::list();
  summary["rejected_actions"] = py::list();
  summary["requires_refresh"] = false;
  summary["message"] = "";
  return summary;
}

void add_rejection(py::dict& summary, int index, const std::string& reason)
{
  auto actions = summary["rejected_actions"].cast<py::list>();
  py::dict action;
  action["index"] = index;
  action["reason"] = reason;
  actions.append(std::move(action));
  summary["rejected_count"] = actions.size();
}

bool has_compatible_terms(idb::IdbCellMaster& source, idb::IdbCellMaster& target)
{
  const auto source_terms = source.get_term_list();
  const auto target_terms = target.get_term_list();
  if (source_terms.size() != target_terms.size()) {
    return false;
  }

  for (const auto* source_term : source_terms) {
    if (source_term == nullptr) {
      return false;
    }
    const auto* target_term = target.findTerm(source_term->get_name());
    if (target_term == nullptr || target_term->get_direction() != source_term->get_direction()
        || target_term->get_type() != source_term->get_type()) {
      return false;
    }
  }
  return true;
}

bool is_sizable_master(idb::IdbCellMaster* master)
{
  return master != nullptr && master->is_core() && !master->is_core_filler() && !master->is_endcap();
}

std::string action_string(const py::dict& action, const char* key)
{
  if (!action.contains(key) || action[key].is_none()) {
    return {};
  }
  try {
    return py::cast<std::string>(action[key]);
  } catch (const py::cast_error&) {
    return {};
  }
}

std::string buffer_action_digest(const py::list& actions)
{
  py::module_ json = py::module_::import("json");
  py::module_ hashlib = py::module_::import("hashlib");
  py::kwargs kwargs;
  kwargs["allow_nan"] = false;
  kwargs["separators"] = py::make_tuple(",", ":");
  kwargs["sort_keys"] = true;
  py::tuple positional = py::make_tuple(actions);
  py::object serialized = json.attr("dumps")(*positional, **kwargs);
  py::object encoded = serialized.attr("encode")("utf-8");
  return py::cast<std::string>(hashlib.attr("sha256")(encoded).attr("hexdigest")());
}

bool action_coordinate(const py::dict& action, const char* key, int32_t* result)
{
  if (!action.contains(key) || action[key].is_none()) {
    return false;
  }
  double value = 0.0;
  try {
    value = py::cast<double>(action[key]);
  } catch (const py::cast_error&) {
    return false;
  }
  if (!std::isfinite(value) || std::round(value) != value || value < std::numeric_limits<int32_t>::min()
      || value > std::numeric_limits<int32_t>::max()) {
    return false;
  }
  *result = static_cast<int32_t>(value);
  return true;
}

bool pin_name_matches(idb::IdbPin* pin, const std::string& requested)
{
  if (pin == nullptr) {
    return false;
  }
  const std::string pin_name = pin->get_pin_name();
  if (pin->get_instance() == nullptr) {
    return pin_name == requested;
  }
  const std::string instance_name = pin->get_instance()->get_name();
  return requested == instance_name + "/" + pin_name || requested == instance_name + ":" + pin_name
         || requested == instance_name + " " + pin_name;
}

idb::IdbPin* find_net_pin(idb::IdbNet* net, const std::string& requested)
{
  if (net == nullptr) {
    return nullptr;
  }
  for (auto* pin : net->get_instance_pin_list()->get_pin_list()) {
    if (pin_name_matches(pin, requested)) {
      return pin;
    }
  }
  for (auto* pin : net->get_io_pins()->get_pin_list()) {
    if (pin_name_matches(pin, requested)) {
      return pin;
    }
  }
  return nullptr;
}

std::string pin_display_name(idb::IdbPin* pin)
{
  if (pin == nullptr || pin->get_instance() == nullptr) {
    return pin == nullptr ? std::string{} : pin->get_pin_name();
  }
  return pin->get_instance()->get_name() + "/" + pin->get_pin_name();
}

py::dict make_buffer_summary()
{
  py::dict summary;
  summary["status"] = "failed";
  summary["ok"] = false;
  summary["accepted_count"] = 0;
  summary["rejected_count"] = 0;
  summary["failed_count"] = 0;
  summary["committed_buffer_instance_names"] = py::list();
  summary["committed_downstream_net_names"] = py::list();
  summary["moved_load_pin_names"] = py::list();
  summary["rejected_actions"] = py::list();
  summary["topology_mutated"] = false;
  summary["requires_refresh"] = false;
  summary["refresh_required"] = false;
  summary["message"] = "";
  return summary;
}

void add_buffer_rejection(py::dict& summary, int index, const std::string& reason)
{
  auto actions = summary["rejected_actions"].cast<py::list>();
  py::dict action;
  action["index"] = index;
  action["reason"] = reason;
  actions.append(std::move(action));
  summary["rejected_count"] = actions.size();
}

void refresh_instance_geometry(idb::IdbInstance* instance)
{
  if (instance == nullptr || instance->get_cell_master() == nullptr) {
    return;
  }
  instance->set_bounding_box();
  instance->set_pin_list_coodinate();
  instance->set_halo_coodinate();
  instance->set_obs_box_list();
}

}  // namespace

py::dict PyPlaceDB::applySizing(const std::vector<int>& cell_ids, const std::vector<std::string>& target_master_names)
{
  py::dict summary = make_summary();
  if (_db == nullptr || _db->get_idb_design() == nullptr) {
    summary["message"] = "native IDB is not initialized";
    summary["failed_count"] = 1;
    return summary;
  }
  if (cell_ids.size() != target_master_names.size()) {
    summary["message"] = "cell_ids and target_master_names must have equal lengths";
    summary["failed_count"] = 1;
    return summary;
  }

  _design = _db->get_idb_design();
  if (cell_ids.empty()) {
    summary["ok"] = true;
    summary["message"] = "no sizing actions";
    return summary;
  }

  struct Candidate
  {
    std::string instance_name;
    std::string old_master_name;
    std::string target_master_name;
  };
  std::vector<Candidate> candidates;
  candidates.reserve(cell_ids.size());
  std::unordered_set<int> seen_ids;

  for (std::size_t i = 0; i < cell_ids.size(); ++i) {
    const int node_id = cell_ids[i];
    if (node_id < 0 || static_cast<std::size_t>(node_id) >= node_names.size()) {
      add_rejection(summary, static_cast<int>(i), "cell id is outside the PyPlaceDB node range");
      continue;
    }
    if (!seen_ids.insert(node_id).second) {
      add_rejection(summary, static_cast<int>(i), "duplicate cell id in one sizing transaction");
      continue;
    }

    const std::string instance_name = py::cast<std::string>(node_names[static_cast<std::size_t>(node_id)]);
    auto* instance = _design->get_instance_list()->find_instance(instance_name);
    if (instance == nullptr || instance->get_cell_master() == nullptr) {
      add_rejection(summary, static_cast<int>(i), "cell id does not resolve to an IDB instance");
      continue;
    }
    if (instance->is_fixed() || instance->is_cover()) {
      add_rejection(summary, static_cast<int>(i), "fixed or cover instances cannot be resized");
      continue;
    }
    if (!is_sizable_master(instance->get_cell_master())) {
      add_rejection(summary, static_cast<int>(i), "instance is not a movable standard cell");
      continue;
    }

    const std::string& target_name = target_master_names[i];
    if (_design->get_layout() == nullptr || _design->get_layout()->get_cell_master_list() == nullptr) {
      add_rejection(summary, static_cast<int>(i), "native cell-master database is not initialized");
      continue;
    }
    auto* target_master = _design->get_layout()->get_cell_master_list()->find_cell_master(target_name);
    if (!is_sizable_master(target_master)) {
      add_rejection(summary, static_cast<int>(i), "target master is not a movable standard cell");
      continue;
    }
    if (!has_compatible_terms(*instance->get_cell_master(), *target_master)) {
      add_rejection(summary, static_cast<int>(i), "target master pin names, directions or net types are incompatible");
      continue;
    }

    candidates.push_back({instance_name, instance->get_cell_master()->get_name(), target_name});
  }

  // Preflight is all-or-nothing. No native object is touched when any action
  // is rejected, which gives callers a deterministic rollback boundary.
  if (summary["rejected_count"].cast<std::size_t>() != 0) {
    summary["message"] = "sizing preflight rejected one or more actions; database unchanged";
    return summary;
  }

  if (!_design->validateConnectivity(false).ok) {
    summary["message"] = "native IDB connectivity is invalid before sizing";
    summary["failed_count"] = 1;
    return summary;
  }

  std::vector<Candidate> applied;
  applied.reserve(candidates.size());
  auto rollback = [&]() {
    bool rollback_ok = true;
    for (auto it = applied.rbegin(); it != applied.rend(); ++it) {
      const bool replaced = _design->replaceInstanceMaster(it->instance_name, it->old_master_name, true);
      auto* restored = _design->get_instance_list()->find_instance(it->instance_name);
      refresh_instance_geometry(restored);
      rollback_ok = replaced && rollback_ok;
    }
    return rollback_ok;
  };

  for (const auto& candidate : candidates) {
    if (!_design->replaceInstanceMaster(candidate.instance_name, candidate.target_master_name, true)) {
      const bool rollback_ok = rollback();
      summary["failed_count"] = 1;
      summary["message"] = rollback_ok ? "native sizing failed; journal rollback applied"
                                        : "native sizing failed and journal rollback failed";
      return summary;
    }
    refresh_instance_geometry(_design->get_instance_list()->find_instance(candidate.instance_name));
    applied.push_back(candidate);
  }

  if (!_design->validateConnectivity(false).ok) {
    const bool rollback_ok = rollback();
    summary["failed_count"] = 1;
    summary["message"] = rollback_ok ? "sizing broke IDB connectivity; journal rollback applied"
                                      : "sizing broke IDB connectivity and journal rollback failed";
    return summary;
  }

  py::list committed_names;
  for (const auto& candidate : applied) {
    committed_names.append(candidate.instance_name);
  }
  summary["ok"] = true;
  summary["accepted_count"] = applied.size();
  summary["committed_instance_names"] = std::move(committed_names);
  summary["requires_refresh"] = true;
  summary["message"] = "sizing committed to native IDB; rebuild PyPlaceDB before reuse";
  native_state_dirty = true;
  ++native_mutation_epoch;
  return summary;
}

py::dict PyPlaceDB::applyBufferActions(const py::list& actions, const std::string& action_digest)
{
  py::dict summary = make_buffer_summary();
  summary["action_count"] = actions.size();
  if (_db == nullptr || _db->get_idb_design() == nullptr) {
    summary["failed_count"] = actions.size();
    summary["message"] = "native IDB is not initialized";
    return summary;
  }
  if (actions.empty()) {
    summary["status"] = "skipped";
    summary["ok"] = true;
    summary["message"] = "no buffer actions";
    return summary;
  }

  const std::string computed_digest = buffer_action_digest(actions);
  summary["computed_action_digest"] = computed_digest;
  summary["action_digest"] = action_digest;
  if (action_digest.empty() || action_digest != computed_digest) {
    summary["failed_count"] = actions.size();
    summary["message"] = "buffer action digest mismatch; database unchanged";
    return summary;
  }

  _design = _db->get_idb_design();
  if (_design->get_instance_list() == nullptr || _design->get_net_list() == nullptr || _design->get_layout() == nullptr
      || _design->get_layout()->get_cell_master_list() == nullptr) {
    summary["failed_count"] = actions.size();
    summary["message"] = "native IDB connectivity or cell-master database is not initialized";
    return summary;
  }
  if (!_design->validateConnectivity(false).ok) {
    summary["failed_count"] = actions.size();
    summary["message"] = "native IDB connectivity is invalid before buffering";
    return summary;
  }

  struct Candidate
  {
    idb::IdbNet* source_net;
    idb::IdbPin* driver_pin;
    std::vector<idb::IdbPin*> loads;
    std::string master_name;
    std::string input_pin_name;
    std::string output_pin_name;
    int32_t x;
    int32_t y;
    int32_t tree_depth;
    int32_t parent_id;
    int32_t child_id;
    double split_ratio;
  };
  std::vector<Candidate> candidates;
  candidates.reserve(actions.size());

  for (std::size_t index = 0; index < actions.size(); ++index) {
    if (!py::isinstance<py::dict>(actions[index])) {
      add_buffer_rejection(summary, static_cast<int>(index), "buffer action must be a mapping");
      continue;
    }
    py::dict action = py::reinterpret_borrow<py::dict>(actions[index]);
    if (action_string(action, "action_kind") != "buffer_insert") {
      add_buffer_rejection(summary, static_cast<int>(index), "action_kind must be buffer_insert");
      continue;
    }

    const std::string net_name = action_string(action, "net_name");
    auto* source_net = _design->get_net_list()->find_net(net_name);
    if (source_net == nullptr) {
      add_buffer_rejection(summary, static_cast<int>(index), "source net does not exist");
      continue;
    }
    if (source_net->is_clock() || source_net->is_pdn()) {
      add_buffer_rejection(summary, static_cast<int>(index), "clock, power and ground nets cannot be buffered");
      continue;
    }

    const std::string driver_name = action_string(action, "driver_pin_name");
    auto* driver_pin = find_net_pin(source_net, driver_name);
    if (driver_pin == nullptr || source_net->get_driving_pin() != driver_pin) {
      add_buffer_rejection(summary, static_cast<int>(index), "driver pin is not the source net driver");
      continue;
    }

    std::vector<std::string> load_names;
    if (action.contains("downstream_pin_names") && !action["downstream_pin_names"].is_none()) {
      if (!py::isinstance<py::list>(action["downstream_pin_names"])) {
        add_buffer_rejection(summary, static_cast<int>(index), "downstream_pin_names must be a list");
        continue;
      }
      bool load_name_error = false;
      for (const auto& item : action["downstream_pin_names"].cast<py::list>()) {
        try {
          load_names.push_back(py::cast<std::string>(item));
        } catch (const py::cast_error&) {
          load_name_error = true;
          break;
        }
      }
      if (load_name_error) {
        add_buffer_rejection(summary, static_cast<int>(index), "downstream_pin_names must contain strings");
        continue;
      }
    }
    if (load_names.empty()) {
      const std::string singleton = action_string(action, "load_pin_name");
      if (!singleton.empty()) {
        load_names.push_back(singleton);
      }
    }
    if (load_names.empty()) {
      add_buffer_rejection(summary, static_cast<int>(index), "downstream load partition is empty");
      continue;
    }
    std::vector<idb::IdbPin*> loads;
    std::unordered_set<idb::IdbPin*> seen_loads;
    bool load_error = false;
    for (const auto& load_name : load_names) {
      auto* load = find_net_pin(source_net, load_name);
      if (load == nullptr || load == driver_pin || !seen_loads.insert(load).second || load->get_special_net() != nullptr) {
        load_error = true;
        break;
      }
      loads.push_back(load);
    }
    if (load_error) {
      add_buffer_rejection(summary, static_cast<int>(index), "downstream load is missing, duplicated or not a regular-net pin");
      continue;
    }

    const std::string master_name = action_string(action, "buffer_master_name");
    auto* master = _design->get_layout()->get_cell_master_list()->find_cell_master(master_name);
    if (!is_sizable_master(master)) {
      add_buffer_rejection(summary, static_cast<int>(index), "buffer master does not exist as a standard cell");
      continue;
    }
    std::string input_pin_name;
    std::string output_pin_name;
    for (auto* term : master->get_term_list()) {
      if (term == nullptr || term->get_type() == idb::IdbConnectType::kPower || term->get_type() == idb::IdbConnectType::kGround) {
        continue;
      }
      if (term->get_direction() == idb::IdbConnectDirection::kInput && input_pin_name.empty()) {
        input_pin_name = term->get_name();
      } else if ((term->get_direction() == idb::IdbConnectDirection::kOutput
                  || term->get_direction() == idb::IdbConnectDirection::kOutputTriState)
                 && output_pin_name.empty()) {
        output_pin_name = term->get_name();
      }
    }
    if (input_pin_name.empty() || output_pin_name.empty()) {
      add_buffer_rejection(summary, static_cast<int>(index), "buffer master must have one signal input and output");
      continue;
    }

    int32_t x = 0;
    int32_t y = 0;
    if (!action_coordinate(action, "candidate_location_x_dbu", &x) || !action_coordinate(action, "candidate_location_y_dbu", &y)) {
      add_buffer_rejection(summary, static_cast<int>(index), "buffer candidate coordinates must be finite integer DBU values");
      continue;
    }
    const int32_t depth = action.contains("segment_tree_depth") ? action["segment_tree_depth"].cast<int32_t>() : -1;
    const int32_t parent = action.contains("segment_parent_node_id") ? action["segment_parent_node_id"].cast<int32_t>() : -1;
    const int32_t child = action.contains("segment_child_node_id") ? action["segment_child_node_id"].cast<int32_t>() : -1;
    const double ratio = action.contains("segment_split_ratio") ? action["segment_split_ratio"].cast<double>() : 0.0;
    candidates.push_back({source_net, driver_pin, std::move(loads), master_name, input_pin_name, output_pin_name, x, y,
                          depth, parent, child, ratio});
  }

  if (summary["rejected_count"].cast<std::size_t>() != 0) {
    summary["message"] = "buffer preflight rejected one or more actions; database unchanged";
    return summary;
  }

  // Insert ancestor segments first. Descendants then resolve the upstream net
  // from their current loads, including repeaters on the same original edge.
  std::stable_sort(candidates.begin(), candidates.end(), [](const auto& lhs, const auto& rhs) {
    return std::make_tuple(lhs.source_net->get_net_name(), lhs.tree_depth, lhs.parent_id, lhs.child_id, lhs.split_ratio)
           < std::make_tuple(rhs.source_net->get_net_name(), rhs.tree_depth, rhs.parent_id, rhs.child_id, rhs.split_ratio);
  });

  struct Applied
  {
    std::string instance_name;
    std::string downstream_net_name;
    idb::IdbNet* source_net;
    std::vector<idb::IdbPin*> loads;
  };
  std::vector<Applied> applied;
  std::unordered_set<idb::IdbPin*> committed_loads;
  auto rollback = [&]() {
    bool rollback_ok = true;
    for (auto it = applied.rbegin(); it != applied.rend(); ++it) {
      auto* downstream = _design->get_net_list()->find_net(it->downstream_net_name);
      if (downstream != nullptr) {
        for (auto* load : it->loads) {
          _design->disconnectPinFromNet(load);
          rollback_ok = _design->connectPinToNet(load, it->source_net) && rollback_ok;
        }
        _design->disconnectAllPinsFromNet(downstream);
        rollback_ok = _design->removeNetSafe(it->downstream_net_name) && rollback_ok;
      }
      rollback_ok = _design->removeInstanceSafe(it->instance_name) && rollback_ok;
    }
    return rollback_ok;
  };

  for (const auto& candidate : candidates) {
    if (candidate.tree_depth < 0 && std::any_of(candidate.loads.begin(), candidate.loads.end(), [&](auto* load) {
          return committed_loads.find(load) != committed_loads.end();
        })) {
      const bool rollback_ok = rollback();
      summary["failed_count"] = 1;
      summary["message"] = rollback_ok ? "buffer load partition overlaps a prior action; journal rollback applied"
                                        : "buffer load partition overlaps a prior action and journal rollback failed";
      return summary;
    }
    auto* upstream = candidate.loads.front()->get_net();
    if (upstream == nullptr || std::any_of(candidate.loads.begin(), candidate.loads.end(), [&](auto* load) {
          return load->get_net() != upstream;
        })) {
      const bool rollback_ok = rollback();
      summary["failed_count"] = 1;
      summary["message"] = rollback_ok ? "buffer partition spans current branches; journal rollback applied"
                                        : "buffer partition spans current branches and journal rollback failed";
      return summary;
    }
    const std::string instance_name = _design->makeUniqueInstanceName("buffer_coord");
    auto* buffer = _design->createInstance(instance_name, candidate.master_name, idb::IdbInstanceType::kTiming,
                                            idb::IdbPlacementStatus::kPlaced, idb::IdbOrient::kN_R0, candidate.x, candidate.y,
                                            idb::IdbCreatePolicy::kErrorIfExists);
    if (buffer == nullptr) {
      const bool rollback_ok = rollback();
      summary["failed_count"] = 1;
      summary["message"] = rollback_ok ? "buffer instance creation failed; journal rollback applied"
                                        : "buffer instance creation failed and journal rollback failed";
      return summary;
    }
    auto* input_pin = buffer->get_pin_by_term(candidate.input_pin_name);
    auto* output_pin = buffer->get_pin_by_term(candidate.output_pin_name);
    const std::string downstream_name = _design->makeUniqueNetName(candidate.source_net->get_net_name() + "__buffer");
    auto* downstream = _design->createOrFindNet(downstream_name, idb::IdbConnectType::kSignal, idb::IdbCreatePolicy::kErrorIfExists);
    if (input_pin == nullptr || output_pin == nullptr || downstream == nullptr
        || !_design->connectPinToNet(input_pin, upstream) || !_design->connectPinToNet(output_pin, downstream)) {
      _design->removeInstanceSafe(instance_name);
      if (downstream != nullptr) {
        _design->removeNetSafe(downstream_name);
      }
      const bool rollback_ok = rollback();
      summary["failed_count"] = 1;
      summary["message"] = rollback_ok ? "buffer topology setup failed; journal rollback applied"
                                        : "buffer topology setup failed and journal rollback failed";
      return summary;
    }
    applied.push_back({instance_name, downstream_name, upstream, candidate.loads});
    for (auto* load : candidate.loads) {
      if (!_design->disconnectPinFromNet(load) || !_design->connectPinToNet(load, downstream)) {
        const bool rollback_ok = rollback();
        summary["failed_count"] = 1;
        summary["message"] = rollback_ok ? "buffer load reconnect failed; journal rollback applied"
                                          : "buffer load reconnect failed and journal rollback failed";
        return summary;
      }
    }
    committed_loads.insert(candidate.loads.begin(), candidate.loads.end());
  }

  if (!_design->validateConnectivity(false).ok) {
    const bool rollback_ok = rollback();
    summary["failed_count"] = 1;
    summary["message"] = rollback_ok ? "buffer topology broke IDB connectivity; journal rollback applied"
                                      : "buffer topology broke IDB connectivity and journal rollback failed";
    return summary;
  }

  py::list buffer_names;
  py::list downstream_names;
  py::list moved_load_names;
  for (const auto& item : applied) {
    buffer_names.append(item.instance_name);
    downstream_names.append(item.downstream_net_name);
    for (auto* load : item.loads) {
      moved_load_names.append(pin_display_name(load));
    }
  }
  summary["status"] = "accepted";
  summary["ok"] = true;
  summary["accepted_count"] = applied.size();
  summary["committed_buffer_instance_names"] = std::move(buffer_names);
  summary["committed_downstream_net_names"] = std::move(downstream_names);
  summary["moved_load_pin_names"] = std::move(moved_load_names);
  summary["topology_mutated"] = true;
  summary["requires_refresh"] = true;
  summary["refresh_required"] = true;
  summary["message"] = "buffer topology committed to native IDB; rebuild PyPlaceDB before reuse";
  native_state_dirty = true;
  ++native_mutation_epoch;
  return summary;
}

}  // namespace python_interface
