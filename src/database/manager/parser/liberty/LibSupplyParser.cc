// SPDX-License-Identifier: MulanPSL-2.0
#include <stdexcept>

#include "CppLibertyAST.hh"
#include "Lib.hh"
#include "LibParserCpp.hh"

namespace idb {
namespace {

void setSupplyAttribute(LibSupplyPin& pin, const std::string& name, const std::string& value)
{
  if (name == "pg_type") {
    pin.pg_type = value;
  } else if (name == "voltage_name") {
    pin.voltage_name = value;
  }
}

void addSupplyPin(LibCell& cell, const std::string& name, LibSupplyPin pin)
{
  if (name.empty() || !cell.get_supply_pins().emplace(name, std::move(pin)).second) {
    throw std::invalid_argument("Empty or duplicate pg_pin in cell " + std::string(cell.get_cell_name()));
  }
}

}  // namespace

unsigned LibertyReader::visitSupplyPin(LibertyGroupStmt* group)
{
  LibSupplyPin pin;
  void* statement;
  FOREACH_LIBERTY_VEC_ELEM(&group->stmts, void, statement)
  {
    if (liberty_is_simple_attri_stmt(statement)) {
      auto* attr = liberty_convert_simple_attribute_stmt(statement);
      void* raw_value = const_cast<void*>(attr->attri_value);
      if (liberty_is_string_value(raw_value)) {
        auto* value = liberty_convert_string_value(raw_value);
        setSupplyAttribute(pin, attr->attri_name, value->value);
        liberty_free_string_value(value);
      }
      liberty_free_simple_attribute_stmt(attr);
    }
  }
  addSupplyPin(*get_library_builder()->get_cell(), getGroupAttriName(group), std::move(pin));
  return 1;
}

unsigned LibertyReader::visitSupplyPin(liberty_ast::LibGroup* group)
{
  LibSupplyPin pin;
  for (auto* statement : group->getStatements()) {
    if (statement->isSimpleAttr()) {
      auto* attr = static_cast<liberty_ast::LibSimpleAttribute*>(statement);
      auto* value = attr->getFirstValue();
      if (value && value->isString()) {
        setSupplyAttribute(pin, attr->getName(), value->asString());
      }
    }
  }
  addSupplyPin(*get_library_builder()->get_cell(), getGroupAttriName(group), std::move(pin));
  return 1;
}

}  // namespace idb
