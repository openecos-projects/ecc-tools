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
#pragma once

#include "Database.hpp"
#include "PWHeader.hpp"
namespace ipw::sdc {

class SdcTclCmd;

enum class QueryObjectType
{
  kPort,
  kPin,
  kCell,
  kNet,
  kClock,
  kAny
};

struct ObjectQueryOptions
{
  bool regexp = false;
  bool nocase = false;
  bool exact = false;
  bool hierarchical = false;
  std::string filter;
  std::vector<std::string> of_objects;
};

std::vector<std::string> parseObjectPatterns(const std::string& text, bool regexp);
std::vector<std::string> findObjects(Database& database, const std::vector<std::string>& patterns, QueryObjectType type, bool regexp = false);
std::vector<std::string> findObjects(Database& database, const std::vector<std::string>& patterns, QueryObjectType type, const ObjectQueryOptions& options);
std::set<std::string> findClocks(Database& database, const std::vector<std::string>& objects);
std::vector<std::string> findClockSources(Database& database, const std::vector<std::string>& objects);

std::vector<std::string> getPortPinNames(Database& database, const std::vector<std::string>& object_list);
std::vector<std::string> getObjectNames(Database& database, const std::string& object_list);
TimingPortConstraint& getOrCreatePortConstraint(Database& database, const std::string& port_name);
void addObjectQueryOptions(SdcTclCmd& command, bool hierarchical, bool exact, bool of_objects);
ObjectQueryOptions getObjectQueryOptions(SdcTclCmd& command);
std::optional<std::string> getObjectQueryError(const ObjectQueryOptions& options, bool has_patterns);

}  // namespace ipw::sdc
