// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// ***************************************************************************************

#include "view_json_edit_apply.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "IdbDesign.h"
#include "IdbEnum.h"
#include "IdbInstance.h"
#include "IdbPins.h"
#include "def_service.h"
#include "json.hpp"

namespace idb {
namespace {

using ViewJson = nlohmann::ordered_json;

bool requireObject(const ViewJson& json, const std::string& context)
{
  if (json.is_object()) {
    return true;
  }
  std::cout << "Apply view json edits failed: " << context << " must be an object." << std::endl;
  return false;
}

bool readIntField(const ViewJson& json, const std::string& key, int& value, const std::string& context)
{
  auto iter = json.find(key);
  if (iter == json.end() || !iter->is_number_integer()) {
    std::cout << "Apply view json edits failed: " << context << " requires integer field `" << key << "`." << std::endl;
    return false;
  }
  value = iter->get<int>();
  return true;
}

bool readStringField(const ViewJson& json, const std::string& key, std::string& value, const std::string& context)
{
  auto iter = json.find(key);
  if (iter == json.end() || !iter->is_string()) {
    std::cout << "Apply view json edits failed: " << context << " requires string field `" << key << "`." << std::endl;
    return false;
  }
  value = iter->get<std::string>();
  return true;
}

bool checkStringFieldValue(const ViewJson& json, const std::string& key, const std::string& expected, const std::string& context)
{
  std::string value;
  if (!readStringField(json, key, value, context)) {
    return false;
  }
  if (value != expected) {
    std::cout << "Apply view json edits failed: " << context << " field `" << key << "` must be `" << expected << "`." << std::endl;
    return false;
  }
  return true;
}

bool readOptionalStringField(const ViewJson& json, const std::string& key, std::string& value, bool& exists, const std::string& context)
{
  auto iter = json.find(key);
  if (iter == json.end() || iter->is_null()) {
    exists = false;
    value.clear();
    return true;
  }
  if (!iter->is_string()) {
    std::cout << "Apply view json edits failed: " << context << " field `" << key << "` must be a string." << std::endl;
    return false;
  }
  exists = true;
  value = iter->get<std::string>();
  return true;
}

bool readPointField(const ViewJson& json, const std::string& key, int32_t& x, int32_t& y, const std::string& context)
{
  auto iter = json.find(key);
  if (iter == json.end() || !iter->is_array() || iter->size() != 2 || !(*iter)[0].is_number_integer() || !(*iter)[1].is_number_integer()) {
    std::cout << "Apply view json edits failed: " << context << " requires point field `" << key << "` as [x, y]." << std::endl;
    return false;
  }
  x = (*iter)[0].get<int32_t>();
  y = (*iter)[1].get<int32_t>();
  return true;
}

std::string toUpper(std::string value)
{
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return std::toupper(c); });
  return value;
}

IdbOrient parseOrient(const std::string& orient_name)
{
  const std::string upper_orient = toUpper(orient_name);
  IdbOrient orient = IdbEnum::GetInstance()->get_site_property()->get_orient_value(upper_orient);
  if (orient != IdbOrient::kNone) {
    return orient;
  }

  if (upper_orient == "N_R0") {
    return IdbOrient::kN_R0;
  }
  if (upper_orient == "W_R90") {
    return IdbOrient::kW_R90;
  }
  if (upper_orient == "S_R180") {
    return IdbOrient::kS_R180;
  }
  if (upper_orient == "E_R270") {
    return IdbOrient::kE_R270;
  }
  if (upper_orient == "FN_MY") {
    return IdbOrient::kFN_MY;
  }
  if (upper_orient == "FS_MX") {
    return IdbOrient::kFS_MX;
  }
  if (upper_orient == "FW_MX90") {
    return IdbOrient::kFW_MX90;
  }
  if (upper_orient == "FE_MY90") {
    return IdbOrient::kFE_MY90;
  }
  return IdbOrient::kNone;
}

bool readOrientValue(const std::string& orient_name, IdbOrient& orient, const std::string& context)
{
  orient = parseOrient(orient_name);
  if (orient == IdbOrient::kNone) {
    std::cout << "Apply view json edits failed: " << context << " has invalid orient `" << orient_name << "`." << std::endl;
    return false;
  }
  return true;
}

bool readStatusValue(const std::string& status_name, IdbPlacementStatus& status, const std::string& context)
{
  const std::string upper_status = toUpper(status_name);
  status = IdbEnum::GetInstance()->get_instance_property()->get_status(upper_status);
  if (status == IdbPlacementStatus::kNone && upper_status != "NONE") {
    std::cout << "Apply view json edits failed: " << context << " has invalid status `" << status_name << "`." << std::endl;
    return false;
  }
  return true;
}

IdbInstance* instanceById(IdbDesign* design, int id)
{
  if (design == nullptr || design->get_instance_list() == nullptr || id < 0) {
    return nullptr;
  }
  int current_id = 0;
  for (auto* inst : design->get_instance_list()->get_instance_list()) {
    if (inst == nullptr) {
      continue;
    }
    if (current_id == id) {
      return inst;
    }
    ++current_id;
  }
  return nullptr;
}

IdbPin* ioPinById(IdbDesign* design, int id)
{
  if (design == nullptr || design->get_io_pin_list() == nullptr || id < 0) {
    return nullptr;
  }
  int current_id = 0;
  for (auto* pin : design->get_io_pin_list()->get_pin_list()) {
    if (pin == nullptr) {
      continue;
    }
    if (current_id == id) {
      return pin;
    }
    ++current_id;
  }
  return nullptr;
}

bool applyOrient(IdbInstance* inst, const std::string& orient_name, const std::string& context, bool dry_run)
{
  IdbOrient orient = IdbOrient::kNone;
  if (!readOrientValue(orient_name, orient, context)) {
    return false;
  }
  if (!dry_run) {
    inst->set_orient(orient);
  }
  return true;
}

bool applyOrient(IdbPin* pin, const std::string& orient_name, const std::string& context, bool dry_run)
{
  IdbOrient orient = IdbOrient::kNone;
  if (!readOrientValue(orient_name, orient, context)) {
    return false;
  }
  if (!dry_run) {
    pin->set_orient(orient);
    pin->set_bounding_box();
  }
  return true;
}

bool applyStatus(IdbInstance* inst, const std::string& status_name, const std::string& context, bool dry_run)
{
  IdbPlacementStatus status = IdbPlacementStatus::kNone;
  if (!readStatusValue(status_name, status, context)) {
    return false;
  }
  if (!dry_run) {
    inst->set_status(status);
  }
  return true;
}

bool applyMoveInstance(IdbDesign* design, const ViewJson& edit, const std::string& context, bool dry_run)
{
  int inst_id = -1;
  int32_t x = 0;
  int32_t y = 0;
  if (!readIntField(edit, "inst_id", inst_id, context) || !readPointField(edit, "origin", x, y, context)) {
    return false;
  }
  IdbInstance* inst = instanceById(design, inst_id);
  if (inst == nullptr) {
    std::cout << "Apply view json edits failed: " << context << " references invalid inst_id " << inst_id << "." << std::endl;
    return false;
  }

  std::string orient_name;
  bool has_orient = false;
  if (!readOptionalStringField(edit, "orient", orient_name, has_orient, context)) {
    return false;
  }
  IdbOrient orient = IdbOrient::kNone;
  if (has_orient && !readOrientValue(orient_name, orient, context)) {
    return false;
  }

  std::string status_name;
  bool has_status = false;
  if (!readOptionalStringField(edit, "status", status_name, has_status, context)) {
    return false;
  }
  IdbPlacementStatus status = IdbPlacementStatus::kNone;
  if (has_status && !readStatusValue(status_name, status, context)) {
    return false;
  }

  if (!dry_run) {
    inst->set_coodinate(x, y);
    if (has_orient) {
      inst->set_orient(orient);
    }
    if (has_status) {
      inst->set_status(status);
    }
  }
  return true;
}

bool applyOrientInstance(IdbDesign* design, const ViewJson& edit, const std::string& context, bool dry_run)
{
  int inst_id = -1;
  std::string orient_name;
  if (!readIntField(edit, "inst_id", inst_id, context) || !readStringField(edit, "orient", orient_name, context)) {
    return false;
  }
  IdbInstance* inst = instanceById(design, inst_id);
  if (inst == nullptr) {
    std::cout << "Apply view json edits failed: " << context << " references invalid inst_id " << inst_id << "." << std::endl;
    return false;
  }
  return applyOrient(inst, orient_name, context, dry_run);
}

bool applySetStatus(IdbDesign* design, const ViewJson& edit, const std::string& context, bool dry_run)
{
  int inst_id = -1;
  std::string status_name;
  if (!readIntField(edit, "inst_id", inst_id, context) || !readStringField(edit, "status", status_name, context)) {
    return false;
  }
  IdbInstance* inst = instanceById(design, inst_id);
  if (inst == nullptr) {
    std::cout << "Apply view json edits failed: " << context << " references invalid inst_id " << inst_id << "." << std::endl;
    return false;
  }
  return applyStatus(inst, status_name, context, dry_run);
}

bool applyMoveIoPin(IdbDesign* design, const ViewJson& edit, const std::string& context, bool dry_run)
{
  int pin_id = -1;
  int32_t x = 0;
  int32_t y = 0;
  if (!readIntField(edit, "pin_id", pin_id, context) || !readPointField(edit, "location", x, y, context)) {
    return false;
  }
  IdbPin* pin = ioPinById(design, pin_id);
  if (pin == nullptr) {
    std::cout << "Apply view json edits failed: " << context << " references invalid pin_id " << pin_id << "." << std::endl;
    return false;
  }

  std::string orient_name;
  bool has_orient = false;
  if (!readOptionalStringField(edit, "orient", orient_name, has_orient, context)) {
    return false;
  }
  IdbOrient orient = IdbOrient::kNone;
  if (has_orient) {
    if (!readOrientValue(orient_name, orient, context)) {
      return false;
    }
  }

  if (!dry_run) {
    if (has_orient) {
      pin->set_orient(orient);
    }
    pin->set_location(x, y);
    pin->set_bounding_box();
  }
  return true;
}

bool applyOrientIoPin(IdbDesign* design, const ViewJson& edit, const std::string& context, bool dry_run)
{
  int pin_id = -1;
  std::string orient_name;
  if (!readIntField(edit, "pin_id", pin_id, context) || !readStringField(edit, "orient", orient_name, context)) {
    return false;
  }
  IdbPin* pin = ioPinById(design, pin_id);
  if (pin == nullptr) {
    std::cout << "Apply view json edits failed: " << context << " references invalid pin_id " << pin_id << "." << std::endl;
    return false;
  }
  return applyOrient(pin, orient_name, context, dry_run);
}

bool processEdit(IdbDesign* design, const ViewJson& edit, const std::string& context, bool dry_run)
{
  if (!requireObject(edit, context)) {
    return false;
  }

  std::string op;
  if (!readStringField(edit, "op", op, context)) {
    return false;
  }

  if (op == "move_instance") {
    return applyMoveInstance(design, edit, context, dry_run);
  }
  if (op == "orient_instance") {
    return applyOrientInstance(design, edit, context, dry_run);
  }
  if (op == "set_status") {
    return applySetStatus(design, edit, context, dry_run);
  }
  if (op == "move_io_pin") {
    return applyMoveIoPin(design, edit, context, dry_run);
  }
  if (op == "orient_io_pin") {
    return applyOrientIoPin(design, edit, context, dry_run);
  }
  if (op == "delete_edit") {
    return true;
  }

  std::cout << "Apply view json edits failed: " << context << " has unsupported op `" << op << "`." << std::endl;
  return false;
}

bool processEdits(IdbDesign* design, const ViewJson& data, bool dry_run)
{
  for (size_t i = 0; i < data.size(); ++i) {
    const std::string context = "data[" + std::to_string(i) + "]";
    if (!processEdit(design, data[i], context, dry_run)) {
      return false;
    }
  }
  return true;
}

}  // namespace

ViewJsonEditApplier::ViewJsonEditApplier(IdbDefService* def_service) : _def_service(def_service)
{
}

bool ViewJsonEditApplier::apply(const std::string& edits_path)
{
  if (_def_service == nullptr || _def_service->get_design() == nullptr) {
    std::cout << "Apply view json edits failed: def service or design is null." << std::endl;
    return false;
  }

  std::ifstream stream(edits_path);
  if (!stream.is_open()) {
    std::cout << "Apply view json edits failed: cannot open " << edits_path << "." << std::endl;
    return false;
  }

  ViewJson root;
  try {
    stream >> root;
  } catch (const nlohmann::json::exception& error) {
    std::cout << "Apply view json edits failed: parse " << edits_path << " error: " << error.what() << std::endl;
    return false;
  }

  if (!requireObject(root, "root")) {
    return false;
  }
  if (!checkStringFieldValue(root, "schema", "ieda.view.edit.v1", "root")
      || !checkStringFieldValue(root, "kind", "layout_edits", "root")) {
    return false;
  }

  auto data_iter = root.find("data");
  if (data_iter == root.end() || !data_iter->is_array()) {
    std::cout << "Apply view json edits failed: `data` must be an array." << std::endl;
    return false;
  }

  IdbDesign* design = _def_service->get_design();
  if (!processEdits(design, *data_iter, true)) {
    return false;
  }

  return processEdits(design, *data_iter, false);
}

}  // namespace idb
